"""Shared configuration for the two-party BFIA HE-WOE example."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
INTERCONNECTION_ROOT = REPO_ROOT / "interconnection"
for source_root in (INTERCONNECTION_ROOT, REPO_ROOT):
    source_root_str = str(source_root)
    if source_root_str not in sys.path:
        sys.path.insert(0, source_root_str)


def _party_endpoints(args: argparse.Namespace, party: str) -> tuple[str, str, str, str]:
    if party == "alice":
        return (
            args.alice_address,
            args.alice_listen,
            "bob",
            args.bob_address,
        )
    return (
        args.bob_address,
        args.bob_listen,
        "alice",
        args.alice_address,
    )


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--alice-address", default="127.0.0.1:9844")
    parser.add_argument("--bob-address", default="127.0.0.1:9845")
    parser.add_argument("--alice-listen", default="0.0.0.0:9844")
    parser.add_argument("--bob-listen", default="0.0.0.0:9845")
    parser.add_argument(
        "--binning-method",
        choices=("quantile", "chimerge", "eq_range"),
        default="quantile",
    )
    parser.add_argument("--bin-num", type=int, default=10)
    parser.add_argument("--key-size", type=int, choices=(1024, 2048, 3072), default=2048)
    parser.add_argument(
        "--log-level",
        choices=("debug", "info", "warning", "error", "critical"),
        default="info",
    )


def _link_config(args: argparse.Namespace, self_party: str) -> Dict[str, Any]:
    return {
        "parties": {
            "alice": {
                "address": args.alice_address,
                "listen_addr": args.alice_listen,
            },
            "bob": {
                "address": args.bob_address,
                "listen_addr": args.bob_listen,
            },
        },
        "self_party": self_party,
    }


def _config(args: argparse.Namespace, self_party: str) -> Dict[str, Any]:
    return {
        "algo": "he_woe",
        "label_holder": "alice",
        "link": _link_config(args, self_party),
        "he_woe": {
            "binning_method": args.binning_method,
            "bin_num": args.bin_num,
            "positive_label": "1",
            "label_col": "label",
            "chimerge_init_bins": max(100, args.bin_num + 1),
            "chimerge_target_bins": max(2, min(args.bin_num, 10)),
            "chimerge_target_pvalue": 0.1,
        },
        "heu": {
            "sk_keeper": {"party": "alice"},
            "evaluators": [{"party": "bob"}],
            "he_parameters": {
                "schema": "ic-paillier",
                "key_pair": {"generate": {"bit_size": args.key_size}},
            },
        },
    }


def _dataset_for_party(party: str) -> Dict[str, Any]:
    from sklearn.datasets import load_breast_cancer

    data = load_breast_cancer()
    features, labels = data["data"], data["target"]
    names = list(data["feature_names"])

    if party == "alice":
        return {
            "features": {"alice": features[:, :15], "bob": None},
            "feature_names": {"alice": names[:15], "bob": names[15:]},
            "label": {"alice": labels},
        }
    if party == "bob":
        return {
            "features": {"alice": None, "bob": features[:, 15:]},
            "feature_names": {"alice": names[:15], "bob": names[15:]},
            "label": {"alice": None},
        }
    raise ValueError(f"unsupported party: {party}")


def run_party(party: str, argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description=f"Run the {party.capitalize()} party of BFIA HE-WOE."
    )
    add_common_arguments(parser)
    args = parser.parse_args(argv)
    local_address, local_listen, peer_party, peer_address = _party_endpoints(
        args, party
    )
    print(
        f"[BFIA][HE-WOE][{party}] 协议=BFIA互联互通协议；"
        f"本方端点={local_address}；监听={local_listen}；"
        f"对端={peer_party}({peer_address})；"
        "将通过两方真实IPv4网络端点通信",
        flush=True,
    )
    from secretflow.ic.runner import run

    result = run(
        config=_config(args, party),
        dataset=_dataset_for_party(party),
        logging_level=args.log_level,
    )
    if result is None:
        print(f"BFIA_HE_WOE_STATUS=failed party={party}", flush=True)
        return

    report = result.get("report", {})
    variables = report.get("variables", [])
    result_path = os.environ.get("BFIA_RESULT_FILE")
    if result_path:
        output_path = Path(result_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as output_file:
            json.dump(result, output_file, ensure_ascii=False, indent=2)

    print(
        "BFIA_HE_WOE_STATUS=completed "
        f"party={party} variables={len(variables)} "
        f"result_file={result_path or 'stdout'} "
        f"local={local_address} peer={peer_address} protocol=BFIA",
        flush=True,
    )


def build_parser(party: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=f"Run the {party.capitalize()} party of BFIA HE-WOE."
    )
    add_common_arguments(parser)
    return parser


def build_config_and_dataset(
    party: str, argv: list[str] | None = None
) -> Tuple[Dict[str, Any], Dict[str, Any], str]:
    args = build_parser(party).parse_args(argv)
    return _config(args, party), _dataset_for_party(party), args.log_level
