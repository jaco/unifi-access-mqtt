#include "config_manager.h"
#include "logging.h"
#include "config.h"
#include <ArduinoJson.h>

// Global config instance
AppConfig appConfig;

// Preferences namespace
static Preferences prefs;
static const char* PREFS_NAMESPACE = "doorbell";

// Certificate buffer (stored in RAM for WebSocket client)
static char* certBuffer = nullptr;

void initConfigManager() {
    // Initialize with defaults
    memset(&appConfig, 0, sizeof(AppConfig));

    // Set compile-time network mode
    #ifdef USE_ETHERNET
        appConfig.useEthernet = true;
    #else
        appConfig.useEthernet = false;
    #endif

    appConfig.mqttPort = 1883;
    appConfig.mqttEnabled = false;
    appConfig.configured = false;

    // Default web credentials
    strncpy(appConfig.webUsername, "admin", CFG_MAX_USERNAME_LEN - 1);
    strncpy(appConfig.webPassword, "admin", CFG_MAX_PASSWORD_LEN - 1);

    // GPIO defaults - empty, will be populated from config.h on first run
    appConfig.gpioCount = 0;
    for (int i = 0; i < CFG_MAX_GPIO_PINS; i++) {
        appConfig.gpios[i].enabled = false;
        appConfig.gpios[i].pin = 0;
        appConfig.gpios[i].action = GPIO_ACTION_NONE;
        appConfig.gpios[i].pullMode = GPIO_PULL_UP;
        appConfig.gpios[i].label[0] = '\0';
        appConfig.gpios[i].debounceMs = 50;
        appConfig.gpios[i].holdMs = 100;
    }

    // MQTT trigger defaults - empty
    appConfig.mqttTriggerCount = 0;
    for (int i = 0; i < CFG_MAX_MQTT_TRIGGERS; i++) {
        appConfig.mqttTriggers[i].enabled = false;
        appConfig.mqttTriggers[i].topic[0] = '\0';
        appConfig.mqttTriggers[i].jsonField[0] = '\0';
        appConfig.mqttTriggers[i].triggerValue[0] = '\0';
        appConfig.mqttTriggers[i].action = MQTT_ACTION_NONE;
        appConfig.mqttTriggers[i].label[0] = '\0';
    }
}

