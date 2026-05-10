#!/usr/bin/env bash
# Shared helpers for the UniFi Access developer-API test scripts.
# Source this file from the individual scripts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Auto-load .env in the same directory if present.
if [[ -f "${SCRIPT_DIR}/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "${SCRIPT_DIR}/.env"
    set +a
fi

# Colors (only when stdout is a TTY).
if [[ -t 1 ]]; then
    C_RED=$'\033[0;31m'; C_GREEN=$'\033[0;32m'; C_YELLOW=$'\033[1;33m'
    C_BLUE=$'\033[0;34m'; C_DIM=$'\033[2m'; C_RESET=$'\033[0m'
else
    C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''; C_DIM=''; C_RESET=''
fi

log_info()    { echo "${C_BLUE}[INFO]${C_RESET} $*" >&2; }
log_ok()      { echo "${C_GREEN}[OK]${C_RESET} $*" >&2; }
log_warn()    { echo "${C_YELLOW}[WARN]${C_RESET} $*" >&2; }
log_err()     { echo "${C_RED}[ERROR]${C_RESET} $*" >&2; }
log_debug()   { [[ "${DEBUG:-}" == "true" ]] && echo "${C_DIM}[DEBUG] $*${C_RESET}" >&2 || true; }

require_env() {
    local missing=0
    for var in "$@"; do
        if [[ -z "${!var:-}" || "${!var:-}" == "replace-me" ]]; then
            log_err "Missing required env var: ${var}"
            missing=1
        fi
    done
    if [[ $missing -eq 1 ]]; then
        log_err "Set them in ${SCRIPT_DIR}/.env (see .env.example) or export them in your shell."
        exit 2
    fi
}

require_cmd() {
    for cmd in "$@"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            log_err "Required command not found: $cmd"
            exit 2
        fi
    done
}

# Build the API base URL from UNIFI_HOST + UNIFI_PORT.
api_base() {
    local port="${UNIFI_PORT:-12445}"
    echo "https://${UNIFI_HOST}:${port}"
}

# curl wrapper:
#   - --insecure (UniFi Console uses a self-signed cert)
#   - sends the Bearer token
#   - splits HTTP status (last line) from body
#   - prints the body, returns nonzero if status >= 400
#
# Usage: api_curl <method> <path> [json_body]
api_curl() {
    local method="$1"; shift
    local path="$1"; shift
    local body="${1:-}"

    local url; url="$(api_base)${path}"
    log_debug "$method $url"
    [[ -n "$body" ]] && log_debug "body: $body"

    local args=(
        --silent
        --show-error
        --insecure
        --connect-timeout 5
        --max-time 15
        --request "$method"
        --header "Authorization: Bearer ${UNIFI_API_TOKEN}"
        --header "Accept: application/json"
        --write-out '\n%{http_code}'
    )
    if [[ -n "$body" ]]; then
        args+=(
            --header "Content-Type: application/json"
            --data "$body"
        )
    fi

    local response
    if ! response="$(curl "${args[@]}" "$url" 2>&1)"; then
        log_err "curl failed: $response"
        return 1
    fi

    local http_code body_out
    http_code="${response##*$'\n'}"
    body_out="${response%$'\n'*}"

    log_debug "http_code=$http_code"

    if command -v jq >/dev/null 2>&1 && [[ -n "$body_out" ]]; then
        echo "$body_out" | jq . 2>/dev/null || echo "$body_out"
    else
        echo "$body_out"
    fi

    if [[ "$http_code" -ge 400 ]]; then
        log_err "HTTP $http_code"
        return 1
    fi
    log_debug "HTTP $http_code"
    return 0
}
