#!/bin/bash
#
# List UniFi Access devices from the controller
#
# Usage: ./list-devices.sh [--raw]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/production/config/config.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
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
    log_debug "Host: $UNIFI_HOST, Username: $UNIFI_USERNAME"
    return 0
}

# Show usage
show_usage() {
    echo "Usage: $0 [--raw] [--config <path>]"
    echo ""
    echo "List all UniFi Access devices from the controller."
    echo ""
    echo "Options:"
    echo "  --raw             Output raw JSON instead of formatted table"
    echo "  --config <path>   Path to config.json (default: ./production/config/config.json)"
    echo "  --help            Show this help"
    echo ""
    echo "Environment variables (override config file):"
    echo "  UNIFI_HOST        Controller URL (e.g., https://10.1.0.1)"
    echo "  UNIFI_USERNAME    Controller username"
    echo "  UNIFI_PASSWORD    Controller password"
    echo "  DEBUG=true        Enable debug output"
    echo ""
    echo "Example:"
    echo "  $0"
    echo "  $0 --raw"
    echo "  $0 --config /path/to/config.json"
}

# Parse arguments
RAW_OUTPUT=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --raw)
            RAW_OUTPUT=true
            shift
            ;;
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --help|-h)
            show_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Load config (environment variables override config file)
load_config || exit 1

# Allow environment variables to override config
UNIFI_HOST="${UNIFI_HOST_OVERRIDE:-$UNIFI_HOST}"
UNIFI_USERNAME="${UNIFI_USERNAME_OVERRIDE:-$UNIFI_USERNAME}"
UNIFI_PASSWORD="${UNIFI_PASSWORD_OVERRIDE:-$UNIFI_PASSWORD}"

BASE_URL="$UNIFI_HOST"
COOKIE_FILE="${HOME}/.unifi-access-cookie"
CSRF_TOKEN=""

# Clear old cookies for fresh authentication
clear_cookies() {
    if [[ -f "$COOKIE_FILE" ]]; then
        rm -f "$COOKIE_FILE"
        log_debug "Cleared old cookies"
    fi
}

# Get initial CSRF token
acquire_csrf_token() {
    log_info "Acquiring CSRF token..."

    # Match Go: request base URL without trailing slash
    RESPONSE=$(curl -sk -c "$COOKIE_FILE" -b "$COOKIE_FILE" \
        -D - \
        "${BASE_URL}" \
        -o /dev/null)

    # Extract CSRF token from headers
    CSRF_TOKEN=$(echo "$RESPONSE" | grep -i "x-csrf-token" | tail -1 | cut -d: -f2 | tr -d ' \r\n')

    if [[ -n "$CSRF_TOKEN" ]]; then
        log_debug "Got CSRF token: ${CSRF_TOKEN:0:20}..."
        return 0
    else
        log_debug "No CSRF token in response"
        return 1
    fi
}

# Authenticate with username/password
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

    # Login request
    RESPONSE=$(curl -sk -c "$COOKIE_FILE" -b "$COOKIE_FILE" \
        -D - \
        -X POST "${BASE_URL}/api/auth/login" \
        "${headers[@]}" \
        -d "{\"username\": \"${username}\", \"password\": \"${password}\", \"token\": \"\", \"rememberMe\": true}" \
        -w "\n%{http_code}")

    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)

    # Extract updated CSRF token from response headers
    NEW_TOKEN=$(echo "$RESPONSE" | grep -i "x-updated-csrf-token\|x-csrf-token" | tail -1 | cut -d: -f2 | tr -d ' \r\n')
    if [[ -n "$NEW_TOKEN" ]]; then
        CSRF_TOKEN="$NEW_TOKEN"
        log_debug "Updated CSRF token: ${CSRF_TOKEN:0:20}..."
    fi

    if [[ "$HTTP_CODE" == "200" ]]; then
        log_success "Authentication successful"
        return 0
    else
        log_error "Authentication failed (HTTP $HTTP_CODE)"
        echo "$RESPONSE" | head -20
        return 1
    fi
}

