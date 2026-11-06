"""Runtime BFIA event metadata. Never log message bodies or private values."""
import hashlib
import itertools
import json
import logging
import os
import time

_sequence = itertools.count(1)
_stage = "transport"

def stage(value):
    global _stage
    _stage = value

def event(name, **detail):
    detail.update(schema="bfia-audit-v1", seq=next(_sequence),
                  timestamp_ms=time.time_ns()//1000000, monotonic_ns=time.monotonic_ns(),
                  stage=_stage, event=name,
                  task_id=os.getenv("BFIA_RUN_ID", "unspecified")+"/"+os.getenv("BFIA_ALGORITHM", "unspecified"),
                  trace_id=os.getenv("BFIA_RUN_ID", "unspecified"),
                  algorithm=os.getenv("BFIA_ALGORITHM", "unspecified"),
                  party=os.getenv("BFIA_SELF_PARTY", detail.get("party", "unspecified")))
    logging.info("BFIA_EVENT %s", json.dumps(detail, ensure_ascii=False, sort_keys=True))

def message(name, source, destination, payload):
    event(name, source=source, destination=destination, bytes=len(payload),
          message_sha256=hashlib.sha256(payload).hexdigest())