void loadConfig() {
    initConfigManager();

    prefs.begin(PREFS_NAMESPACE, true);  // Read-only mode

    // Check if we have saved configuration
    appConfig.configured = prefs.getBool("configured", false);

    if (appConfig.configured) {
        // Load from NVS
        logPrintln("Config: Loading from NVS...");

        // WiFi
        String ssid = prefs.getString("wifi_ssid", "");
        String pass = prefs.getString("wifi_pass", "");
        strncpy(appConfig.wifiSsid, ssid.c_str(), CFG_MAX_SSID_LEN - 1);
        strncpy(appConfig.wifiPassword, pass.c_str(), CFG_MAX_PASSWORD_LEN - 1);

        // UniFi
        String host = prefs.getString("unifi_host", "");
        String user = prefs.getString("unifi_user", "");
        String upass = prefs.getString("unifi_pass", "");
        strncpy(appConfig.unifiHost, host.c_str(), CFG_MAX_HOST_LEN - 1);
        strncpy(appConfig.unifiUsername, user.c_str(), CFG_MAX_USERNAME_LEN - 1);
        strncpy(appConfig.unifiPassword, upass.c_str(), CFG_MAX_PASSWORD_LEN - 1);

        // Doorbell
        String devId = prefs.getString("db_device_id", "");
        String devName = prefs.getString("db_dev_name", "");
        String doorName = prefs.getString("db_door_name", "");
        strncpy(appConfig.doorbellDeviceId, devId.c_str(), CFG_MAX_DEVICE_ID_LEN - 1);
        strncpy(appConfig.doorbellDeviceName, devName.c_str(), CFG_MAX_NAME_LEN - 1);
        strncpy(appConfig.doorbellDoorName, doorName.c_str(), CFG_MAX_NAME_LEN - 1);

        // Viewers
        appConfig.viewerCount = prefs.getInt("viewer_count", 0);
        for (int i = 0; i < appConfig.viewerCount && i < CFG_MAX_VIEWERS; i++) {
            String key = "viewer_" + String(i);
            String vid = prefs.getString(key.c_str(), "");
            strncpy(appConfig.viewerIds[i], vid.c_str(), CFG_MAX_DEVICE_ID_LEN - 1);
        }

        // MQTT
        appConfig.mqttEnabled = prefs.getBool("mqtt_enabled", false);
        String mqttSrv = prefs.getString("mqtt_server", "");
        String mqttTopic = prefs.getString("mqtt_topic", "");
        String mqttUser = prefs.getString("mqtt_user", "");
        String mqttPass = prefs.getString("mqtt_pass", "");
        strncpy(appConfig.mqttServer, mqttSrv.c_str(), CFG_MAX_HOST_LEN - 1);
        strncpy(appConfig.mqttTopic, mqttTopic.c_str(), CFG_MAX_TOPIC_LEN - 1);
        strncpy(appConfig.mqttUsername, mqttUser.c_str(), CFG_MAX_USERNAME_LEN - 1);
        strncpy(appConfig.mqttPassword, mqttPass.c_str(), CFG_MAX_PASSWORD_LEN - 1);
        appConfig.mqttPort = prefs.getInt("mqtt_port", 1883);
        appConfig.mqttAuthEnabled = prefs.getBool("mqtt_auth", false);

        // Web UI auth
        String webUser = prefs.getString("web_user", "admin");
        String webPass = prefs.getString("web_pass", "admin");
        strncpy(appConfig.webUsername, webUser.c_str(), CFG_MAX_USERNAME_LEN - 1);
        strncpy(appConfig.webPassword, webPass.c_str(), CFG_MAX_PASSWORD_LEN - 1);

        // GPIO configuration
        appConfig.gpioCount = prefs.getInt("gpio_count", 0);
        for (int i = 0; i < appConfig.gpioCount && i < CFG_MAX_GPIO_PINS; i++) {
            String prefix = "gpio_" + String(i) + "_";
            appConfig.gpios[i].enabled = prefs.getBool((prefix + "en").c_str(), false);
            appConfig.gpios[i].pin = prefs.getInt((prefix + "pin").c_str(), 0);
            appConfig.gpios[i].action = (GpioAction)prefs.getInt((prefix + "act").c_str(), GPIO_ACTION_NONE);
            appConfig.gpios[i].pullMode = (GpioPullMode)prefs.getInt((prefix + "pull").c_str(), GPIO_PULL_UP);
            String label = prefs.getString((prefix + "lbl").c_str(), "");
            strncpy(appConfig.gpios[i].label, label.c_str(), CFG_MAX_LABEL_LEN - 1);
            appConfig.gpios[i].debounceMs = prefs.getInt((prefix + "deb").c_str(), 50);
            appConfig.gpios[i].holdMs = prefs.getInt((prefix + "hld").c_str(), 100);
        }

        // MQTT trigger configuration
        appConfig.mqttTriggerCount = prefs.getInt("mqtrig_count", 0);
        for (int i = 0; i < appConfig.mqttTriggerCount && i < CFG_MAX_MQTT_TRIGGERS; i++) {
            String prefix = "mqtrig_" + String(i) + "_";
            appConfig.mqttTriggers[i].enabled = prefs.getBool((prefix + "en").c_str(), false);
            String topic = prefs.getString((prefix + "topic").c_str(), "");
            String field = prefs.getString((prefix + "field").c_str(), "");
            String value = prefs.getString((prefix + "val").c_str(), "");
            String label = prefs.getString((prefix + "lbl").c_str(), "");
            strncpy(appConfig.mqttTriggers[i].topic, topic.c_str(), CFG_MAX_TOPIC_LEN - 1);
            strncpy(appConfig.mqttTriggers[i].jsonField, field.c_str(), CFG_MAX_JSON_FIELD_LEN - 1);
            strncpy(appConfig.mqttTriggers[i].triggerValue, value.c_str(), CFG_MAX_LABEL_LEN - 1);
            strncpy(appConfig.mqttTriggers[i].label, label.c_str(), CFG_MAX_LABEL_LEN - 1);
            appConfig.mqttTriggers[i].action = (MqttTriggerAction)prefs.getInt((prefix + "act").c_str(), MQTT_ACTION_NONE);
        }

        // JWT secret
        appConfig.jwtSecretInitialized = prefs.getBool("jwt_init", false);
        if (appConfig.jwtSecretInitialized) {
            prefs.getBytes("jwt_secret", appConfig.jwtSecret, 32);
        }

        logPrintln("Config: Loaded from NVS");
    } else {
        // First run - migrate from config.h defaults
        logPrintln("Config: First run, using config.h defaults...");

        // WiFi (if defined)
        #ifdef WIFI_SSID
            strncpy(appConfig.wifiSsid, WIFI_SSID, CFG_MAX_SSID_LEN - 1);
        #endif
        #ifdef WIFI_PASSWORD
            strncpy(appConfig.wifiPassword, WIFI_PASSWORD, CFG_MAX_PASSWORD_LEN - 1);
        #endif

        // UniFi
        #ifdef UNIFI_HOST
            strncpy(appConfig.unifiHost, UNIFI_HOST, CFG_MAX_HOST_LEN - 1);
        #endif
        #ifdef UNIFI_USERNAME
            strncpy(appConfig.unifiUsername, UNIFI_USERNAME, CFG_MAX_USERNAME_LEN - 1);
        #endif
        #ifdef UNIFI_PASSWORD
            strncpy(appConfig.unifiPassword, UNIFI_PASSWORD, CFG_MAX_PASSWORD_LEN - 1);
        #endif

        // Doorbell
        #ifdef DOORBELL_DEVICE_ID
            strncpy(appConfig.doorbellDeviceId, DOORBELL_DEVICE_ID, CFG_MAX_DEVICE_ID_LEN - 1);
        #endif
        #ifdef DOORBELL_DEVICE_NAME
            strncpy(appConfig.doorbellDeviceName, DOORBELL_DEVICE_NAME, CFG_MAX_NAME_LEN - 1);
        #endif
        #ifdef DOORBELL_DOOR_NAME
            strncpy(appConfig.doorbellDoorName, DOORBELL_DOOR_NAME, CFG_MAX_NAME_LEN - 1);
        #endif

        // Viewers
        appConfig.viewerCount = 0;
        #ifdef VIEWER_ID_1
            strncpy(appConfig.viewerIds[appConfig.viewerCount++], VIEWER_ID_1, CFG_MAX_DEVICE_ID_LEN - 1);
        #endif
        #ifdef VIEWER_ID_2
            strncpy(appConfig.viewerIds[appConfig.viewerCount++], VIEWER_ID_2, CFG_MAX_DEVICE_ID_LEN - 1);
        #endif

        // MQTT
        #ifdef MQTT_SERVER
            strncpy(appConfig.mqttServer, MQTT_SERVER, CFG_MAX_HOST_LEN - 1);
        #endif
        #ifdef MQTT_PORT
            appConfig.mqttPort = MQTT_PORT;
        #endif
        #ifdef MQTT_TOPIC
            strncpy(appConfig.mqttTopic, MQTT_TOPIC, CFG_MAX_TOPIC_LEN - 1);
        #endif
        #ifdef MQTT_USERNAME
            strncpy(appConfig.mqttUsername, MQTT_USERNAME, CFG_MAX_USERNAME_LEN - 1);
            strncpy(appConfig.mqttPassword, MQTT_PASSWORD, CFG_MAX_PASSWORD_LEN - 1);
            appConfig.mqttAuthEnabled = true;
        #endif

        // GPIO defaults from config.h
        appConfig.gpioCount = 0;
        #ifdef PIN_RING_TRIGGER
            appConfig.gpios[appConfig.gpioCount].enabled = true;
            appConfig.gpios[appConfig.gpioCount].pin = PIN_RING_TRIGGER;
            appConfig.gpios[appConfig.gpioCount].action = GPIO_ACTION_RING_BUTTON;
            appConfig.gpios[appConfig.gpioCount].pullMode = GPIO_PULL_UP;
            strncpy(appConfig.gpios[appConfig.gpioCount].label, "Ring Button", CFG_MAX_LABEL_LEN - 1);
            appConfig.gpios[appConfig.gpioCount].debounceMs = 50;
            appConfig.gpios[appConfig.gpioCount].holdMs = 100;
            appConfig.gpioCount++;
        #endif
        #ifdef PIN_DISMISS_TRIGGER
            appConfig.gpios[appConfig.gpioCount].enabled = true;
            appConfig.gpios[appConfig.gpioCount].pin = PIN_DISMISS_TRIGGER;
            appConfig.gpios[appConfig.gpioCount].action = GPIO_ACTION_DOOR_CONTACT;
            appConfig.gpios[appConfig.gpioCount].pullMode = GPIO_PULL_UP;
            strncpy(appConfig.gpios[appConfig.gpioCount].label, "Door Contact", CFG_MAX_LABEL_LEN - 1);
            appConfig.gpios[appConfig.gpioCount].debounceMs = 50;
            appConfig.gpios[appConfig.gpioCount].holdMs = 100;
            appConfig.gpioCount++;
        #endif

        // If we have UniFi credentials from config.h, mark as configured
        #if defined(UNIFI_HOST) && defined(UNIFI_USERNAME) && defined(UNIFI_PASSWORD)
            if (strlen(appConfig.unifiHost) > 0 && strlen(appConfig.unifiUsername) > 0) {
                appConfig.configured = true;
                logPrintln("Config: Migrated from config.h, saving to NVS...");
                prefs.end();
                saveConfig();
                return;
            }
        #endif

        logPrintln("Config: No valid config found, AP mode needed");
    }

    prefs.end();
}

