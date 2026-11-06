import math

import numpy as np

from secretflow.ic.handler.he_woe_handler import (
    HeWoeConfig,
    _build_feature_bins,
    _build_report,
    _calc_woe_iv,
    _feature_stats,
    _flatten_stats,
)


def test_he_woe_binning_keeps_numeric_and_categorical_metadata_local():
    config = HeWoeConfig({"binning_method": "quantile", "bin_num": 3})
    features = np.array(
        [
            [0.1, "a"],
            [0.2, "b"],
            [0.3, "a"],
            [0.4, "c"],
            [0.5, None],
            [0.6, "b"],
        ],
        dtype=object,
    )
    bins = _build_feature_bins(features, config, {}, "bob")

    assert bins[0].is_string is False
    assert bins[0].bin_size == 3
    assert bins[0].total_counts == [2, 2, 2, 0]
    assert bins[1].is_string is True
    assert bins[1].split_points == ["a", "b", "c"]
    assert bins[1].else_bin == [4]


def test_he_woe_matches_the_native_smoothing_formula():
    woe, iv = _calc_woe_iv(5, 0, 10, 4)
    expected_woe = math.log((0.5 / 4) / (5.5 / 6))
    expected_iv = (0.5 / 4 - 5.5 / 6) * expected_woe
    assert math.isclose(woe, expected_woe)
    assert math.isclose(iv, expected_iv)


def test_he_woe_chimerge_returns_reusable_removed_boundaries():
    config = HeWoeConfig(
        {
            "binning_method": "chimerge",
            "chimerge_init_bins": 6,
            "chimerge_target_bins": 2,
        }
    )
    features = np.arange(6, dtype=float).reshape(-1, 1)
    bins = _build_feature_bins(features, config, {}, "bob")
    labels = np.array([0, 1, 1, 1, 0, 0])
    positive_counts = [int(labels[item].sum()) for item in bins[0].all_selectors]
    stats, removed = _feature_stats(
        bins[0], positive_counts, labels.size, int(labels.sum()), labels.size, config
    )
    bins[0].apply_merge(removed)

    assert removed
    assert len(stats) == bins[0].bin_size + 1
    assert len(bins[0].split_points) == bins[0].bin_size - 1


def test_he_woe_report_contains_native_woe_rule_fields():
    config = HeWoeConfig({"binning_method": "eq_range", "bin_num": 2})
    features = _build_feature_bins(
        np.array([[0.0], [1.0], [2.0], [3.0], [4.0], [5.0]]),
        config,
        {"alice": ["age"]},
        "alice",
    )
    labels = np.array([0, 0, 0, 1, 1, 1])
    stats = []
    for feature in features:
        positive_counts = [int(labels[item].sum()) for item in feature.all_selectors]
        feature_stats, _ = _feature_stats(
            feature, positive_counts, labels.size, int(labels.sum()), labels.size, config
        )
        stats.append(feature_stats)

    report = _build_report(features, _flatten_stats(features, stats), False)
    variable = report["variables"][0]
    assert variable["name"] == "age"
    assert variable["split_points"]
    assert len(variable["filling_values"]) == 2
    assert "else_filling_value" in variable
