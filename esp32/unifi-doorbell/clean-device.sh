#!/bin/bash
# Erase ESP32 flash to reset device to factory state
# This clears all configuration and allows testing the onboarding process

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Use PlatformIO from venv if available, otherwise global (for CI)
if [ -x "$SCRIPT_DIR/.venv/bin/pio" ]; then
  PIO="$SCRIPT_DIR/.venv/bin/pio"
elif command -v pio &> /dev/null; then
  PIO="pio"
else
  echo "Error: PlatformIO not found. Run 'make setup-venv' first."
  exit 1
fi

# Default environment
ENV="${1:-esp32-poe}"

echo "==================================="
echo "UniFi Doorbell - Erase Device"
echo "Environment: $ENV"
echo "==================================="
echo ""
echo "WARNING: This will erase ALL data on the device including:"
echo "  - WiFi credentials"
echo "  - UniFi configuration"
echo "  - Saved certificates"
echo "  - GPIO settings"
echo ""
read -p "Are you sure? (y/N) " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

echo ""
echo "Erasing flash..."
"$PIO" run -t erase -e "$ENV"

echo ""
echo "==================================="
echo "Device erased!"
echo ""
echo "Next steps:"
echo "  1. Run ./upload.sh $ENV to flash firmware"
echo "  2. Device will start in AP mode (WiFi) or use config.h defaults (Ethernet)"
echo "==================================="
