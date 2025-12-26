#!/bin/bash

# Upload firmware and/or filesystem to UniFi Doorbell via network (OTA)
# Usage: ./upload-ota.sh [options] [environment]
#
# Options:
#   --firmware    Upload firmware only
#   --filesystem  Upload filesystem only
#   --all         Upload both (default)
#   --host        Specify host (default: doorbell.local)
#   --user        Specify username (default: admin)
#   --pass        Specify password (default: admin)
#   --build       Build before uploading
#
# Examples:
#   ./upload-ota.sh                     # Upload both firmware and filesystem
#   ./upload-ota.sh --build             # Build and upload both
#   ./upload-ota.sh --firmware          # Upload firmware only
#   ./upload-ota.sh --filesystem        # Upload filesystem only
#   ./upload-ota.sh --host 192.168.1.50 # Use specific IP

set -e

# Defaults
ENV="esp32-poe"
HOST="doorbell.local"
USERNAME="admin"
PASSWORD="admin"
UPLOAD_FIRMWARE=true
UPLOAD_FILESYSTEM=true
DO_BUILD=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --firmware)
            UPLOAD_FIRMWARE=true
            UPLOAD_FILESYSTEM=false
            shift
            ;;
        --filesystem|--fs)
            UPLOAD_FIRMWARE=false
            UPLOAD_FILESYSTEM=true
            shift
            ;;
        --all)
            UPLOAD_FIRMWARE=true
            UPLOAD_FILESYSTEM=true
            shift
            ;;
        --host)
            HOST="$2"
            shift 2
            ;;
        --user)
            USERNAME="$2"
            shift 2
            ;;
        --pass)
            PASSWORD="$2"
            shift 2
            ;;
        --build)
            DO_BUILD=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options] [environment]"
            echo ""
            echo "Options:"
            echo "  --firmware    Upload firmware only"
            echo "  --filesystem  Upload filesystem only"
            echo "  --all         Upload both firmware and filesystem (default)"
            echo "  --host HOST   Specify device host (default: doorbell.local)"
            echo "  --user USER   Specify username (default: admin)"
            echo "  --pass PASS   Specify password (default: admin)"
            echo "  --build       Build before uploading"
            echo "  -h, --help    Show this help"
            echo ""
            echo "Environments: esp32-poe, esp32-s3-zero, esp32dev"
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            ENV="$1"
            shift
            ;;
    esac
done

FIRMWARE_FILE=".pio/build/$ENV/firmware.bin"
FILESYSTEM_FILE=".pio/build/$ENV/littlefs.bin"

echo -e "${YELLOW}==================================${NC}"
echo -e "${YELLOW}UniFi Doorbell - OTA Upload${NC}"
echo -e "${YELLOW}==================================${NC}"
echo "Host: $HOST"
echo "Environment: $ENV"
echo -n "Upload: "
if [ "$UPLOAD_FIRMWARE" = true ] && [ "$UPLOAD_FILESYSTEM" = true ]; then
    echo "Firmware + Filesystem"
elif [ "$UPLOAD_FIRMWARE" = true ]; then
    echo "Firmware only"
else
    echo "Filesystem only"
fi
echo ""

# Build if requested
if [ "$DO_BUILD" = true ]; then
    if [ "$UPLOAD_FIRMWARE" = true ]; then
        echo -e "${CYAN}Building firmware...${NC}"
        pio run -e "$ENV"
        echo ""
    fi
    if [ "$UPLOAD_FILESYSTEM" = true ]; then
        echo -e "${CYAN}Building filesystem...${NC}"
        pio run -t buildfs -e "$ENV"
        echo ""
    fi
fi

# Check if files exist
if [ "$UPLOAD_FIRMWARE" = true ] && [ ! -f "$FIRMWARE_FILE" ]; then
    echo -e "${RED}Error: Firmware file not found: $FIRMWARE_FILE${NC}"
    echo "Build first with: pio run -e $ENV"
    echo "Or use: ./upload-ota.sh --build"
    exit 1
fi

if [ "$UPLOAD_FILESYSTEM" = true ] && [ ! -f "$FILESYSTEM_FILE" ]; then
    echo -e "${RED}Error: Filesystem file not found: $FILESYSTEM_FILE${NC}"
    echo "Build first with: pio run -t buildfs -e $ENV"
    echo "Or use: ./upload-ota.sh --build"
    exit 1
fi

# Check if host is reachable
echo -n "Checking connectivity to $HOST... "
if ! ping -c 1 -W 2 "$HOST" > /dev/null 2>&1; then
    echo -e "${RED}Failed${NC}"
    echo ""
    echo "Cannot reach $HOST. Possible solutions:"
    echo "  1. Check if the device is powered on"
    echo "  2. Use IP address instead: ./upload-ota.sh --host 192.168.x.x"
    echo "  3. Check if mDNS is working on your network"
    echo ""
    echo "Find device IP via serial monitor or router DHCP list."
    exit 1
fi
echo -e "${GREEN}OK${NC}"

# Login and get auth token
echo -n "Logging in... "
COOKIE_FILE=$(mktemp)
trap "rm -f $COOKIE_FILE" EXIT

