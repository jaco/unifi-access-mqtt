#!/usr/bin/env bash
#
# List all devices known to UniFi Access via the official developer API.
#
#   GET /api/v1/developer/devices?refresh=<true|false>
#
# Usage:
#   ./list-devices.sh                # full list, refresh=true
#   ./list-devices.sh --no-refresh   # use cache (faster)
#   ./list-devices.sh --readers-only # filter to doorbell-capable readers
#   DEBUG=true ./list-devices.sh     # verbose logging
#
# Configure UNIFI_HOST / UNIFI_PORT / UNIFI_API_TOKEN via env or .env file.

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

REFRESH="true"
READERS_ONLY="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-refresh)   REFRESH="false" ;;
        --refresh)      REFRESH="true" ;;
        --readers-only) READERS_ONLY="true" ;;
        -h|--help)
            sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            log_err "Unknown argument: $1"
            exit 2
            ;;
    esac
    shift
done

require_env UNIFI_HOST UNIFI_API_TOKEN
require_cmd curl

log_info "Fetching devices from $(api_base) (refresh=$REFRESH)"

# Capture body so we can post-process even on error.
TMPFILE="$(mktemp)"
trap 'rm -f "$TMPFILE"' EXIT

if ! api_curl GET "/api/v1/developer/devices?refresh=${REFRESH}" >"$TMPFILE"; then
    cat "$TMPFILE"
    exit 1
fi

if [[ "$READERS_ONLY" == "true" ]]; then
    if ! command -v jq >/dev/null 2>&1; then
        log_err "--readers-only requires jq"
        exit 2
    fi
    log_info "Filtered to doorbell-capable readers (capabilities include 'remote_call'):"
    jq '
        [ .data[]?[]?
          | select((.capabilities // []) | index("remote_call"))
          | {id, name, alias, type}
        ]
    ' "$TMPFILE"
else
    cat "$TMPFILE"
fi

log_ok "Done."
