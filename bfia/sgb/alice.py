"""Alice entrypoint for the two-party BFIA SGB example."""

try:
    from .common import run_party
except ImportError:  # Direct execution: python bfia/sgb/alice.py
    from common import run_party


if __name__ == "__main__":
    run_party("alice")