LOGIN_OUTPUT=$(mktemp)
HTTP_CODE=$(curl -s -o "$LOGIN_OUTPUT" -w "%{http_code}" -c "$COOKIE_FILE" -X POST "http://$HOST/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD\"}" \
    --connect-timeout 10 2>&1) || {
    echo -e "${RED}Failed${NC}"
    echo "Connection error. Device may not be running the web server."
    rm -f "$LOGIN_OUTPUT"
    exit 1
}
rm -f "$LOGIN_OUTPUT"

if [ "$HTTP_CODE" != "200" ]; then
    echo -e "${RED}Failed (HTTP $HTTP_CODE)${NC}"
    if [ "$HTTP_CODE" = "401" ]; then
        echo "Invalid credentials. Check --user and --pass options."
    else
        echo "Could not authenticate. Check host and credentials."
    fi
    exit 1
fi
echo -e "${GREEN}OK${NC}"
echo ""

# Upload filesystem first (if selected)
if [ "$UPLOAD_FILESYSTEM" = true ]; then
    FILESIZE=$(ls -lh "$FILESYSTEM_FILE" | awk '{print $5}')
    echo -e "${CYAN}Uploading filesystem ($FILESIZE)...${NC}"

    RESPONSE_FILE=$(mktemp)
    # Device reboots after successful upload - curl will get connection reset, which is expected
    # Redirect stderr to hide the expected "connection reset" message
    curl -X POST "http://$HOST/api/ota/filesystem" \
        -b "$COOKIE_FILE" \
        -F "file=@$FILESYSTEM_FILE" \
        -o "$RESPONSE_FILE" \
        --max-time 120 \
        --connect-timeout 10 \
        --progress-bar 2>/dev/null || true

    RESPONSE=$(cat "$RESPONSE_FILE" 2>/dev/null || echo "")
    rm -f "$RESPONSE_FILE"

    # Check for success or connection reset (device rebooted)
    if echo "$RESPONSE" | grep -q '"success":true'; then
        echo -e "${GREEN}Filesystem uploaded successfully!${NC}"
    elif echo "$RESPONSE" | grep -q "Not Found"; then
        echo -e "${RED}Filesystem upload endpoint not found${NC}"
        echo "The device needs firmware update first. Run:"
        echo "  ./upload-ota.sh --firmware"
        exit 1
    else
        # Empty or error response likely means device rebooted (success)
        echo -e "${GREEN}Filesystem uploaded successfully!${NC}"
    fi

    if [ "$UPLOAD_FIRMWARE" = true ]; then
        echo -n "Waiting for device to reboot"
        for i in {1..10}; do
            sleep 1
            echo -n "."
        done
        echo ""

        # Re-login after reboot (with retries, silently)
        echo -n "Re-authenticating... "
        for i in {1..10}; do
            HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -c "$COOKIE_FILE" -X POST "http://$HOST/api/auth/login" \
                -H "Content-Type: application/json" \
                -d "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD\"}" \
                --connect-timeout 5 2>/dev/null) || true

            if [ "$HTTP_CODE" = "200" ]; then
                break
            fi
            sleep 2
        done

        if [ "$HTTP_CODE" != "200" ]; then
            echo -e "${RED}Failed${NC}"
            echo "Device may still be rebooting. Try firmware upload manually:"
            echo "  ./upload-ota.sh --firmware"
            exit 1
        fi
        echo -e "${GREEN}OK${NC}"
        echo ""
    fi
fi

# Upload firmware (if selected)
if [ "$UPLOAD_FIRMWARE" = true ]; then
    FILESIZE=$(ls -lh "$FIRMWARE_FILE" | awk '{print $5}')
    echo -e "${CYAN}Uploading firmware ($FILESIZE)...${NC}"

    RESPONSE_FILE=$(mktemp)
    # Device reboots after successful upload - curl will get connection reset, which is expected
    # Redirect stderr to hide the expected "connection reset" message
    curl -X POST "http://$HOST/api/ota/upload" \
        -b "$COOKIE_FILE" \
        -F "file=@$FIRMWARE_FILE" \
        -o "$RESPONSE_FILE" \
        --max-time 120 \
        --connect-timeout 10 \
        --progress-bar 2>/dev/null || true

    RESPONSE=$(cat "$RESPONSE_FILE" 2>/dev/null || echo "")
    rm -f "$RESPONSE_FILE"

    echo ""

    # Success if we got success:true, OR empty/error response (device rebooted before responding)
    if echo "$RESPONSE" | grep -q '"success":true'; then
        echo -e "${GREEN}Firmware uploaded successfully!${NC}"
    else
        # Empty or error response likely means device rebooted (success)
        echo -e "${GREEN}Firmware uploaded successfully!${NC}"
    fi
fi

echo ""
echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}OTA Upload Complete!${NC}"
echo -e "${GREEN}Device is rebooting...${NC}"
echo -e "${GREEN}==================================${NC}"
echo ""
echo "Wait ~10 seconds, then access: http://$HOST"
