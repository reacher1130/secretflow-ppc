# Copyright 2026
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""BFIA HE-WOE reference handler.

The BFIA HE-WOE handshake is defined in ``he_woe.proto``.  This module adds
the data phase on top of the existing interconnection link:

* the label holder encrypts the binary label with Paillier and sends only the
  public key and ciphertext to feature parties;
* each feature party computes local bin membership and asks the evaluator to
  homomorphically sum the encrypted labels in every bin;
* only clear bin totals (which are already exposed by SecretFlow's native HE
  WOE implementation) and encrypted positive counts cross the link;
* the label holder decrypts the positive counts, computes WOE/IV, and sends
  WOE/IV/rates back to the feature party.

Split points and categories stay at their owning feature party.  The wire
messages use the existing BFIA ``DataExchangeProtocol`` serializer, so the
Paillier key/ciphertext representation is the same one used by the current
interconnection proxy.
"""

from __future__ import annotations

import logging
import math
from dataclasses import dataclass
from statistics import NormalDist
from typing import Dict, List, Sequence, Tuple, Union

import numpy as np
from google.protobuf import any_pb2
from heu import numpy as hnp
from interconnection.common import header_pb2
from interconnection.handshake import entry_pb2
from interconnection.handshake.algos import he_woe_pb2
from interconnection.handshake.protocol_family import phe_pb2

from secretflow.ic.handler import util
from secretflow.ic.handler.handler import IcHandler
from secretflow.ic.handler.protocol_family import phe
from secretflow.ic.proxy import LinkProxy


SUPPORTED_BINNING_METHODS = ("quantile", "chimerge", "eq_range")
_STATS_ROWS = 4  # WOE, IV, positive rate, total rate.


@dataclass
class HeWoeConfig:
    """Validated HE-WOE parameters used by both parties."""

    binning_method: str = "quantile"
    bin_num: int = 10
    positive_label: str = "1"
    label_col: str = "label"
    chimerge_init_bins: int = 100
    chimerge_target_bins: int = 10
    chimerge_target_pvalue: float = 0.1
    enable_audit_log: bool = False
    support_chimerge: bool = True
    support_quantile: bool = True
    support_eq_range: bool = True

    def __init__(self, config: dict):
        config = config or {}
        self.binning_method = config.get("binning_method", "quantile")
        self.bin_num = int(config.get("bin_num", 10))
        self.positive_label = str(config.get("positive_label", "1"))
        self.label_col = str(config.get("label_col", "label"))
        self.chimerge_init_bins = int(config.get("chimerge_init_bins", 100))
        self.chimerge_target_bins = int(config.get("chimerge_target_bins", 10))
        self.chimerge_target_pvalue = float(
            config.get("chimerge_target_pvalue", 0.1)
        )
        self.enable_audit_log = bool(config.get("enable_audit_log", False))
        self.support_chimerge = bool(config.get("support_chimerge", True))
        self.support_quantile = bool(config.get("support_quantile", True))
        self.support_eq_range = bool(config.get("support_eq_range", True))

        if self.binning_method not in SUPPORTED_BINNING_METHODS:
            raise ValueError(
                "binning_method must be one of "
                f"{SUPPORTED_BINNING_METHODS}, got {self.binning_method!r}"
            )
        if self.bin_num <= 0:
            raise ValueError(f"bin_num must be positive, got {self.bin_num}")
        if self.chimerge_init_bins <= 2:
            raise ValueError(
                "chimerge_init_bins must be greater than 2, "
                f"got {self.chimerge_init_bins}"
            )
        if not 2 <= self.chimerge_target_bins < self.chimerge_init_bins:
            raise ValueError(
                "chimerge_target_bins must be in "
                f"[2, {self.chimerge_init_bins}), got {self.chimerge_target_bins}"
            )
        if not 0 < self.chimerge_target_pvalue < 1:
            raise ValueError(
                "chimerge_target_pvalue must be in (0, 1), "
                f"got {self.chimerge_target_pvalue}"
            )

    @property
    def chimerge_target_chi(self) -> float:
        # Chi-square(1) is the square of a standard normal variable.  Using
        # the standard library avoids requiring SciPy's optional native
        # extensions in the IC runtime.
        return NormalDist().inv_cdf(1 - self.chimerge_target_pvalue / 2) ** 2

    def supports(self, method: str) -> bool:
        return {
            "chimerge": self.support_chimerge,
            "quantile": self.support_quantile,
            "eq_range": self.support_eq_range,
        }[method]


@dataclass
class FeatureBins:
    """Local bin membership and private split metadata for one feature."""

    name: str
    is_string: bool
    split_points: Union[np.ndarray, List[str]]
    bins: List[List[int]]
    else_bin: List[int]
    values: np.ndarray

    @property
    def bin_size(self) -> int:
        return len(self.bins)

    @property
    def all_selectors(self) -> List[List[int]]:
        return [*self.bins, self.else_bin]

    @property
    def total_counts(self) -> List[int]:
        return [len(item) for item in self.all_selectors]

    def apply_merge(self, removed_split_point_indices: Sequence[int]) -> None:
        """Apply label-holder-selected ChiMerge boundaries locally."""

        removed = sorted(set(int(index) for index in removed_split_point_indices))
        if not removed:
            return
        if self.is_string:
            raise ValueError("ChiMerge cannot remove boundaries of string features")
        if any(index < 0 or index >= len(self.split_points) for index in removed):
            raise ValueError(
                f"invalid merged split-point indices {removed} for feature {self.name}"
            )
        self.split_points = np.delete(np.asarray(self.split_points), removed)
        self.bins = _numeric_bin_indices(self.values, self.split_points)


def _is_missing(value) -> bool:
    if value is None:
        return True
    try:
        return bool(np.isnan(value))
    except (TypeError, ValueError):
        return False


def _as_feature_matrix(data) -> Tuple[np.ndarray, List[str]]:
    """Convert a local feature block to a 2-D array without revealing it."""

    names = None
    if hasattr(data, "to_numpy"):
        names = list(getattr(data, "columns", [])) or None
        data = data.to_numpy()
    elif hasattr(data, "to_pandas"):
        table = data.to_pandas()
        names = list(table.columns)
        data = table.to_numpy()

    matrix = np.asarray(data)
    if matrix.ndim == 1:
        matrix = matrix.reshape((-1, 1))
    if matrix.ndim != 2:
        raise ValueError(f"features must be a 1-D or 2-D array, got {matrix.shape}")
    if names is None:
        names = [f"f{index}" for index in range(matrix.shape[1])]
    if len(names) != matrix.shape[1]:
        raise ValueError("feature name count does not match feature column count")
    return matrix, [str(name) for name in names]


def _numeric_bin_indices(values: np.ndarray, split_points: Sequence[float]):
    """Build left-open/right-closed numeric bins, excluding missing values."""

    split_points = np.asarray(split_points, dtype=float)
    non_missing = np.array([not _is_missing(value) for value in values], dtype=bool)
    if non_missing.any():
        numeric_values = np.asarray(values[non_missing], dtype=float)
        if not np.isfinite(numeric_values).all():
            raise ValueError("numeric features may contain NaN, but not +/-inf")
        bin_ids = np.digitize(numeric_values, split_points, right=True)
        bins = []
        row_indices = np.flatnonzero(non_missing)
        for bin_id in range(len(split_points) + 1):
            bins.append(row_indices[bin_ids == bin_id].astype(int).tolist())
        return bins
    return []


def _build_numeric_bins(
    values: np.ndarray, bin_num: int, method: str
) -> Tuple[np.ndarray, List[List[int]]]:
    non_missing = np.array([not _is_missing(value) for value in values], dtype=bool)
    if not non_missing.any():
        return np.asarray([], dtype=float), []

    numeric_values = np.asarray(values[non_missing], dtype=float)
    if not np.isfinite(numeric_values).all():
        raise ValueError("numeric features may contain NaN, but not +/-inf")
    if np.min(numeric_values) == np.max(numeric_values):
        return np.asarray([], dtype=float), [np.flatnonzero(non_missing).tolist()]

    if method == "eq_range":
        candidates = np.linspace(
            float(np.min(numeric_values)), float(np.max(numeric_values)), bin_num + 1
        )[1:-1]
    else:
        # ChiMerge uses the initial-bin count, while quantile uses the final
        # requested count.  np.unique mirrors qcut's duplicate-boundary rule.
        quantiles = np.linspace(0.0, 1.0, bin_num + 1)
        candidates = np.unique(np.quantile(numeric_values, quantiles)[1:-1])

    split_points = np.asarray(candidates, dtype=float)
    # Duplicate values can make an otherwise valid quantile boundary produce
    # an empty interval. Remove such boundaries until every remaining bin has
    # at least one sample, matching the native WOE worker's qcut behavior.
    while split_points.size:
        bins = _numeric_bin_indices(values, split_points)
        empty = next((index for index, item in enumerate(bins) if not item), None)
        if empty is None:
            break
        split_index = min(empty, split_points.size - 1)
        split_points = np.delete(split_points, split_index)

    return split_points, _numeric_bin_indices(values, split_points)


def _build_feature_bins(
    data, config: HeWoeConfig, feature_names: Dict[str, Sequence[str]], party: str
) -> List[FeatureBins]:
    matrix, inferred_names = _as_feature_matrix(data)
    configured_names = feature_names.get(party) if feature_names else None
    names = [str(item) for item in configured_names] if configured_names else inferred_names
    if len(names) != matrix.shape[1]:
        raise ValueError(
            f"feature name count {len(names)} does not match {matrix.shape[1]} columns"
        )

    result = []
    initial_bin_num = (
        config.chimerge_init_bins
        if config.binning_method == "chimerge"
        else config.bin_num
    )
    for column_index, name in enumerate(names):
        values = matrix[:, column_index]
        non_missing_values = [value for value in values if not _is_missing(value)]
        numeric = np.issubdtype(values.dtype, np.number) and not np.issubdtype(
            values.dtype, np.bool_
        )
        if not numeric and non_missing_values:
            try:
                np.asarray(non_missing_values, dtype=float)
                numeric = True
            except (TypeError, ValueError):
                numeric = False

        if numeric:
            split_points, bins = _build_numeric_bins(
                values, initial_bin_num, config.binning_method
            )
            else_bin = [
                int(index)
                for index, value in enumerate(values)
                if _is_missing(value)
            ]
            result.append(
                FeatureBins(
                    name=name,
                    is_string=False,
                    split_points=split_points,
                    bins=bins,
                    else_bin=else_bin,
                    values=values,
                )
            )
        else:
            categories = sorted(
                {str(value) for value in non_missing_values}
            )
            bins = [
                [
                    int(index)
                    for index, value in enumerate(values)
                    if not _is_missing(value) and str(value) == category
                ]
                for category in categories
            ]
            else_bin = [
                int(index)
                for index, value in enumerate(values)
                if _is_missing(value)
            ]
            result.append(
                FeatureBins(
                    name=name,
                    is_string=True,
                    split_points=categories,
                    bins=bins,
                    else_bin=else_bin,
                    values=values,
                )
            )
    return result


def _chi_value(left: Tuple[int, int], right: Tuple[int, int]) -> float:
    total = left[0] + right[0]
    positives = left[1] + right[1]
    if total == 0:
        return 0.0
    positive_rate = positives / float(total)
    if positive_rate == 0 or positive_rate == 1:
        return 0.0

    expected_left_positive = positive_rate * left[0]
    expected_left_negative = left[0] - expected_left_positive
    expected_right_positive = positive_rate * right[0]
    expected_right_negative = right[0] - expected_right_positive
    return sum(
        (
            (left[1] - expected_left_positive) ** 2 / expected_left_positive,
            (left[0] - left[1] - expected_left_negative) ** 2
            / expected_left_negative,
            (right[1] - expected_right_positive) ** 2 / expected_right_positive,
            (right[0] - right[1] - expected_right_negative) ** 2
            / expected_right_negative,
        )
    )


def _chi_merge(
    bins: Sequence[Tuple[int, int]], target_bins: int, target_chi: float
) -> Tuple[List[Tuple[int, int]], List[int]]:
    """ChiMerge with the same removed-boundary convention as SecretFlow."""

    current = [tuple(map(int, item)) for item in bins]
    original_indices = list(range(len(current)))
    removed = []
    while len(current) > target_bins and len(current) > 1:
        chis = [_chi_value(current[i], current[i + 1]) for i in range(len(current) - 1)]
        min_index = int(np.argmin(chis))
        if chis[min_index] > target_chi:
            break
        current[min_index] = (
            current[min_index][0] + current[min_index + 1][0],
            current[min_index][1] + current[min_index + 1][1],
        )
        current.pop(min_index + 1)
        removed.append(original_indices.pop(min_index))
    return current, removed


def _calc_woe_iv(
    bin_total: int, bin_positive: int, total_labels: int, total_positives: int
) -> Tuple[float, float]:
    total_negatives = total_labels - total_positives
    if total_positives <= 0 or total_negatives <= 0:
        raise ValueError("HE-WOE requires both positive and negative labels")
    bin_negative = bin_total - bin_positive
    if bin_negative == 0 or bin_positive == 0:
        positive_distribution = (bin_positive + 0.5) / total_positives
        negative_distribution = (bin_negative + 0.5) / total_negatives
    else:
        positive_distribution = bin_positive / float(total_positives)
        negative_distribution = bin_negative / float(total_negatives)
    woe = math.log(positive_distribution / negative_distribution)
    return woe, (positive_distribution - negative_distribution) * woe


def _calc_rate(bin_total: int, bin_positive: int, row_count: int) -> Tuple[float, float]:
    if bin_total < 5 or bin_positive == 0 or bin_positive == bin_total:
        return float("nan"), float("nan")
    return bin_positive / float(bin_total), bin_total / float(row_count)


def _feature_stats(
    feature: FeatureBins,
    positive_counts: Sequence[int],
    total_labels: int,
    total_positives: int,
    row_count: int,
    config: HeWoeConfig,
) -> Tuple[List[Tuple[float, float, float, float]], List[int]]:
    expected = feature.bin_size + 1
    if len(positive_counts) != expected:
        raise ValueError(
            f"positive count length {len(positive_counts)} does not match "
            f"feature {feature.name} bin count {expected}"
        )
    totals = feature.total_counts
    regular_stats = list(zip(totals[:-1], map(int, positive_counts[:-1])))
    removed = []
    if config.binning_method == "chimerge" and not feature.is_string:
        regular_stats, removed = _chi_merge(
            regular_stats,
            config.chimerge_target_bins,
            config.chimerge_target_chi,
        )
    stats = []
    for total, positive in [*regular_stats, (totals[-1], int(positive_counts[-1]))]:
        woe, iv = _calc_woe_iv(total, positive, total_labels, total_positives)
        positive_rate, total_rate = _calc_rate(total, positive, row_count)
        stats.append((woe, iv, positive_rate, total_rate))
    return stats, removed


def _flatten_stats(
    features: Sequence[FeatureBins],
    stats_by_feature: Sequence[Sequence[Tuple[float, float, float, float]]],
) -> np.ndarray:
    flat = [item for feature_stats in stats_by_feature for item in feature_stats]
    if not flat:
        return np.empty((0, 0), dtype=np.float64)
    return np.asarray(flat, dtype=np.float64).T.reshape((_STATS_ROWS, -1))


def _build_report(
    features: Sequence[FeatureBins],
    flat_stats: np.ndarray,
    include_feature_iv_info: bool,
) -> dict:
    variables = []
    feature_iv_info = []
    offset = 0
    for feature in features:
        count = feature.bin_size + 1
        if offset + count > flat_stats.shape[1]:
            raise ValueError("HE-WOE response does not contain enough bin statistics")
        feature_stats = flat_stats[:, offset : offset + count]
        offset += count
        regular_count = feature.bin_size
        split_points = feature.split_points
        if isinstance(split_points, np.ndarray):
            split_points = split_points.astype(float).tolist()
        else:
            split_points = list(split_points)
        variables.append(
            {
                "name": feature.name,
                "type": "string" if feature.is_string else "numeric",
                "categories": split_points if feature.is_string else [],
                "split_points": split_points if not feature.is_string else [],
                "total_counts": feature.total_counts[:-1],
                "else_counts": feature.total_counts[-1],
                "filling_values": feature_stats[0, :regular_count].tolist(),
                "else_filling_value": float(feature_stats[0, -1]),
                "positive_rates": feature_stats[2, :regular_count].tolist(),
                "else_positive_rate": float(feature_stats[2, -1]),
                "total_rates": feature_stats[3, :regular_count].tolist(),
                "else_total_rate": float(feature_stats[3, -1]),
                "ivs": feature_stats[1, :regular_count].tolist(),
                "else_iv": float(feature_stats[1, -1]),
            }
        )
        if include_feature_iv_info:
            feature_iv_info.append(
                {
                    "name": feature.name,
                    "ivs": feature_stats[1, :regular_count].tolist(),
                    "else_iv": float(feature_stats[1, -1]),
                    "feature_iv": float(np.nansum(feature_stats[1, :])),
                }
            )

    if offset != flat_stats.shape[1]:
        raise ValueError("HE-WOE response contains unexpected bin statistics")
    report = {"variables": variables}
    if include_feature_iv_info:
        report["feature_iv_info"] = feature_iv_info
    return report


class HeWoeIcHandler(IcHandler):
    """Two-party/multi-party BFIA HE-WOE handler using Paillier PHE."""

    def __init__(self, config: dict, dataset: dict):
        super().__init__(dataset)
        self._config = config
        self._woe = HeWoeConfig(config.get("he_woe", config.get("woe", {})))
        self._phe = phe.PheConfig(config["heu"])
        self._label_holder = config.get("label_holder", LinkProxy.all_parties[0])
        self._feature_names = dataset.get("feature_names", {})
        if self._label_holder not in LinkProxy.all_parties:
            raise ValueError(f"unknown label holder {self._label_holder}")
        if LinkProxy.all_parties[0] != self._label_holder:
            raise ValueError(
                "BFIA HE-WOE currently requires the label holder to be the first "
                "party because the existing IC handshake uses the first party as "
                "the passive responder"
            )

    def _build_handshake_request(self) -> entry_pb2.HandshakeRequest:
        request = entry_pb2.HandshakeRequest(version=2)
        proposal = he_woe_pb2.HeWoeParamsProposal(supported_versions=[1])
        proposal.support_chimerge = self._woe.support_chimerge
        proposal.support_quantile = self._woe.support_quantile
        proposal.support_eq_range = self._woe.support_eq_range
        request.supported_algos.append(entry_pb2.ALGO_TYPE_HE_WOE)
        algo_params = any_pb2.Any()
        algo_params.Pack(proposal)
        request.algo_params.append(algo_params)
        self._append_phe_proposal(request)
        return request

    def _append_phe_proposal(self, request: entry_pb2.HandshakeRequest) -> None:
        proposal = phe_pb2.PheProtocolProposal(supported_versions=[1])
        proposal.supported_phe_algos.append(phe_pb2.PHE_ALGO_PAILLIER)
        paillier = phe_pb2.PaillierParamsProposal(key_sizes=[1024, 2048, 3072])
        param = any_pb2.Any()
        param.Pack(paillier)
        proposal.supported_phe_params.append(param)
        protocol = any_pb2.Any()
        protocol.Pack(proposal)
        request.protocol_families.append(entry_pb2.PROTOCOL_FAMILY_PHE)
        request.protocol_family_params.append(protocol)

    def _build_handshake_response(self) -> entry_pb2.HandshakeResponse:
        response = super()._build_handshake_response()
        response.algo = entry_pb2.ALGO_TYPE_HE_WOE
        result = he_woe_pb2.HeWoeParamsResult(
            version=1,
            binning_method=self._woe.binning_method,
            bin_num=self._woe.bin_num,
            positive_label=self._woe.positive_label,
            chimerge_init_bins=self._woe.chimerge_init_bins,
            chimerge_target_bins=self._woe.chimerge_target_bins,
            chimerge_target_pvalue=self._woe.chimerge_target_pvalue,
            enable_audit_log=self._woe.enable_audit_log,
            label_col=self._woe.label_col,
        )
        response.algo_param.Pack(result)

        phe_result = phe_pb2.PheProtocolResult(version=1, phe_algo=self._phe.phe_algo)
        if phe_result.phe_algo != phe_pb2.PHE_ALGO_PAILLIER:
            raise ValueError("BFIA HE-WOE currently supports Paillier only")
        phe_result.phe_param.Pack(
            phe_pb2.PaillierParamsResult(key_size=self._phe.key_size)
        )
        protocol = any_pb2.Any()
        protocol.Pack(phe_result)
        response.protocol_families.append(entry_pb2.PROTOCOL_FAMILY_PHE)
        response.protocol_family_params.append(protocol)
        return response

    def _process_handshake_response(self, response: entry_pb2.HandshakeResponse) -> bool:
        if not super()._process_handshake_response(response):
            return False
        if response.algo != entry_pb2.ALGO_TYPE_HE_WOE:
            return False
        result = he_woe_pb2.HeWoeParamsResult()
        if not response.algo_param.Unpack(result) or result.version != 1:
            return False
        if not self._woe.supports(result.binning_method):
            return False
        self._woe.binning_method = result.binning_method
        self._woe.bin_num = result.bin_num
        self._woe.positive_label = result.positive_label
        self._woe.chimerge_init_bins = result.chimerge_init_bins
        self._woe.chimerge_target_bins = result.chimerge_target_bins
        self._woe.chimerge_target_pvalue = result.chimerge_target_pvalue
        self._woe.enable_audit_log = result.enable_audit_log
        self._woe.label_col = result.label_col

        if not response.protocol_families:
            return False
        phe_result = phe_pb2.PheProtocolResult()
        if not response.protocol_family_params[0].Unpack(phe_result):
            return False
        if phe_result.phe_algo != phe_pb2.PHE_ALGO_PAILLIER:
            return False
        self._phe.phe_algo = phe_result.phe_algo
        logging.info(
            'BFIA协议HE-WOE握手参数已协商：分箱方法=%s，分箱数=%d，Paillier密钥位数=%d',
            self._woe.binning_method,
            self._woe.bin_num,
            self._phe.key_size,
        )
        return True

    def _negotiate_handshake_params(
        self, requests: List[entry_pb2.HandshakeRequest]
    ) -> Tuple[int, str]:
        proposals = util.extract_req_algo_params(
            requests,
            entry_pb2.ALGO_TYPE_HE_WOE,
            he_woe_pb2.HeWoeParamsProposal,
        )
        if not proposals:
            return header_pb2.HANDSHAKE_REFUSED, "negotiate he-woe algo failed"
        method_support = {
            "chimerge": "support_chimerge",
            "quantile": "support_quantile",
            "eq_range": "support_eq_range",
        }[self._woe.binning_method]
        if any(1 not in proposal.supported_versions for proposal in proposals):
            return header_pb2.HANDSHAKE_REFUSED, "unsupported he-woe version"
        if any(not getattr(proposal, method_support) for proposal in proposals):
            return header_pb2.HANDSHAKE_REFUSED, (
                f"peer does not support {self._woe.binning_method} binning"
            )

        phe_proposals = util.extract_req_protocol_family_params(
            requests,
            entry_pb2.PROTOCOL_FAMILY_PHE,
            phe_pb2.PheProtocolProposal,
        )
        if not phe_proposals or not self._negotiate_paillier(phe_proposals):
            return header_pb2.HANDSHAKE_REFUSED, "negotiate paillier params failed"
        return header_pb2.OK, ""

    def _negotiate_paillier(self, proposals: List[phe_pb2.PheProtocolProposal]) -> bool:
        if any(phe_pb2.PHE_ALGO_PAILLIER not in item.supported_phe_algos for item in proposals):
            return False
        params = util.extract_req_phe_params(
            proposals,
            phe_pb2.PHE_ALGO_PAILLIER,
            phe_pb2.PaillierParamsProposal,
        )
        if not params:
            return False
        return self._phe.key_size in util.intersect_param_items(params, "key_sizes")

    def _run_algo(self):
        logging.info(
            'BFIA协议算法开始：HE-WOE；本方=%s；标签持有方=%s；分箱方法=%s',
            LinkProxy.self_party,
            self._label_holder,
            self._woe.binning_method,
        )
        if LinkProxy.self_party == self._label_holder:
            return self._run_label_holder()
        return self._run_feature_holder()

    def _local_features(self):
        features = self._dataset.get("features", {})
        data = features.get(LinkProxy.self_party)
        if data is None:
            return np.empty((self._label_row_count(), 0), dtype=np.float64)
        return data

    def _label_row_count(self) -> int:
        labels = self._dataset.get("label", {}).get(self._label_holder)
        if labels is None:
            raise ValueError("label holder must provide the label array")
        return int(np.asarray(labels).reshape(-1).size)

    def _encode_label(self) -> np.ndarray:
        labels = np.asarray(
            self._dataset.get("label", {}).get(self._label_holder)
        ).reshape(-1)
        encoded = np.asarray(
            [1 if str(value) == self._woe.positive_label else 0 for value in labels],
            dtype=np.int64,
        )
        if encoded.size == 0 or encoded.sum() == 0 or encoded.sum() == encoded.size:
            raise ValueError("HE-WOE requires both positive and negative labels")
        return encoded

    def _run_label_holder(self):
        labels = self._encode_label()
        local_features = _build_feature_bins(
            self._local_features(), self._woe, self._feature_names, LinkProxy.self_party
        )
        logging.info(
            'BFIA协议HE-WOE标签方本地数据准备完成：样本数=%d，特征数=%d，正样本数=%d',
            labels.size,
            len(local_features),
            int(labels.sum()),
        )
        local_stats = []
        for feature in local_features:
            positive_counts = [int(labels[item].sum()) for item in feature.all_selectors]
            stats, removed = _feature_stats(
                feature,
                positive_counts,
                labels.size,
                int(labels.sum()),
                labels.size,
                self._woe,
            )
            feature.apply_merge(removed)
            local_stats.append(stats)
        local_report = _build_report(
            local_features, _flatten_stats(local_features, local_stats), True
        )

        hekit = hnp.setup(self._phe.config["he_parameters"]["schema"][3:], self._phe.key_size)
        encoder = hekit.bigint_encoder()
        encrypted_labels = hekit.encryptor().encrypt(
            hekit.array(labels, encoder)
        )
        logging.info(
            'BFIA协议HE-WOE标签方已生成Paillier密文标签：样本数=%d；开始发送公钥和密文',
            labels.size,
        )
        for party in LinkProxy.all_parties:
            if party == self._label_holder:
                continue
            LinkProxy.send(party, hekit.public_key())
            LinkProxy.send(party, encrypted_labels)

        for party in LinkProxy.all_parties:
            if party == self._label_holder:
                continue
            bin_sizes = np.asarray(LinkProxy.recv(party), dtype=np.int64).reshape(-1)
            is_string = np.asarray(LinkProxy.recv(party), dtype=np.int64).reshape(-1)
            total_counts = np.asarray(LinkProxy.recv(party), dtype=np.int64).reshape(-1)
            encrypted_positive = LinkProxy.recv(party)
            logging.info(
                'BFIA协议HE-WOE标签方收到特征方统计请求：来源=%s，特征数=%d，统计桶数=%d',
                party,
                bin_sizes.size,
                total_counts.size,
            )
            response_stats, removed = self._remote_stats(
                bin_sizes, is_string, total_counts, encrypted_positive, hekit, encoder, labels
            )
            LinkProxy.send(party, np.asarray([len(item) for item in removed], dtype=np.int64))
            LinkProxy.send(
                party,
                np.asarray([item for group in removed for item in group], dtype=np.int64),
            )
            LinkProxy.send(party, response_stats)
            logging.info(
                'BFIA协议HE-WOE标签方完成远端统计并返回结果：目标=%s，结果行数=%d',
                party,
                response_stats.size,
            )
        return {"party": LinkProxy.self_party, "report": local_report}

    def _remote_stats(
        self,
        bin_sizes: np.ndarray,
        is_string: np.ndarray,
        total_counts: np.ndarray,
        encrypted_positive,
        hekit,
        encoder,
        labels: np.ndarray,
    ) -> Tuple[np.ndarray, List[List[int]]]:
        if bin_sizes.size != is_string.size:
            raise ValueError("remote HE-WOE feature metadata has inconsistent lengths")
        expected = int(bin_sizes.sum()) + int(bin_sizes.size)
        if total_counts.size != expected:
            raise ValueError(
                f"remote total count length {total_counts.size} != expected {expected}"
            )
        decrypted = hekit.decryptor().decrypt(encrypted_positive).to_numpy(encoder)
        positives = np.asarray(decrypted, dtype=np.int64).reshape(-1)
        if positives.size != expected:
            raise ValueError("remote encrypted positive count length mismatch")

        stats_by_feature = []
        removed_by_feature = []
        offset = 0
        for size, string_flag in zip(bin_sizes.tolist(), is_string.tolist()):
            size = int(size)
            feature_total = int(total_counts[offset : offset + size + 1].sum())
            if feature_total != labels.size:
                raise ValueError(
                    "feature party row count does not match label party "
                    f"for feature offset {offset}: {feature_total} != {labels.size}"
                )
            feature = FeatureBins(
                name="",
                is_string=bool(string_flag),
                split_points=[],
                bins=[[] for _ in range(size)],
                else_bin=[],
                values=np.asarray([], dtype=float),
            )
            # The remote feature's actual split points remain private.  Only
            # counts and type/bin cardinality are needed for WOE/ChiMerge.
            feature.bins = [
                list(range(int(total_counts[offset + index])))
                for index in range(size)
            ]
            feature.else_bin = list(range(int(total_counts[offset + size])))
            positive_slice = positives[offset : offset + size + 1]
            stats, removed = _feature_stats(
                feature,
                positive_slice,
                labels.size,
                int(labels.sum()),
                labels.size,
                self._woe,
            )
            stats_by_feature.append(stats)
            removed_by_feature.append(removed)
            offset += size + 1
        return _flatten_stats([], stats_by_feature), removed_by_feature

    def _run_feature_holder(self):
        if LinkProxy.self_party == self._label_holder:
            raise AssertionError("feature holder cannot be the label holder")
        public_key = LinkProxy.recv(self._label_holder)
        encrypted_labels = LinkProxy.recv(self._label_holder)
        logging.info(
            'BFIA协议HE-WOE特征方收到Paillier公钥和密文标签：来源=%s',
            self._label_holder,
        )
        evaluator = hnp.setup(public_key).evaluator()
        features = _build_feature_bins(
            self._local_features(), self._woe, self._feature_names, LinkProxy.self_party
        )
        if not features:
            raise ValueError("feature holder must provide at least one feature")
        logging.info(
            'BFIA协议HE-WOE特征方本地数据准备完成：样本数=%d，特征数=%d，统计桶数=%d',
            _as_feature_matrix(self._local_features())[0].shape[0],
            len(features),
            sum(feature.bin_size + 1 for feature in features),
        )
        selectors = [selector for feature in features for selector in feature.all_selectors]
        bin_sizes = np.asarray([feature.bin_size for feature in features], dtype=np.int64)
        is_string = np.asarray(
            [int(feature.is_string) for feature in features], dtype=np.int64
        )
        total_counts = np.asarray(
            [count for feature in features for count in feature.total_counts],
            dtype=np.int64,
        )
        row_count = _as_feature_matrix(self._local_features())[0].shape[0]
        for feature in features:
            feature_count = sum(feature.total_counts)
            if feature_count != row_count:
                raise ValueError(
                    f"feature {feature.name} bins cover {feature_count} samples, "
                    f"but the feature block has {row_count} rows"
                )
        LinkProxy.send(self._label_holder, bin_sizes)
        LinkProxy.send(self._label_holder, is_string)
        LinkProxy.send(self._label_holder, total_counts)
        encrypted_positive = evaluator.batch_select_sum(encrypted_labels, selectors)
        LinkProxy.send(self._label_holder, encrypted_positive)
        logging.info(
            'BFIA协议HE-WOE特征方已完成同态分桶求和并发送密文统计：目标=%s，特征数=%d，选择器数=%d',
            self._label_holder,
            len(features),
            len(selectors),
        )

        removed_sizes = np.asarray(LinkProxy.recv(self._label_holder), dtype=np.int64).reshape(-1)
        removed_indices = np.asarray(
            LinkProxy.recv(self._label_holder), dtype=np.int64
        ).reshape(-1)
        stats = np.asarray(LinkProxy.recv(self._label_holder), dtype=np.float64)
        if removed_sizes.size != len(features):
            raise ValueError("HE-WOE merge response feature count mismatch")
        removed_by_feature = []
        offset = 0
        for size in removed_sizes.tolist():
            size = int(size)
            removed_by_feature.append(removed_indices[offset : offset + size].tolist())
            offset += size
        if offset != removed_indices.size:
            raise ValueError("HE-WOE merge response contains extra indices")
        for feature, removed in zip(features, removed_by_feature):
            feature.apply_merge(removed)
        report = _build_report(features, stats, False)
        logging.info(
            'BFIA协议HE-WOE特征方收到统计结果并完成报告：来源=%s，报告特征数=%d，统计值数=%d',
            self._label_holder,
            len(features),
            stats.size,
        )
        return {"party": LinkProxy.self_party, "report": report}
