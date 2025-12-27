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

# Check if host is reachable and resolve IP
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

# Resolve and cache IP address to avoid mDNS issues after reboot
RESOLVED_IP=$(ping -c 1 "$HOST" 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [ -n "$RESOLVED_IP" ]; then
    echo -e "${GREEN}OK${NC} ($RESOLVED_IP)"
    # Use IP for all subsequent requests to avoid mDNS issues
    HOST="$RESOLVED_IP"
else
    echo -e "${GREEN}OK${NC}"
fi

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
    PROGRESS_FILE=$(mktemp)

    # Start upload in background - progress to file, show it with tail
    curl -X POST "http://$HOST/api/ota/filesystem" \
        -b "$COOKIE_FILE" \
        -F "file=@$FILESYSTEM_FILE" \
        -o "$RESPONSE_FILE" \
        -w "\n%{http_code}" \
        --connect-timeout 10 \
        -# \
        2>"$PROGRESS_FILE" &
    CURL_PID=$!

    # Disable exit-on-error for curl handling (curl may be killed/timeout)
    set +e

    # Show progress and detect 100% completion
    UPLOAD_COMPLETE=false
    while kill -0 $CURL_PID 2>/dev/null; do
        # Show current progress
        PROGRESS=$(tail -c 100 "$PROGRESS_FILE" 2>/dev/null || echo "")
        printf "\r%s" "$PROGRESS"

        # Check if upload reached 100%
        if echo "$PROGRESS" | grep -q "100"; then
            if [ "$UPLOAD_COMPLETE" = false ]; then
                UPLOAD_COMPLETE=true
                echo ""
                echo -n "Upload complete, waiting for response..."
                # Wait 3 more seconds for response, then kill curl
                sleep 3
                kill $CURL_PID 2>/dev/null
                wait $CURL_PID 2>/dev/null
                break
            fi
        fi
        sleep 0.2
    done
    wait $CURL_PID 2>/dev/null

    # Re-enable exit-on-error
    set -e

    echo ""
    RESPONSE=$(cat "$RESPONSE_FILE" 2>/dev/null || echo "")
    rm -f "$RESPONSE_FILE" "$PROGRESS_FILE"

    # Check response - extract HTTP code from end of response (added by -w)
    if echo "$RESPONSE" | grep -q '"success":true'; then
        echo -e "${GREEN}Filesystem uploaded successfully!${NC}"
    elif echo "$RESPONSE" | grep -q "Not Found"; then
        echo -e "${RED}Filesystem upload endpoint not found${NC}"
        echo "The device needs firmware update first. Run:"
        echo "  ./upload-ota.sh --firmware"
        exit 1
    elif echo "$RESPONSE" | grep -q "200"; then
        echo -e "${GREEN}Filesystem uploaded successfully!${NC}"
    elif [ "$UPLOAD_COMPLETE" = true ]; then
        # Upload completed but no response = device rebooted (expected with old firmware)
        echo -e "${GREEN}Filesystem uploaded (device rebooted)${NC}"
    else
        echo -e "${RED}Filesystem upload may have failed${NC}"
        echo "Response: $RESPONSE"
    fi

    if [ "$UPLOAD_FIRMWARE" = true ]; then
        echo -n "Waiting for device to reboot"
        for i in {1..5}; do
            sleep 1
            echo -n "."
        done
        echo ""

        # Re-login after reboot (with retries)
        echo -n "Re-authenticating "
        HTTP_CODE=""
        CURL_ERR=""
        for i in {1..20}; do
            CURL_ERR=$(mktemp)
            HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -c "$COOKIE_FILE" -X POST "http://$HOST/api/auth/login" \
                -H "Content-Type: application/json" \
                -d "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD\"}" \
                --connect-timeout 2 \
                --max-time 3 2>"$CURL_ERR") || true

            ERR_MSG=$(cat "$CURL_ERR" 2>/dev/null | head -1)
            rm -f "$CURL_ERR"

            if [ "$HTTP_CODE" = "200" ]; then
                break
            fi
            # Show what we're getting for debugging
            if [ -n "$ERR_MSG" ]; then
                echo ""
                echo "  curl error: $ERR_MSG"
                echo -n "  Retrying "
            else
                echo -n "[$HTTP_CODE]"
            fi
            sleep 1
        done

        if [ "$HTTP_CODE" != "200" ]; then
            echo -e " ${RED}Failed (last: $HTTP_CODE)${NC}"
            echo "Device may still be rebooting. Try firmware upload manually:"
            echo "  ./upload-ota.sh --firmware"
            exit 1
        fi
        echo -e " ${GREEN}OK${NC}"
        echo ""
    fi
fi

# Upload firmware (if selected)
if [ "$UPLOAD_FIRMWARE" = true ]; then
    FILESIZE=$(ls -lh "$FIRMWARE_FILE" | awk '{print $5}')
    echo -e "${CYAN}Uploading firmware ($FILESIZE)...${NC}"

    RESPONSE_FILE=$(mktemp)
    PROGRESS_FILE=$(mktemp)

    # Start upload in background - progress to file, show it with tail
    curl -X POST "http://$HOST/api/ota/upload" \
        -b "$COOKIE_FILE" \
        -F "file=@$FIRMWARE_FILE" \
        -o "$RESPONSE_FILE" \
        -w "\n%{http_code}" \
        --connect-timeout 10 \
        -# \
        2>"$PROGRESS_FILE" &
    CURL_PID=$!

    # Disable exit-on-error for curl handling (curl may be killed/timeout)
    set +e

    # Show progress and detect 100% completion
    UPLOAD_COMPLETE=false
    while kill -0 $CURL_PID 2>/dev/null; do
        # Show current progress
        PROGRESS=$(tail -c 100 "$PROGRESS_FILE" 2>/dev/null || echo "")
        printf "\r%s" "$PROGRESS"

        # Check if upload reached 100%
        if echo "$PROGRESS" | grep -q "100"; then
            if [ "$UPLOAD_COMPLETE" = false ]; then
                UPLOAD_COMPLETE=true
                echo ""
                echo -n "Upload complete, waiting for response..."
                # Wait 3 more seconds for response, then kill curl
                sleep 3
                kill $CURL_PID 2>/dev/null
                wait $CURL_PID 2>/dev/null
                break
            fi
        fi
        sleep 0.2
    done
    wait $CURL_PID 2>/dev/null

    # Re-enable exit-on-error
    set -e

    echo ""
    RESPONSE=$(cat "$RESPONSE_FILE" 2>/dev/null || echo "")
    rm -f "$RESPONSE_FILE" "$PROGRESS_FILE"

    # Check response
    if echo "$RESPONSE" | grep -q '"success":true'; then
        echo -e "${GREEN}Firmware uploaded successfully!${NC}"
    elif echo "$RESPONSE" | grep -q "200"; then
        echo -e "${GREEN}Firmware uploaded successfully!${NC}"
    elif [ "$UPLOAD_COMPLETE" = true ]; then
        # Upload completed but no response = device rebooted (expected with old firmware)
        echo -e "${GREEN}Firmware uploaded (device rebooted)${NC}"
    else
        echo -e "${RED}Firmware upload may have failed${NC}"
        echo "Response: $RESPONSE"
    fi
fi

echo ""
echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}OTA Upload Complete!${NC}"
echo -e "${GREEN}Device is rebooting...${NC}"
echo -e "${GREEN}==================================${NC}"
echo ""
echo "Wait ~10 seconds, then access: http://$HOST"