void saveConfig() {
    prefs.begin(PREFS_NAMESPACE, false);  // Read-write mode

    // WiFi
    prefs.putString("wifi_ssid", appConfig.wifiSsid);
    prefs.putString("wifi_pass", appConfig.wifiPassword);

    // UniFi
    prefs.putString("unifi_host", appConfig.unifiHost);
    prefs.putString("unifi_user", appConfig.unifiUsername);
    prefs.putString("unifi_pass", appConfig.unifiPassword);

    // Doorbell
    prefs.putString("db_device_id", appConfig.doorbellDeviceId);
    prefs.putString("db_dev_name", appConfig.doorbellDeviceName);
    prefs.putString("db_door_name", appConfig.doorbellDoorName);

    // Viewers
    prefs.putInt("viewer_count", appConfig.viewerCount);
    for (int i = 0; i < CFG_MAX_VIEWERS; i++) {
        String key = "viewer_" + String(i);
        if (i < appConfig.viewerCount) {
            prefs.putString(key.c_str(), appConfig.viewerIds[i]);
        } else if (prefs.isKey(key.c_str())) {
            prefs.remove(key.c_str());
        }
    }

    // MQTT
    prefs.putBool("mqtt_enabled", appConfig.mqttEnabled);
    prefs.putString("mqtt_server", appConfig.mqttServer);
    prefs.putInt("mqtt_port", appConfig.mqttPort);
    prefs.putString("mqtt_topic", appConfig.mqttTopic);
    prefs.putString("mqtt_user", appConfig.mqttUsername);
    prefs.putString("mqtt_pass", appConfig.mqttPassword);
    prefs.putBool("mqtt_auth", appConfig.mqttAuthEnabled);

    // Web UI auth
    prefs.putString("web_user", appConfig.webUsername);
    prefs.putString("web_pass", appConfig.webPassword);

    // GPIO configuration
    prefs.putInt("gpio_count", appConfig.gpioCount);
    for (int i = 0; i < CFG_MAX_GPIO_PINS; i++) {
        String prefix = "gpio_" + String(i) + "_";
        if (i < appConfig.gpioCount) {
            prefs.putBool((prefix + "en").c_str(), appConfig.gpios[i].enabled);
            prefs.putInt((prefix + "pin").c_str(), appConfig.gpios[i].pin);
            prefs.putInt((prefix + "act").c_str(), (int)appConfig.gpios[i].action);
            prefs.putInt((prefix + "pull").c_str(), (int)appConfig.gpios[i].pullMode);
            prefs.putString((prefix + "lbl").c_str(), appConfig.gpios[i].label);
            prefs.putInt((prefix + "deb").c_str(), appConfig.gpios[i].debounceMs);
            prefs.putInt((prefix + "hld").c_str(), appConfig.gpios[i].holdMs);
        } else if (prefs.isKey((prefix + "en").c_str())) {
            // Clean up unused entries only if they exist
            prefs.remove((prefix + "en").c_str());
            prefs.remove((prefix + "pin").c_str());
            prefs.remove((prefix + "act").c_str());
            prefs.remove((prefix + "pull").c_str());
            prefs.remove((prefix + "lbl").c_str());
            prefs.remove((prefix + "deb").c_str());
            prefs.remove((prefix + "hld").c_str());
        }
    }

    // MQTT trigger configuration
    prefs.putInt("mqtrig_count", appConfig.mqttTriggerCount);
    for (int i = 0; i < CFG_MAX_MQTT_TRIGGERS; i++) {
        String prefix = "mqtrig_" + String(i) + "_";
        if (i < appConfig.mqttTriggerCount) {
            prefs.putBool((prefix + "en").c_str(), appConfig.mqttTriggers[i].enabled);
            prefs.putString((prefix + "topic").c_str(), appConfig.mqttTriggers[i].topic);
            prefs.putString((prefix + "field").c_str(), appConfig.mqttTriggers[i].jsonField);
            prefs.putString((prefix + "val").c_str(), appConfig.mqttTriggers[i].triggerValue);
            prefs.putString((prefix + "lbl").c_str(), appConfig.mqttTriggers[i].label);
            prefs.putInt((prefix + "act").c_str(), (int)appConfig.mqttTriggers[i].action);
        } else if (prefs.isKey((prefix + "en").c_str())) {
            prefs.remove((prefix + "en").c_str());
            prefs.remove((prefix + "topic").c_str());
            prefs.remove((prefix + "field").c_str());
            prefs.remove((prefix + "val").c_str());
            prefs.remove((prefix + "lbl").c_str());
            prefs.remove((prefix + "act").c_str());
        }
    }

    // Mark as configured
    prefs.putBool("configured", appConfig.configured);

    // JWT secret
    if (appConfig.jwtSecretInitialized) {
        prefs.putBool("jwt_init", true);
        prefs.putBytes("jwt_secret", appConfig.jwtSecret, 32);
    }

    prefs.end();
    logPrintln("Config: Saved to NVS");
}

