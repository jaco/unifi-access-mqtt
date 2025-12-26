# UniFi Access Doorbell

[![mqtt-smarthome](https://img.shields.io/badge/mqtt-smarthome-blue.svg)](https://github.com/mqtt-smarthome/mqtt-smarthome)

An external doorbell solution for UniFi Access systems, enabling physical doorbell buttons and home automation integration.

## Why This Project Exists

UniFi Access Readers (G2 Pro, G3 Pro) have built-in doorbell functionality with "Swipe to Ring" or "Hold to Call" modes, but they **lack support for external physical doorbell buttons**. The only way to connect an external button is through the Access Hub's EXIT REQUEST terminal, which requires additional wiring and doesn't work with standalone reader setups.

This project was created to solve two common issues discussed in the Ubiquiti community:

1. **No external doorbell button support** - Many users want to connect a traditional doorbell button to their UniFi Access reader, but there's no official way to do this without an Access Hub ([Community Discussion](https://community.ui.com/questions/Request-to-Enter-REN-as-replacement-for-Swipe-to-Ring-on-Reader-Pro-possible/b498b19c-3b06-44a1-afd7-1fa300341473))

2. **Doorbell keeps ringing after door opens** - When someone rings the doorbell and you unlock the door, the doorbell animation and sound persist until manually dismissed or timeout. This is frustrating when you've already opened the door for your visitor ([Community Discussion](https://community.ui.com/questions/Access-Reader-G2-Pro-Stop-ringing-when-door-opens/1003ed7c-58ff-4e4d-b2be-31ffc7ab1dc9))

This project provides:
- **Physical doorbell button support** via GPIO pins or MQTT
- **Automatic call dismissal** when a door contact sensor detects the door has opened (via GPIO pins or MQTT)
- **MQTT integration** for home automation systems (Home Assistant, etc.)

### Known Limitations

When triggering a doorbell ring externally (via this project), the **Access Reader itself does not display the ringing animation**. However:
- All configured **Viewers (UA Intercom Viewer) will ring** and show the video feed
- The **UniFi Access mobile app** receives the doorbell notification
- **MQTT status updates** are published for home automation

This is a limitation of the UniFi Access API - externally triggered rings are handled by the system but not reflected on the originating reader's display.

---

## Components

This project consists of two components that can be used independently or together:

| Component | Purpose |
|-----------|---------|
| **ESP32 Firmware** | Standalone device with GPIO inputs, web UI, and direct UniFi API integration |
| **Go Application** | MQTT gateway for integrating UniFi Access with home automation systems |

---

## ESP32 Doorbell Controller

A standalone ESP32-based device that connects directly to your UniFi Access controller.

### Features

- Trigger doorbell rings on UniFi Access readers via GPIO buttons
- Dismiss active calls when door contact sensors detect the door opening
- MQTT integration for home automation
- **Custom GPIO triggers** - Configure multiple GPIO pins with assignable actions (ring, dismiss, generic MQTT publish)
- **Custom MQTT triggers** - Subscribe to MQTT topics and trigger actions based on JSON field values (e.g., Zigbee door sensors)
- Web-based configuration UI
- Browser-based installation (no tools required)

### Supported Hardware

| Board | Connectivity | Notes |
|-------|--------------|-------|
| **Olimex ESP32-POE** | Ethernet (PoE) | **Recommended** - Reliable wired connection, Power over Ethernet |
| Waveshare ESP32-S3-Zero | WiFi | Compact form factor |
| ESP32-S3-WROOM DevKit | WiFi | Common development board |

**Recommendation:** The Olimex ESP32-POE is the preferred choice for reliability. It provides wired Ethernet connectivity and can be powered via PoE, making installation clean and simple.

### Installation

#### Option 1: Browser-Based Installation (Recommended)

Visit the [web installer](https://mqtt-home.github.io/unifi-access-mqtt/) to flash the firmware directly from your browser.

Requirements:
- Chrome, Edge, or Opera browser (Web Serial API)
- USB cable connected to your ESP32
- USB drivers installed (CP2102 for Olimex, CH340 for other boards)

#### Option 2: PlatformIO

```bash
# Clone the repository
git clone https://github.com/mqtt-home/unifi-access-mqtt.git
cd unifi-access-mqtt/esp32/unifi-doorbell

# Build and upload (adjust environment for your board)
pio run -e esp32-poe -t upload

# Upload filesystem (web UI)
pio run -e esp32-poe -t uploadfs
```

### Configuration

1. **Initial Network Setup**

   **Ethernet (ESP32-POE):**
   - Connect the device to your network via Ethernet
   - The device obtains an IP address via DHCP
   - Access http://doorbell.local or check your router for the assigned IP

   **WiFi (ESP32-S3 boards):**
   - After flashing, the device starts in Access Point mode
   - Connect to WiFi network: `UniFi-Doorbell-Setup`
   - Open http://192.168.4.1 in your browser
   - Configure your WiFi credentials

2. **Web Configuration**
   - Access the web interface at http://doorbell.local
   - Complete the setup wizard:
     - **UniFi Access**: Enter your controller URL and credentials
     - **Select Reader**: Choose which reader to trigger for doorbell rings
     - **Configure GPIOs**: Set up your doorbell button and door contact pins

3. **GPIO Wiring**

   | Function | Wiring | Description |
   |----------|--------|-------------|
   | Ring Button | GPIO → Button → GND | Internal pull-up enabled, triggers on press |
   | Door Contact | GPIO → Sensor → GND | Internal pull-up enabled, dismisses call on open |

### MQTT Integration (Optional)

Enable MQTT in the web UI to integrate with home automation:

**Topics Published:**
- `{topic}/ring` - Doorbell ring events
- `{topic}/gpio/{label}` - GPIO state changes (for generic sensors)

**Topics Subscribed:**
- `{topic}/ring/set` - Trigger a ring via MQTT
- `{topic}/dismiss/set` - Dismiss active call via MQTT

**Custom MQTT Triggers:**

You can configure custom MQTT triggers to execute actions based on incoming messages. This is useful for integrating with Zigbee/Z-Wave sensors via Zigbee2MQTT or similar bridges.

Example: Dismiss doorbell call when door opens (Zigbee2MQTT contact sensor)
- **Topic:** `zigbee2mqtt/front_door_sensor`
- **JSON Field:** `contact`
- **Trigger Value:** `false`
- **Action:** Dismiss

When the door sensor publishes `{"contact": false}`, the active doorbell call is automatically dismissed.

---

## Go MQTT Gateway

A standalone application that bridges UniFi Access to MQTT, providing real-time door status and remote control capabilities.

### Features

- Real-time door lock/unlock status via WebSocket events
- Doorbell ring notifications
- Remote door unlock via MQTT commands
- Trigger doorbell rings via MQTT
- Support for multiple door types (UAH, UGT, UA-ULTRA, UA-Hub-Door-Mini)
- Automatic reconnection on connection loss

### Installation

#### Docker (Recommended)

```bash
docker run -d \
    -v /path/to/config.json:/var/lib/unifi-access-mqtt/config.json:ro \
    --name unifi-access-mqtt \
    ghcr.io/mqtt-home/unifi-access-mqtt:latest
```

#### Docker Compose

```yaml
version: "3"
services:
  unifi-access-mqtt:
    image: ghcr.io/mqtt-home/unifi-access-mqtt:latest
    container_name: unifi-access-mqtt
    restart: unless-stopped
    volumes:
      - ./config.json:/var/lib/unifi-access-mqtt/config.json:ro
    environment:
      - TZ=Europe/Berlin
```

#### Binary

```bash
# Build from source
cd app
make build

# Run
./build/unifi-access-mqtt /path/to/config.json
```

### Configuration

Create a `config.json` file:

```json
{
    "mqtt": {
        "url": "tcp://192.168.0.1:1883",
        "topic": "home/unifi-access",
        "retain": true,
        "qos": 1
    },
    "unifi": {
        "host": "https://192.168.1.1",
        "username": "api-user",
        "password": "your-password",
        "verify-ssl": false
    },
    "loglevel": "info"
}
```

Environment variables can be used with `${ENV_VAR}` syntax.

### MQTT Topics

#### State Topics (Published)

Door state published to `{topic}/{door-name}`:

```json
{
    "door_id": "unique-device-id",
    "name": "Front Door",
    "lock_status": "locked",
    "door_status": "closed",
    "device_type": "UAH",
    "is_online": true,
    "has_doorbell": true
}
```

Doorbell state published to `{topic}/{door-name}/doorbell`:

```json
{
    "door_id": "unique-device-id",
    "name": "Front Door",
    "status": "ringing",
    "request_id": "call-request-id"
}
```

#### Command Topics (Subscribed)

Send commands to `{topic}/{door-name}/set`:

```json
{"action": "unlock"}  // Unlock door
{"action": "ring"}    // Trigger doorbell
```

### Home Assistant Integration

```yaml
mqtt:
  lock:
    - name: "Front Door"
      state_topic: "home/unifi-access/front-door"
      command_topic: "home/unifi-access/front-door/set"
      value_template: "{{ value_json.lock_status }}"
      payload_lock: '{"action": "lock"}'
      payload_unlock: '{"action": "unlock"}'
      state_locked: "locked"
      state_unlocked: "unlocked"

  binary_sensor:
    - name: "Front Door"
      state_topic: "home/unifi-access/front-door"
      value_template: "{{ value_json.door_status }}"
      payload_on: "open"
      payload_off: "closed"
      device_class: door

    - name: "Front Door Doorbell"
      state_topic: "home/unifi-access/front-door/doorbell"
      value_template: "{{ value_json.status }}"
      payload_on: "ringing"
      payload_off: "idle"
      device_class: sound
```

---

## UniFi Access Setup

1. Log into your UniFi Access controller
2. Navigate to **Settings** > **Users**
3. Create a new user with API access permissions
4. Use these credentials in your configuration

---

## OTA Updates

Usage:

```bash
# Build and upload both firmware and filesystem
./upload-ota.sh --build

# Upload both (pre-built)
./upload-ota.sh

# Upload only firmware
./upload-ota.sh --firmware

# Upload only filesystem (web UI)
./upload-ota.sh --filesystem

# Specify host/credentials
./upload-ota.sh --host 192.168.1.50 --user admin --pass mypassword

# Different environment
./upload-ota.sh --build esp32-s3-zero
```

---

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
