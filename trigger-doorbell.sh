#!/bin/bash
#
# Trigger a doorbell ring with video via the UniFi Access Controller REST API
# This is the proper way to trigger a doorbell - the controller handles Agora credentials
#
# Usage: ./trigger-doorbell.sh <reader-device-id> [viewer-device-id]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/production/config/config.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_debug() { [[ "${DEBUG:-}" == "true" ]] && echo -e "${YELLOW}[DEBUG]${NC} $1"; }

# Load configuration from config.json
load_config() {
    if [[ ! -f "$CONFIG_FILE" ]]; then
        log_error "Config file not found: $CONFIG_FILE"
        return 1
    fi

    if ! command -v jq &> /dev/null; then
        log_error "jq is required to parse config file"
        return 1
    fi

    UNIFI_HOST=$(jq -r '.unifi.host // empty' "$CONFIG_FILE")
    UNIFI_USERNAME=$(jq -r '.unifi.username // empty' "$CONFIG_FILE")
    UNIFI_PASSWORD=$(jq -r '.unifi.password // empty' "$CONFIG_FILE")

    if [[ -z "$UNIFI_HOST" ]]; then
        log_error "unifi.host not found in config"
        return 1
    fi

    log_info "Loaded config from $CONFIG_FILE"
    return 0
}

# Show usage
show_usage() {
    echo "Usage: $0 <reader-device-id> [viewer-device-id] [--config <path>]"
    echo ""
    echo "Trigger a doorbell ring with video via the UniFi Access Controller REST API."
    echo "This properly coordinates video streaming between the reader and viewer."
    echo ""
    echo "Arguments:"
    echo "  reader-device-id  Device ID of the reader/doorbell (source of video)"
    echo "  viewer-device-id  Optional: specific viewer to ring (omit to ring all)"
    echo ""
    echo "Options:"
    echo "  --config <path>   Path to config.json (default: ./production/config/config.json)"
    echo "  --help            Show this help"
    echo ""
    echo "Config file format (production/config/config.json):"
    echo "  {"
    echo "    \"unifi\": {"
    echo "      \"host\": \"https://10.1.0.1\","
    echo "      \"username\": \"admin\","
    echo "      \"password\": \"your-password\""
    echo "    }"
    echo "  }"
    echo ""
    echo "Example:"
    echo "  $0 a1b2c3d4e5f6"
    echo "  $0 a1b2c3d4e5f6 9c05d6d3447f"
    echo ""
    echo "To find device IDs, run: ./list-devices.sh"
}

# Parse arguments
READER_ID=""
VIEWER_ID=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --help|-h)
            show_usage
            exit 0
            ;;
        --*)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
        *)
            if [[ -z "$READER_ID" ]]; then
                READER_ID="$1"
            elif [[ -z "$VIEWER_ID" ]]; then
                VIEWER_ID="$1"
            fi
            shift
            ;;
    esac
done

if [[ -z "$READER_ID" ]]; then
    log_error "reader-device-id is required"
    echo ""
    show_usage
    exit 1
fi

# Load config
load_config || exit 1

BASE_URL="$UNIFI_HOST"
API_URL="${BASE_URL}/proxy/access/api/v2"

# Generate unique IDs (matching Go implementation)
ROOM_ID="PR-$(cat /proc/sys/kernel/random/uuid 2>/dev/null || uuidgen | tr '[:upper:]' '[:lower:]')"
REQUEST_ID=$(cat /dev/urandom | LC_ALL=C tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)
NOW=$(date +%s)
COOKIE_FILE="${HOME}/.unifi-access-cookie"
CSRF_TOKEN=""

log_info "Triggering doorbell ring..."
log_info "  Controller: $BASE_URL"
log_info "  Reader:     $READER_ID"
log_info "  Room ID:    $ROOM_ID"
[[ -n "$VIEWER_ID" ]] && log_info "  Viewer:     $VIEWER_ID"

# Build viewer IDs array
if [[ -n "$VIEWER_ID" ]]; then
    NOTIFY_GUARDS="[\"${VIEWER_ID}\"]"
else
    NOTIFY_GUARDS="[]"
fi

# Build the request body (matching Go's DoorbellRequestBody format)
REQUEST_BODY=$(cat <<EOF
{
  "request_id": "${REQUEST_ID}",
  "agora_channel": "${ROOM_ID}",
  "controller_id": "${READER_ID}",
  "device_id": "${READER_ID}",
  "device_name": "API Trigger",
  "door_name": "",
  "floor_name": "",
  "in_or_out": "in",
  "mode": "webrtc",
  "create_time_uid": ${NOW},
  "create_time": ${NOW},
  "room_id": "${ROOM_ID}",
  "notify_door_guards": ${NOTIFY_GUARDS}
}
EOF
)