void resetConfig() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    initConfigManager();
    logPrintln("Config: Reset to defaults");
}

bool hasWifiCredentials() {
    return strlen(appConfig.wifiSsid) > 0 && strlen(appConfig.wifiPassword) > 0;
}

bool hasUnifiCredentials() {
    return strlen(appConfig.unifiHost) > 0 &&
           strlen(appConfig.unifiUsername) > 0 &&
           strlen(appConfig.unifiPassword) > 0;
}

String getConfigJson(bool maskPasswords) {
    JsonDocument doc;

    // Network
    doc["network"]["useEthernet"] = appConfig.useEthernet;
    doc["network"]["wifiSsid"] = appConfig.wifiSsid;
    doc["network"]["wifiPassword"] = maskPasswords ? "********" : appConfig.wifiPassword;

    // UniFi
    doc["unifi"]["host"] = appConfig.unifiHost;
    doc["unifi"]["username"] = appConfig.unifiUsername;
    doc["unifi"]["password"] = maskPasswords ? "********" : appConfig.unifiPassword;

    // Doorbell
    doc["doorbell"]["deviceId"] = appConfig.doorbellDeviceId;
    doc["doorbell"]["deviceName"] = appConfig.doorbellDeviceName;
    doc["doorbell"]["doorName"] = appConfig.doorbellDoorName;

    // Viewers
    JsonArray viewers = doc["viewers"].to<JsonArray>();
    for (int i = 0; i < appConfig.viewerCount; i++) {
        viewers.add(appConfig.viewerIds[i]);
    }

    // MQTT
    doc["mqtt"]["enabled"] = appConfig.mqttEnabled;
    doc["mqtt"]["server"] = appConfig.mqttServer;
    doc["mqtt"]["port"] = appConfig.mqttPort;
    doc["mqtt"]["topic"] = appConfig.mqttTopic;
    doc["mqtt"]["authEnabled"] = appConfig.mqttAuthEnabled;
    doc["mqtt"]["username"] = appConfig.mqttUsername;
    doc["mqtt"]["password"] = maskPasswords ? "********" : appConfig.mqttPassword;

    // Web UI auth
    doc["web"]["username"] = appConfig.webUsername;
    doc["web"]["password"] = maskPasswords ? "********" : appConfig.webPassword;

    // GPIO configuration
    JsonArray gpios = doc["gpios"].to<JsonArray>();
    for (int i = 0; i < appConfig.gpioCount; i++) {
        JsonObject gpio = gpios.add<JsonObject>();
        gpio["enabled"] = appConfig.gpios[i].enabled;
        gpio["pin"] = appConfig.gpios[i].pin;
        switch (appConfig.gpios[i].action) {
            case GPIO_ACTION_RING_BUTTON: gpio["action"] = "ring_button"; break;
            case GPIO_ACTION_DOOR_CONTACT: gpio["action"] = "door_contact"; break;
            case GPIO_ACTION_GENERIC: gpio["action"] = "generic"; break;
            default: gpio["action"] = "none"; break;
        }
        gpio["pullMode"] = appConfig.gpios[i].pullMode == GPIO_PULL_UP ? "up" : "down";
        gpio["label"] = appConfig.gpios[i].label;
        gpio["debounceMs"] = appConfig.gpios[i].debounceMs;
        gpio["holdMs"] = appConfig.gpios[i].holdMs;
    }

    // MQTT trigger configuration
    JsonArray mqttTriggers = doc["mqttTriggers"].to<JsonArray>();
    for (int i = 0; i < appConfig.mqttTriggerCount; i++) {
        JsonObject trigger = mqttTriggers.add<JsonObject>();
        trigger["enabled"] = appConfig.mqttTriggers[i].enabled;
        trigger["topic"] = appConfig.mqttTriggers[i].topic;
        trigger["jsonField"] = appConfig.mqttTriggers[i].jsonField;
        trigger["triggerValue"] = appConfig.mqttTriggers[i].triggerValue;
        trigger["label"] = appConfig.mqttTriggers[i].label;
        switch (appConfig.mqttTriggers[i].action) {
            case MQTT_ACTION_RING: trigger["action"] = "ring"; break;
            case MQTT_ACTION_DISMISS: trigger["action"] = "dismiss"; break;
            default: trigger["action"] = "none"; break;
        }
    }

    // State
    doc["configured"] = appConfig.configured;

    String output;
    serializeJson(doc, output);
    return output;
}

