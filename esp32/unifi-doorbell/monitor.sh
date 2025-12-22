#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BOARD="${1:-esp32-poe}"

case "$BOARD" in
  esp32-poe|poe)
    ENV="esp32-poe"
    ;;
  esp32-s3-zero|s3-zero|s3)
    ENV="esp32-s3-zero"
    ;;
  esp32-s3-wroom|wroom)
    ENV="esp32-s3-wroom"
    ;;
  *)
    echo "Usage: $0 <board>"
    echo ""
    echo "Available boards:"
    echo "  esp32-poe, poe        Olimex ESP32-POE (Ethernet)"
    echo "  esp32-s3-zero, s3     Waveshare ESP32-S3-Zero (WiFi, USB CDC)"
    echo "  esp32-s3-wroom, wroom ESP32-S3-WROOM-1 DevKit (WiFi, CH340)"
    echo ""
    exit 1
    ;;
esac

echo "Monitoring: $ENV"
pio device monitor -e "$ENV"