log_info "Request body:"
echo "$REQUEST_BODY" | jq . 2>/dev/null || echo "$REQUEST_BODY"

# Clear old cookies for fresh authentication
clear_cookies() {
    if [[ -f "$COOKIE_FILE" ]]; then
        rm -f "$COOKIE_FILE"
        log_debug "Cleared old cookies"
    fi
}

# Get initial CSRF token
acquire_csrf_token() {
    log_debug "Acquiring CSRF token..."

    # Match Go: request base URL without trailing slash
    RESPONSE=$(curl -sk -c "$COOKIE_FILE" -b "$COOKIE_FILE" \
        -D - \
        "${BASE_URL}" \
        -o /dev/null)

    CSRF_TOKEN=$(echo "$RESPONSE" | grep -i "x-csrf-token" | tail -1 | cut -d: -f2 | tr -d ' \r\n')

    if [[ -n "$CSRF_TOKEN" ]]; then
        log_debug "Got CSRF token: ${CSRF_TOKEN:0:20}..."
        return 0
    fi
    return 1
}

authenticate() {
    local username="$UNIFI_USERNAME"
    local password="$UNIFI_PASSWORD"

    if [[ -z "$password" ]]; then
        log_error "Password not found in config"
        return 1
    fi

    # First acquire CSRF token
    acquire_csrf_token || true

    log_info "Authenticating as ${username}..."

    # Build headers
    local headers=(-H "Content-Type: application/json")
    if [[ -n "$CSRF_TOKEN" ]]; then
        headers+=(-H "X-Csrf-Token: ${CSRF_TOKEN}")
    fi

    # UniFi Access uses the UniFi OS authentication
    AUTH_RESPONSE=$(curl -sk -c "$COOKIE_FILE" -b "$COOKIE_FILE" \
        -D - \
        -X POST "${BASE_URL}/api/auth/login" \
        "${headers[@]}" \
        -d "{\"username\": \"${username}\", \"password\": \"${password}\", \"token\": \"\", \"rememberMe\": true}" \
        -w "\n%{http_code}")

    HTTP_CODE=$(echo "$AUTH_RESPONSE" | tail -n1)

    # Extract updated CSRF token
    NEW_TOKEN=$(echo "$AUTH_RESPONSE" | grep -i "x-updated-csrf-token\|x-csrf-token" | tail -1 | cut -d: -f2 | tr -d ' \r\n')
    if [[ -n "$NEW_TOKEN" ]]; then
        CSRF_TOKEN="$NEW_TOKEN"
    fi

    if [[ "$HTTP_CODE" == "200" ]]; then
        log_success "Authentication successful"
        return 0
    else
        log_error "Authentication failed (HTTP $HTTP_CODE)"
        return 1
    fi
}

make_request() {
    log_info "Sending remote_call request to /device/${READER_ID}/remote_call..."

    # Build headers
    local headers=(-H "Content-Type: application/json")
    if [[ -n "$CSRF_TOKEN" ]]; then
        headers+=(-H "X-Csrf-Token: ${CSRF_TOKEN}")
    fi

    # Use the correct endpoint: /device/{device_id}/remote_call (matching Go implementation)
    RESPONSE=$(curl -sk -b "$COOKIE_FILE" -c "$COOKIE_FILE" \
        "${headers[@]}" \
        -X POST "${API_URL}/device/${READER_ID}/remote_call" \
        -d "$REQUEST_BODY" \
        -w "\n%{http_code}")

    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    RESPONSE_BODY=$(echo "$RESPONSE" | sed '$d')

    echo ""
    log_info "Response (HTTP $HTTP_CODE):"
    echo "$RESPONSE_BODY" | jq . 2>/dev/null || echo "$RESPONSE_BODY"

    if [[ "$HTTP_CODE" == "200" ]] || [[ "$HTTP_CODE" == "201" ]]; then
        log_success "Doorbell ring triggered successfully!"
        log_info "The viewer should now show the doorbell notification with video."
        return 0
    elif [[ "$HTTP_CODE" == "401" ]] || [[ "$HTTP_CODE" == "403" ]]; then
        log_error "Authentication required or expired"
        return 1
    else
        log_error "Request failed (HTTP $HTTP_CODE)"
        return 1
    fi
}

# Main flow
# Clear old cookies to ensure fresh authentication (like Go does with fresh cookie jar)
clear_cookies

# Authenticate and make request
if authenticate; then
    if make_request; then
        exit 0
    fi
fi

log_error "Failed to trigger doorbell ring"
exit 1
