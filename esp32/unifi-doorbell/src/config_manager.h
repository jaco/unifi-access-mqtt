#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Maximum sizes for string fields (prefixed to avoid conflicts with SDK)
#define CFG_MAX_SSID_LEN 33
#define CFG_MAX_PASSWORD_LEN 65
#define CFG_MAX_HOST_LEN 64
#define CFG_MAX_USERNAME_LEN 64
#define CFG_MAX_DEVICE_ID_LEN 48
#define CFG_MAX_NAME_LEN 32
#define CFG_MAX_TOPIC_LEN 128
#define CFG_MAX_CERT_LEN 4096
#define CFG_MAX_VIEWERS 4
#define CFG_MAX_GPIO_PINS 8
#define CFG_MAX_LABEL_LEN 32
#define CFG_MAX_MQTT_TRIGGERS 4
#define CFG_MAX_JSON_FIELD_LEN 32

// Certificate storage in NVS (survives firmware updates)

// GPIO action types
enum GpioAction {
    GPIO_ACTION_NONE = 0,
    GPIO_ACTION_RING_BUTTON = 1,    // Triggers doorbell ring
    GPIO_ACTION_DOOR_CONTACT = 2,   // Dismisses active call
    GPIO_ACTION_GENERIC = 3         // Publishes to MQTT
};

// GPIO pull mode
enum GpioPullMode {
    GPIO_PULL_UP = 0,    // Active LOW (button pulls to GND)
    GPIO_PULL_DOWN = 1   // Active HIGH (button pulls to VCC)
};

// GPIO configuration for a single pin
struct GpioConfig {
    bool enabled;
    uint8_t pin;
    GpioAction action;
    GpioPullMode pullMode;
    char label[CFG_MAX_LABEL_LEN];
    uint16_t debounceMs;    // Debounce time (default 50ms)
    uint16_t holdMs;        // Hold time to trigger (default 100ms)
};

// MQTT trigger action types
enum MqttTriggerAction {
    MQTT_ACTION_NONE = 0,
    MQTT_ACTION_RING = 1,       // Trigger doorbell ring
    MQTT_ACTION_DISMISS = 2     // Dismiss active call
};

// MQTT trigger configuration
struct MqttTriggerConfig {
    bool enabled;
    char topic[CFG_MAX_TOPIC_LEN];      // Topic to subscribe to
    char jsonField[CFG_MAX_JSON_FIELD_LEN]; // JSON field to check (e.g., "contact")
    char triggerValue[CFG_MAX_LABEL_LEN];   // Value that triggers action (e.g., "false")
    MqttTriggerAction action;
    char label[CFG_MAX_LABEL_LEN];      // User-friendly label
};

// Configuration structure
struct AppConfig {
    // Network mode (compile-time for now, runtime later)
    bool useEthernet;

    // WiFi credentials (for WiFi boards)
    char wifiSsid[CFG_MAX_SSID_LEN];
    char wifiPassword[CFG_MAX_PASSWORD_LEN];

    // UniFi Access controller
    char unifiHost[CFG_MAX_HOST_LEN];
    char unifiUsername[CFG_MAX_USERNAME_LEN];
    char unifiPassword[CFG_MAX_PASSWORD_LEN];

    // Doorbell device
    char doorbellDeviceId[CFG_MAX_DEVICE_ID_LEN];
    char doorbellDeviceName[CFG_MAX_NAME_LEN];
    char doorbellDoorName[CFG_MAX_NAME_LEN];

    // Viewer devices
    char viewerIds[CFG_MAX_VIEWERS][CFG_MAX_DEVICE_ID_LEN];
    int viewerCount;

    // MQTT settings (optional)
    bool mqttEnabled;
    char mqttServer[CFG_MAX_HOST_LEN];
    uint16_t mqttPort;
    char mqttTopic[CFG_MAX_TOPIC_LEN];
    char mqttUsername[CFG_MAX_USERNAME_LEN];
    char mqttPassword[CFG_MAX_PASSWORD_LEN];
    bool mqttAuthEnabled;

    // Web UI authentication
    char webUsername[CFG_MAX_USERNAME_LEN];
    char webPassword[CFG_MAX_PASSWORD_LEN];

    // GPIO configuration
    GpioConfig gpios[CFG_MAX_GPIO_PINS];
    int gpioCount;

    // MQTT trigger configuration
    MqttTriggerConfig mqttTriggers[CFG_MAX_MQTT_TRIGGERS];
    int mqttTriggerCount;

    // System state
    bool configured;  // false = first run, show AP mode
};

// Global config instance
extern AppConfig appConfig;

// Configuration management functions
void initConfigManager();
void loadConfig();
void saveConfig();
void resetConfig();

// Helper to check if WiFi credentials are set
bool hasWifiCredentials();

// Helper to check if UniFi credentials are set
bool hasUnifiCredentials();

// Get config as JSON (for web API, passwords masked)
String getConfigJson(bool maskPasswords = true);

// Update config from JSON (for web API)
bool updateConfigFromJson(const String& json);

// Certificate management (stored in NVS, survives firmware updates)
bool saveCertificate(const String& cert);
String loadCertificate();
bool hasCertificate();
const char* getCertificatePtr();
