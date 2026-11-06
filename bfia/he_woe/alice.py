"""Alice entrypoint for the BFIA HE-WOE example."""

try:
    from .common import run_party
except ImportError:
    from common import run_party


if __name__ == "__main__":
    run_party("alice")