# Fetch devices using topology endpoint (same as the Go app)
fetch_devices() {
    log_info "Fetching device topology..."

    # Build headers
    local headers=()
    if [[ -n "$CSRF_TOKEN" ]]; then
        headers+=(-H "X-Csrf-Token: ${CSRF_TOKEN}")
    fi

    RESPONSE=$(curl -sk -b "$COOKIE_FILE" -c "$COOKIE_FILE" \
        "${headers[@]}" \
        "${BASE_URL}/proxy/access/api/v2/devices/topology4" \
        -w "\n%{http_code}")

    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    RESPONSE_BODY=$(echo "$RESPONSE" | sed '$d')

    # Update CSRF token if present
    # (would need to capture headers for this, skipping for simplicity)

    if [[ "$HTTP_CODE" == "200" ]] || [[ "$HTTP_CODE" == "201" ]]; then
        log_success "Fetched topology successfully"
        return 0
    elif [[ "$HTTP_CODE" == "401" ]] || [[ "$HTTP_CODE" == "403" ]]; then
        log_error "Authentication required or expired (HTTP $HTTP_CODE)"
        return 1
    else
        log_error "Request failed (HTTP $HTTP_CODE)"
        log_debug "Response: $RESPONSE_BODY"
        return 1
    fi
}

format_output() {
    if [[ "$RAW_OUTPUT" == "true" ]]; then
        echo "$RESPONSE_BODY" | jq .
        return
    fi

    # Check if jq is available
    if ! command -v jq &> /dev/null; then
        log_info "Install jq for formatted output. Showing raw JSON:"
        echo "$RESPONSE_BODY"
        return
    fi

    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}                           UniFi Access Devices${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════════════════${NC}"
    echo ""

    # Parse topology structure: data[] -> floors[] -> doors[] -> device_groups[][]
    echo "$RESPONSE_BODY" | jq -r '
        [.data[]? |
            .floors[]? |
            .doors[]? |
            {
                door_name: .name,
                door_id: .unique_id,
                door_position: .door_position_status,
                door_lock: .door_lock_relay_status,
                devices: [.device_groups[]?[]?]
            }
        ] | .[] |
        "Door: \(.door_name // "unknown")\n" +
        "  Door ID:     \(.door_id // "n/a")\n" +
        "  Position:    \(.door_position // "n/a")\n" +
        "  Lock Status: \(.door_lock // "n/a")\n" +
        "  Devices:\n" +
        (.devices | map(
            "    - \(.device_type // .type // "unknown"): \(.name // .alias // "unnamed")\n" +
            "      ID:       \(.unique_id // .id // "n/a")\n" +
            "      MAC:      \(.mac // "n/a")\n" +
            "      IP:       \(.ip // "n/a")\n" +
            "      Firmware: \(.firmware // .fw_version // "n/a")\n" +
            "      Adopted:  \(.adopted // "n/a")\n" +
            "      Online:   \(.is_online // .online // "n/a")"
        ) | join("\n")) +
        "\n───────────────────────────────────────────────────────────────────────────────"
    ' 2>/dev/null || {
        # Fallback: show raw structure
        log_info "Could not parse topology, showing raw:"
        echo "$RESPONSE_BODY" | jq '.data[0].floors[0].doors[0] | keys' 2>/dev/null || echo "$RESPONSE_BODY" | jq .
    }

    echo ""

    # Summary - count devices
    DEVICE_COUNT=$(echo "$RESPONSE_BODY" | jq '[.data[]?.floors[]?.doors[]?.device_groups[]?[]?] | length' 2>/dev/null || echo "?")
    DOOR_COUNT=$(echo "$RESPONSE_BODY" | jq '[.data[]?.floors[]?.doors[]?] | length' 2>/dev/null || echo "?")
    log_success "Found ${DEVICE_COUNT} device(s) across ${DOOR_COUNT} door(s)"

    # Show device IDs for easy copying
    echo ""
    echo -e "${YELLOW}Device IDs for scripts:${NC}"
    echo "$RESPONSE_BODY" | jq -r '
        .data[]?.floors[]?.doors[]? |
        .device_groups[]?[]? |
        "  \(.device_type // .type // "?"): \(.unique_id // .id // "n/a") (\(.name // .alias // "unnamed"))"
    ' 2>/dev/null || true

    # Show door IDs
    echo ""
    echo -e "${YELLOW}Door IDs:${NC}"
    echo "$RESPONSE_BODY" | jq -r '
        .data[]?.floors[]?.doors[]? |
        "  \(.unique_id // "n/a"): \(.name // "unnamed")"
    ' 2>/dev/null || true
}

# Main flow

log_info "Connecting to $BASE_URL"

# Clear old cookies to ensure fresh authentication (like Go does with fresh cookie jar)
clear_cookies

# Authenticate and fetch
if authenticate; then
    if fetch_devices; then
        format_output
        exit 0
    fi
fi

log_error "Failed to fetch devices"
exit 1