bool updateConfigFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        logPrintln("Config: JSON parse error: " + String(error.c_str()));
        return false;
    }

    // Network (wifiSsid only, useEthernet is compile-time)
    if (doc["network"]["wifiSsid"].is<const char*>()) {
        strncpy(appConfig.wifiSsid, doc["network"]["wifiSsid"], CFG_MAX_SSID_LEN - 1);
    }
    if (doc["network"]["wifiPassword"].is<const char*>()) {
        const char* pass = doc["network"]["wifiPassword"];
        if (strcmp(pass, "********") != 0) {  // Don't overwrite with mask
            strncpy(appConfig.wifiPassword, pass, CFG_MAX_PASSWORD_LEN - 1);
        }
    }

    // UniFi
    if (doc["unifi"]["host"].is<const char*>()) {
        strncpy(appConfig.unifiHost, doc["unifi"]["host"], CFG_MAX_HOST_LEN - 1);
    }
    if (doc["unifi"]["username"].is<const char*>()) {
        strncpy(appConfig.unifiUsername, doc["unifi"]["username"], CFG_MAX_USERNAME_LEN - 1);
    }
    if (doc["unifi"]["password"].is<const char*>()) {
        const char* pass = doc["unifi"]["password"];
        if (strcmp(pass, "********") != 0) {
            strncpy(appConfig.unifiPassword, pass, CFG_MAX_PASSWORD_LEN - 1);
        }
    }

    // Doorbell
    if (doc["doorbell"]["deviceId"].is<const char*>()) {
        strncpy(appConfig.doorbellDeviceId, doc["doorbell"]["deviceId"], CFG_MAX_DEVICE_ID_LEN - 1);
    }
    if (doc["doorbell"]["deviceName"].is<const char*>()) {
        strncpy(appConfig.doorbellDeviceName, doc["doorbell"]["deviceName"], CFG_MAX_NAME_LEN - 1);
    }
    if (doc["doorbell"]["doorName"].is<const char*>()) {
        strncpy(appConfig.doorbellDoorName, doc["doorbell"]["doorName"], CFG_MAX_NAME_LEN - 1);
    }

    // Viewers
    if (doc["viewers"].is<JsonArray>()) {
        JsonArray viewers = doc["viewers"];
        appConfig.viewerCount = 0;
        for (JsonVariant v : viewers) {
            if (appConfig.viewerCount >= CFG_MAX_VIEWERS) break;
            if (v.is<const char*>()) {
                strncpy(appConfig.viewerIds[appConfig.viewerCount], v.as<const char*>(), CFG_MAX_DEVICE_ID_LEN - 1);
                appConfig.viewerCount++;
            }
        }
    }

    // MQTT
    if (doc["mqtt"]["enabled"].is<bool>()) {
        appConfig.mqttEnabled = doc["mqtt"]["enabled"];
    }
    if (doc["mqtt"]["server"].is<const char*>()) {
        strncpy(appConfig.mqttServer, doc["mqtt"]["server"], CFG_MAX_HOST_LEN - 1);
    }
    if (doc["mqtt"]["port"].is<int>()) {
        appConfig.mqttPort = doc["mqtt"]["port"];
    }
    if (doc["mqtt"]["topic"].is<const char*>()) {
        strncpy(appConfig.mqttTopic, doc["mqtt"]["topic"], CFG_MAX_TOPIC_LEN - 1);
    }
    if (doc["mqtt"]["authEnabled"].is<bool>()) {
        appConfig.mqttAuthEnabled = doc["mqtt"]["authEnabled"];
    }
    if (doc["mqtt"]["username"].is<const char*>()) {
        strncpy(appConfig.mqttUsername, doc["mqtt"]["username"], CFG_MAX_USERNAME_LEN - 1);
    }
    if (doc["mqtt"]["password"].is<const char*>()) {
        const char* pass = doc["mqtt"]["password"];
        if (strcmp(pass, "********") != 0) {
            strncpy(appConfig.mqttPassword, pass, CFG_MAX_PASSWORD_LEN - 1);
        }
    }

    // Web UI auth
    if (doc["web"]["username"].is<const char*>()) {
        strncpy(appConfig.webUsername, doc["web"]["username"], CFG_MAX_USERNAME_LEN - 1);
    }
    if (doc["web"]["password"].is<const char*>()) {
        const char* pass = doc["web"]["password"];
        if (strcmp(pass, "********") != 0) {
            strncpy(appConfig.webPassword, pass, CFG_MAX_PASSWORD_LEN - 1);
        }
    }

    // GPIO configuration
    if (doc["gpios"].is<JsonArray>()) {
        JsonArray gpios = doc["gpios"];
        appConfig.gpioCount = 0;
        for (JsonObject gpio : gpios) {
            if (appConfig.gpioCount >= CFG_MAX_GPIO_PINS) break;
            int idx = appConfig.gpioCount;

            appConfig.gpios[idx].enabled = gpio["enabled"] | false;
            appConfig.gpios[idx].pin = gpio["pin"] | 0;

            // Parse action string
            String action = gpio["action"] | "none";
            if (action == "ring_button") {
                appConfig.gpios[idx].action = GPIO_ACTION_RING_BUTTON;
            } else if (action == "door_contact") {
                appConfig.gpios[idx].action = GPIO_ACTION_DOOR_CONTACT;
            } else if (action == "generic") {
                appConfig.gpios[idx].action = GPIO_ACTION_GENERIC;
            } else {
                appConfig.gpios[idx].action = GPIO_ACTION_NONE;
            }

            // Parse pull mode
            String pullMode = gpio["pullMode"] | "up";
            appConfig.gpios[idx].pullMode = (pullMode == "down") ? GPIO_PULL_DOWN : GPIO_PULL_UP;

            // Label
            if (gpio["label"].is<const char*>()) {
                strncpy(appConfig.gpios[idx].label, gpio["label"], CFG_MAX_LABEL_LEN - 1);
            } else {
                appConfig.gpios[idx].label[0] = '\0';
            }

            // Timing
            appConfig.gpios[idx].debounceMs = gpio["debounceMs"] | 50;
            appConfig.gpios[idx].holdMs = gpio["holdMs"] | 100;

            appConfig.gpioCount++;
        }
    }

    // MQTT trigger configuration
    if (doc["mqttTriggers"].is<JsonArray>()) {
        JsonArray triggers = doc["mqttTriggers"];
        appConfig.mqttTriggerCount = 0;
        for (JsonObject trigger : triggers) {
            if (appConfig.mqttTriggerCount >= CFG_MAX_MQTT_TRIGGERS) break;
            int idx = appConfig.mqttTriggerCount;

            appConfig.mqttTriggers[idx].enabled = trigger["enabled"] | false;

            if (trigger["topic"].is<const char*>()) {
                strncpy(appConfig.mqttTriggers[idx].topic, trigger["topic"], CFG_MAX_TOPIC_LEN - 1);
            } else {
                appConfig.mqttTriggers[idx].topic[0] = '\0';
            }

            if (trigger["jsonField"].is<const char*>()) {
                strncpy(appConfig.mqttTriggers[idx].jsonField, trigger["jsonField"], CFG_MAX_JSON_FIELD_LEN - 1);
            } else {
                appConfig.mqttTriggers[idx].jsonField[0] = '\0';
            }

            if (trigger["triggerValue"].is<const char*>()) {
                strncpy(appConfig.mqttTriggers[idx].triggerValue, trigger["triggerValue"], CFG_MAX_LABEL_LEN - 1);
            } else {
                appConfig.mqttTriggers[idx].triggerValue[0] = '\0';
            }

            if (trigger["label"].is<const char*>()) {
                strncpy(appConfig.mqttTriggers[idx].label, trigger["label"], CFG_MAX_LABEL_LEN - 1);
            } else {
                appConfig.mqttTriggers[idx].label[0] = '\0';
            }

            // Parse action string
            String action = trigger["action"] | "none";
            if (action == "ring") {
                appConfig.mqttTriggers[idx].action = MQTT_ACTION_RING;
            } else if (action == "dismiss") {
                appConfig.mqttTriggers[idx].action = MQTT_ACTION_DISMISS;
            } else {
                appConfig.mqttTriggers[idx].action = MQTT_ACTION_NONE;
            }

            appConfig.mqttTriggerCount++;
        }
    }

    // Mark as configured if we have the essentials
    if (hasUnifiCredentials()) {
        appConfig.configured = true;
    }

    saveConfig();
    return true;
}

