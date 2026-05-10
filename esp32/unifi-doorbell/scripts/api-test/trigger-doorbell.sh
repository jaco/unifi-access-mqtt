#!/usr/bin/env bash
#
# Trigger (or cancel) a doorbell ring on a UniFi Access reader via the
# official developer API.
#
#   POST /api/v1/developer/devices/{device_id}/doorbell
#   body: { "room_name": "...", "cancel": true|false }   (both fields optional)
#
# Requires UniFi Access 4.0.10 or later. Permission key: edit:device.
#
# Usage:
#   ./trigger-doorbell.sh <device_id>
#   ./trigger-doorbell.sh <device_id> --room "Front Door"
#   ./trigger-doorbell.sh <device_id> --cancel               # abort in-flight ring
#   ./trigger-doorbell.sh <device_id> --room "Front" --cancel
#   DEBUG=true ./trigger-doorbell.sh <device_id>
#
# Tip: run ./list-devices.sh --readers-only to find the device_id.

# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

DEVICE_ID=""
ROOM_NAME=""
CANCEL=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --room)
            [[ -z "${2:-}" ]] && { log_err "--room requires a value"; exit 2; }
            ROOM_NAME="$2"; shift 2 ;;
        --cancel)        CANCEL="true";  shift ;;
        --no-cancel)     CANCEL="false"; shift ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --) shift; break ;;
        -*)
            log_err "Unknown argument: $1"
            exit 2
            ;;
        *)
            if [[ -z "$DEVICE_ID" ]]; then
                DEVICE_ID="$1"
            else
                log_err "Unexpected positional argument: $1"
                exit 2
            fi
            shift
            ;;
    esac
done

if [[ -z "$DEVICE_ID" ]]; then
    log_err "Missing device_id. Run: ./list-devices.sh --readers-only"
    exit 2
fi

require_env UNIFI_HOST UNIFI_API_TOKEN
require_cmd curl

# Build the JSON body. Empty {} is valid; the API treats both fields as optional.
BODY="{}"
if [[ -n "$ROOM_NAME" || -n "$CANCEL" ]]; then
    if command -v jq >/dev/null 2>&1; then
        BODY="$(jq -n \
            --arg room "$ROOM_NAME" \
            --arg cancel "$CANCEL" \
            '{} + (if $room   != "" then {room_name: $room}                  else {} end)
                + (if $cancel != "" then {cancel: ($cancel == "true")}       else {} end)')"
    else
        # Fallback hand-rolled JSON. ROOM_NAME must not contain double quotes.
        if [[ "$ROOM_NAME" == *\"* ]]; then
            log_err "Install jq to use --room values containing double quotes."
            exit 2
        fi
        parts=()
        [[ -n "$ROOM_NAME" ]] && parts+=("\"room_name\":\"${ROOM_NAME}\"")
        [[ -n "$CANCEL"    ]] && parts+=("\"cancel\":${CANCEL}")
        BODY="{$(IFS=,; echo "${parts[*]}")}"
    fi
fi

if [[ "$CANCEL" == "true" ]]; then
    log_info "Cancelling doorbell on device $DEVICE_ID"
else
    log_info "Triggering doorbell on device $DEVICE_ID"
fi
log_debug "body: $BODY"

if api_curl POST "/api/v1/developer/devices/${DEVICE_ID}/doorbell" "$BODY"; then
    log_ok "Done."
else
    exit 1
fi
