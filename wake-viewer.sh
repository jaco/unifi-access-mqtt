#!/bin/bash

# Wake up a UniFi Access Viewer display via MQTT RPC
# This sends a /remote_view command which triggers the doorbell UI and wakes the screen

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERTS_DIR="${SCRIPT_DIR}/scripts/certs"
TRACE_BIN="${SCRIPT_DIR}/access-mqtt-trace/mqtt-trace"

# Default values from trace output
CONTROLLER_ID="${CONTROLLER_ID:-28704e48033b}"
VIEWER_ID="${VIEWER_ID:-9c05d6d3447f}"
BROKER="${UNIFI_BROKER:-$1}"

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

# Show usage if no broker specified
if [[ -z "$BROKER" ]]; then
    echo "Usage: $0 <broker-ip> [viewer-id] [controller-id]"
    echo ""
    echo "Wake up a UniFi Access Viewer display via MQTT"
    echo ""
    echo "Arguments:"
    echo "  broker-ip      Cloud Gateway IP address"
    echo "  viewer-id      Optional: Viewer device ID (default: $VIEWER_ID)"
    echo "  controller-id  Optional: Controller ID (default: $CONTROLLER_ID)"
    echo ""
    echo "Environment variables:"
    echo "  UNIFI_BROKER   Default broker IP"
    echo "  VIEWER_ID      Viewer device ID"
    echo "  CONTROLLER_ID  Controller ID"
    echo ""
    echo "Example:"
    echo "  $0 10.1.0.1"
    echo "  $0 10.1.0.1 9c05d6d3447f 28704e48033b"
    echo "  VIEWER_ID=abc123 $0 10.1.0.1"
    exit 1
fi

# Override defaults with positional arguments
if [[ -n "$2" ]]; then
    VIEWER_ID="$2"
fi
if [[ -n "$3" ]]; then
    CONTROLLER_ID="$3"
fi

echo "Waking up Viewer..."
echo "  Broker:     $BROKER"
echo "  Controller: $CONTROLLER_ID"
echo "  Viewer:     $VIEWER_ID"
echo ""

exec "$TRACE_BIN" \
    -broker "$BROKER" \
    -ca "${CERTS_DIR}/ca-cert.pem" \
    -cert "${CERTS_DIR}/mqtt-client-cert.pem" \
    -key "${CERTS_DIR}/mqtt-client-priv.pem" \
    -rpc "remote_view" \
    -controller "$CONTROLLER_ID" \
    -viewer "$VIEWER_ID" \
    -v