// =============================================================================
// Certificate Management (stored in NVS to survive firmware updates)
// =============================================================================

bool saveCertificate(const String& cert) {
    if (cert.length() == 0 || cert.length() > CFG_MAX_CERT_LEN) {
        logPrintln("Config: Invalid certificate length (" + String(cert.length()) + " bytes)");
        return false;
    }

    Preferences certPrefs;
    certPrefs.begin("doorbell_cert", false);
    size_t written = certPrefs.putBytes("cert", cert.c_str(), cert.length() + 1);
    certPrefs.end();

    if (written == 0) {
        logPrintln("Config: Failed to write certificate to NVS");
        return false;
    }

    loadCertificate();
    logPrintln("Config: Certificate saved (" + String(cert.length()) + " bytes)");
    return true;
}

String loadCertificate() {
    Preferences certPrefs;
    certPrefs.begin("doorbell_cert", true);

    size_t certLen = certPrefs.getBytesLength("cert");
    if (certLen > 0 && certLen <= CFG_MAX_CERT_LEN) {
        if (certBuffer) free(certBuffer);
        certBuffer = (char*)malloc(certLen);
        if (certBuffer) {
            certPrefs.getBytes("cert", certBuffer, certLen);
            certPrefs.end();
            return String(certBuffer);
        }
    }
    certPrefs.end();

    #ifdef UNIFI_SERVER_CERT
        if (certBuffer) free(certBuffer);
        certBuffer = (char*)malloc(strlen(UNIFI_SERVER_CERT) + 1);
        if (certBuffer) {
            strcpy(certBuffer, UNIFI_SERVER_CERT);
        }
        return String(UNIFI_SERVER_CERT);
    #endif

    return "";
}

bool hasCertificate() {
    Preferences certPrefs;
    certPrefs.begin("doorbell_cert", true);
    size_t certLen = certPrefs.getBytesLength("cert");
    certPrefs.end();

    if (certLen > 50) return true;

    #ifdef UNIFI_SERVER_CERT
        return strlen(UNIFI_SERVER_CERT) > 50;
    #endif

    return false;
}

const char* getCertificatePtr() {
    if (certBuffer == nullptr) {
        loadCertificate();
    }
    return certBuffer;
}
