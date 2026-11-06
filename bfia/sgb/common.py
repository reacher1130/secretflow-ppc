"""Shared configuration for the two-party BFIA SGB example.

The example is intentionally kept outside the ``secretflow`` package. When it
is launched directly from this directory, this module adds both the SecretFlow
source tree and the local ``interconnection`` package root to ``sys.path``.
This makes the example usable from a source checkout without requiring a
temporary package installation just for imports.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Dict, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
INTERCONNECTION_ROOT = REPO_ROOT / "interconnection"

# ``secretflow`` is under REPO_ROOT, while the Python package named
# ``interconnection`` is under REPO_ROOT/interconnection/interconnection.
for source_root in (INTERCONNECTION_ROOT, REPO_ROOT):
    source_root_str = str(source_root)
    if source_root_str not in sys.path:
        sys.path.insert(0, source_root_str)

DEFAULT_ALICE_ADDRESS = "127.0.0.1:9844"
DEFAULT_BOB_ADDRESS = "127.0.0.1:9845"
DEFAULT_LISTEN_ADDRESS = "0.0.0.0"
DEFAULT_NUM_ROUND = 5


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
    """Add network and SGB arguments shared by both party entrypoints."""

    parser.add_argument(
        "--alice-address",
        default=DEFAULT_ALICE_ADDRESS,
        help="Alice address advertised to Bob (default: %(default)s)",
    )
    parser.add_argument(
        "--bob-address",
        default=DEFAULT_BOB_ADDRESS,
        help="Bob address advertised to Alice (default: %(default)s)",
    )
    parser.add_argument(
        "--alice-listen",
        default=f"{DEFAULT_LISTEN_ADDRESS}:9844",
        help="Alice local listen address (default: %(default)s)",
    )
    parser.add_argument(
        "--bob-listen",
        default=f"{DEFAULT_LISTEN_ADDRESS}:9845",
        help="Bob local listen address (default: %(default)s)",
    )
    parser.add_argument(
        "--num-round",
        type=int,
        default=DEFAULT_NUM_ROUND,
        help="Number of boosting rounds used by Alice (default: %(default)s)",
    )
    parser.add_argument(
        "--log-level",
        default="info",
        choices=("debug", "info", "warning", "error", "critical"),
        help="SecretFlow log level (default: %(default)s)",
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


def _alice_config(args: argparse.Namespace) -> Dict[str, Any]:
    return {
        "algo": "sgb",
        "link": _link_config(args, "alice"),
        "xgb": {
            "num_round": args.num_round,
            "max_depth": 5,
            "bucket_eps": 0.08,
            "objective": "logistic",
            "reg_lambda": 0.3,
            "row_sample_by_tree": 0.9,
            "col_sample_by_tree": 0.9,
            "gamma": 1.0,
            "use_completely_sgb": False,
        },
        "heu": {
            "sk_keeper": {"party": "alice"},
            "evaluators": [{"party": "bob"}],
            "he_parameters": {
                "schema": "ic-paillier",
                "key_pair": {
                    "generate": {
                        # 2048 is the minimum size recommended by the
                        # original BFIA SGB example for real deployments.
                        "bit_size": 2048,
                    }
                },
            },
        },
    }


def _bob_config(args: argparse.Namespace) -> Dict[str, Any]:
    return {
        "algo": "sgb",
        "link": _link_config(args, "bob"),
        "xgb": {
            # Bob does not hold the label. These fields describe the features
            # Bob can support and are reconciled during the handshake.
            "support_completely_sgb": True,
            "support_row_sample_by_tree": True,
            "support_col_sample_by_tree": True,
        },
        "heu": {
            "sk_keeper": {"party": "alice"},
            "evaluators": [{"party": "bob"}],
            "he_parameters": {
                # Alice owns the secret key and generates the key pair. Bob
                # only needs the interconnection schema as an evaluator.
                "schema": "ic-paillier",
            },
        },
    }


def _dataset_for_party(party: str) -> Dict[str, Any]:
    """Build the same vertical dataset view expected by both parties."""

    # Keep argument/configuration inspection usable without importing the
    # optional dataset dependency. Training still requires scikit-learn.
    from sklearn.datasets import load_breast_cancer

    data = load_breast_cancer()
    features, labels = data["data"], data["target"]

    if party == "alice":
        return {
            "features": {
                "alice": features[:, :15],
                "bob": None,
            },
            "label": {"alice": labels},
        }

    if party == "bob":
        return {
            "features": {
                "alice": None,
                "bob": features[:, 15:],
            },
            "label": {"alice": None},
        }

    raise ValueError(f"unsupported party: {party}")


def run_party(party: str, argv: list[str] | None = None) -> None:
    """Parse arguments and start one BFIA SGB party instance."""

    parser = argparse.ArgumentParser(
        description=f"Run the {party.capitalize()} party of BFIA SGB."
    )
    add_common_arguments(parser)
    args = parser.parse_args(argv)

    local_address, local_listen, peer_party, peer_address = _party_endpoints(
        args, party
    )
    print(
        f"[BFIA][SGB][{party}] 协议=BFIA互联互通协议；"
        f"本方端点={local_address}；监听={local_listen}；"
        f"对端={peer_party}({peer_address})；"
        "将通过两方真实IPv4网络端点通信",
        flush=True,
    )

    # Import after source paths are prepared. This keeps direct source-tree
    # execution independent from the caller's current working directory.
    from secretflow.ic.runner import run

    config = _alice_config(args) if party == "alice" else _bob_config(args)
    dataset = _dataset_for_party(party)
    run(config=config, dataset=dataset, logging_level=args.log_level)
    print(
        f"BFIA_SGB_STATUS=completed party={party} "
        f"local={local_address} peer={peer_address} protocol=BFIA",
        flush=True,
    )


def build_parser(party: str) -> argparse.ArgumentParser:
    """Build a parser for tests and tooling without parsing process arguments."""

    parser = argparse.ArgumentParser(
        description=f"Run the {party.capitalize()} party of BFIA SGB."
    )
    add_common_arguments(parser)
    return parser


def build_config_and_dataset(
    party: str, argv: list[str] | None = None
) -> Tuple[Dict[str, Any], Dict[str, Any], str]:
    """Return the party config, dataset and log level for validation tools."""

    args = build_parser(party).parse_args(argv)
    config = _alice_config(args) if party == "alice" else _bob_config(args)
    return config, _dataset_for_party(party), args.log_level
