#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERTS_DIR="${SCRIPT_DIR}/scripts/certs"
TRACE_BIN="${SCRIPT_DIR}/access-mqtt-trace/mqtt-trace"

# Check if binary exists
if [[ ! -x "$TRACE_BIN" ]]; then
    echo "Building mqtt-trace..."
    (cd "${SCRIPT_DIR}/access-mqtt-trace" && go build -o mqtt-trace .)
fi

# Check if certificates exist
if [[ ! -f "${CERTS_DIR}/ca-cert.pem" ]]; then
    echo "Error: Certificates not found in ${CERTS_DIR}"
    echo "Run scripts/copy-certs.sh first to copy certificates from the Cloud Gateway"
    exit 1
fi

# Default broker from environment or require as argument
BROKER="${UNIFI_BROKER:-$1}"

if [[ -z "$BROKER" ]]; then
    echo "Usage: $0 <broker-ip> [additional-args...]"
    echo ""
    echo "Example:"
    echo "  $0 10.1.0.1"
    echo "  $0 10.1.0.1 -v                    # Verbose mode"
    echo "  $0 10.1.0.1 -topic '/uctrl/+/device/+/rpc'"
    echo ""
    echo "Or set UNIFI_BROKER environment variable:"
    echo "  export UNIFI_BROKER=10.1.0.1"
    echo "  $0"
    exit 1
fi

# Shift broker argument if provided
if [[ "$1" == "$BROKER" ]]; then
    shift
fi

exec "$TRACE_BIN" \
    -broker "$BROKER" \
    -ca "${CERTS_DIR}/ca-cert.pem" \
    -cert "${CERTS_DIR}/mqtt-client-cert.pem" \
    -key "${CERTS_DIR}/mqtt-client-priv.pem" \
    "$@"
