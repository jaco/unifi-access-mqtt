#!/bin/bash
# Upload firmware and filesystem to ESP32

set -e

# Parse arguments
CLEAN=false
ENV=""

for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=true
            ;;
        -*)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--clean] [environment]"
            echo "  --clean    Erase all data before upload (factory reset)"
            echo "  environment: esp32-poe (default), esp32-s3-zero, etc."
            exit 1
            ;;
        *)
            ENV="$arg"
            ;;
    esac
done

# Default environment
ENV="${ENV:-esp32-poe}"

echo "==================================="
echo "UniFi Doorbell - Upload Script"
echo "Environment: $ENV"
if [ "$CLEAN" = true ]; then
    echo "Mode: CLEAN (factory reset)"
else
    echo "Mode: Update (preserving NVS config)"
fi
echo "==================================="

# Check if config.h exists
if [ ! -f "include/config.h" ]; then
    echo ""
    echo "ERROR: include/config.h not found!"
    echo "Please copy include/config.h.example to include/config.h"
    echo ""
    exit 1
fi

# Clean mode: erase flash first
if [ "$CLEAN" = true ]; then
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
    echo "Step 1: Erasing flash..."
    pio run -t erase -e "$ENV"
fi

echo ""
echo "Building firmware..."
pio run -e "$ENV"

echo ""
echo "Building filesystem..."
pio run -t buildfs -e "$ENV"

echo ""
echo "Uploading filesystem (web app)..."
pio run -t uploadfs -e "$ENV"

echo ""
echo "Uploading firmware..."
pio run -t upload -e "$ENV"

echo ""
echo "==================================="
echo "Upload complete!"
echo "Access web UI at http://doorbell.local"
if [ "$CLEAN" = true ]; then
    echo ""
    echo "NOTE: Device was factory reset. Please complete setup wizard."
fi
echo "==================================="
echo ""
echo "Starting serial monitor (Ctrl+C to exit)..."
echo ""
pio device monitor -e "$ENV"
