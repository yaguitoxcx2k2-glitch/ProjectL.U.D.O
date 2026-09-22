#!/usr/bin/env bash
set -euo pipefail
CONFIG="${LUDO_TEAM_CONFIG:-/etc/ludo-team-server.env}"
if [[ -f "$CONFIG" ]]; then
  # shellcheck disable=SC1090
  source "$CONFIG"
fi
DATA_DIR="${DATA_DIR:-/var/lib/ludo-team-server}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8787}"
SERVER_NAME="${SERVER_NAME:-LUDO Team Server}"
PUBLIC_URL="${PUBLIC_URL:-}"
ARGS=(--data "$DATA_DIR" --host "$HOST" --port "$PORT" --name "$SERVER_NAME")
if [[ -n "$PUBLIC_URL" ]]; then ARGS+=(--public-url "$PUBLIC_URL"); fi
if [[ "${TRUST_PROXY:-1}" == "1" ]]; then ARGS+=(--trust-proxy); fi
exec /usr/bin/python3 "$(dirname "$0")/ludo_server.py" "${ARGS[@]}"
