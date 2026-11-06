#!/bin/bash

# Minimal Kuscia entrypoint for BFIA's C++ ic_main.  Environment variables with
# dots are read with printenv because they are not valid shell identifiers.
# Bash preserves those names in the inherited environment; dash silently drops
# them when it starts, which makes ic_main lose config.self_role/node_id.
set -u

get_env() {
  printenv "$1" 2>/dev/null || true
}

algo="$(get_env 'runtime.component.parameter.algo')"
case "$algo" in
  ecdh_psi|psi|ecdh)
    disable_handshake=1
    ;;
  *)
    disable_handshake=0
    ;;
esac

self_role="$(get_env 'config.self_role')"
label_owner="$(get_env 'runtime.component.parameter.label_owner')"
has_label=false
if [ -n "$self_role" ] && [ -n "$label_owner" ] && [ "$self_role" = "$label_owner" ]; then
  has_label=true
fi

# UI-created ECDH graphs commonly send one shared column name (for example
# `id`) instead of the party-keyed JSON accepted by older ic_main builds.
# Normalize that legacy form at the process boundary so both old and newly
# rebuilt binaries can execute the same Kuscia task contract.
field_names="$(get_env 'runtime.component.parameter.field_names')"
if [ -n "$field_names" ] && [[ "$field_names" != \{* ]]; then
  field_names="{\"host.0\":\"$field_names\",\"guest.0\":\"$field_names\"}"
fi

log_file="$(mktemp /tmp/bfia-ic-main.XXXXXX)"
trap 'rm -f "$log_file"' EXIT

set +e
if [ -n "$field_names" ]; then
  # `env` is intentional: dotted keys cannot be shell identifiers, but env
  # can place the normalized value in ic_main's inherited environment.
  env "runtime.component.parameter.field_names=$field_names" \
    /app/ic_main "-disable_handshake=$disable_handshake" "-has_label=$has_label" >"$log_file" 2>&1
else
  /app/ic_main "-disable_handshake=$disable_handshake" "-has_label=$has_label" >"$log_file" 2>&1
fi
exit_code=$?
set -e
cat "$log_file"

# Preserve the legacy wrapper's application-level failure detection even when
# an older ic_main binary exits zero after logging a failed operation.
if grep -qE 'run failed:|run psi failed:' "$log_file"; then
  exit_code=2
fi

callback_url="$(get_env 'system.callback')"
task_id="$(get_env 'config.task_id')"
role="$(get_env 'config.self_role')"
if [ -n "$callback_url" ]; then
  if [ "$exit_code" -eq 0 ]; then
    status=SUCCESS
  else
    status=FAILED
  fi
  # Kuscia supplies UUID/role-like values here; keep the payload construction
  # dependency-free and never print the callback URL or other task metadata.
  payload=$(printf '{"task_id":"%s","status":"%s","role":"%s"}' \
    "$task_id" "$status" "$role")
  curl --silent --show-error --max-time 10 --insecure \
    -H 'Content-Type: application/json' \
    --data "$payload" "$callback_url" >/dev/null 2>&1 || true
fi

exit "$exit_code"
