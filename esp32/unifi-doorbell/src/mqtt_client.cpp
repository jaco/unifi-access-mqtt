#include "mqtt_client.h"
#include "config_manager.h"
#include "logging.h"
#include "config.h"
#include "unifi_api.h"
#include "websocket.h"
#include <WiFi.h>
#if defined(USE_ETHERNET)
  #include <ETH.h>
#endif
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Firmware version - defined by build script
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

static WiFiClient netClient;
PubSubClient mqtt(netClient);

static unsigned long lastMqttReconnect = 0;
#define MQTT_RETRY_INTERVAL 5000

// Get current IP address
static String getLocalIP() {
#if defined(USE_ETHERNET)
  return ETH.localIP().toString();
#else
  return WiFi.localIP().toString();
#endif
}

// Publish bridge status information (called when fully connected to UniFi)
void publishBridgeInfo() {
  if (!mqtt.connected()) return;

  String baseTopic = String(appConfig.mqttTopic) + "/bridge";

  // Build topic strings (must persist during publish)
  String stateTopic = baseTopic + "/state";
  String versionTopic = baseTopic + "/version";
  String ipTopic = baseTopic + "/ip";
  String ip = getLocalIP();

  // Publish online state (retained)
  bool stateOk = mqtt.publish(stateTopic.c_str(), "online", true);
  bool versionOk = mqtt.publish(versionTopic.c_str(), FIRMWARE_VERSION, true);
  bool ipOk = mqtt.publish(ipTopic.c_str(), ip.c_str(), true);

  if (stateOk && versionOk && ipOk) {
    log("MQTT: Published bridge info (state=online, version=" + String(FIRMWARE_VERSION) + ", ip=" + ip + ")");
  } else {
    log("MQTT: Failed to publish bridge info (state=" + String(stateOk) + ", version=" + String(versionOk) + ", ip=" + String(ipOk) + ")");
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length);

void setupMqtt() {
  if (!appConfig.mqttEnabled) {
    log("MQTT: Disabled");
    return;
  }

  if (strlen(appConfig.mqttServer) == 0) {
    log("MQTT: No server configured");
    return;
  }

  log("MQTT: Server=" + String(appConfig.mqttServer) +
             " Port=" + String(appConfig.mqttPort) +
             " Topic=" + String(appConfig.mqttTopic) +
             " Auth=" + String(appConfig.mqttAuthEnabled ? "yes" : "no"));

  mqtt.setServer(appConfig.mqttServer, appConfig.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(60);     // 60 second keepalive (default 15 is too short)
  mqtt.setSocketTimeout(10); // 10 second socket timeout
  log("MQTT: Configured");
}

void mqttLoop() {
  mqtt.loop();
}

void mqttReconnect() {
  if (!appConfig.mqttEnabled) return;
  if (mqtt.connected()) return;
  if (strlen(appConfig.mqttServer) == 0) return;
  if (millis() - lastMqttReconnect < MQTT_RETRY_INTERVAL) return;

  lastMqttReconnect = millis();
  log("MQTT: Connecting to " + String(appConfig.mqttServer) + ":" + String(appConfig.mqttPort));

  // Test TCP connectivity first
  if (!netClient.connect(appConfig.mqttServer, appConfig.mqttPort)) {
    log("MQTT: TCP connection failed to " + String(appConfig.mqttServer) + ":" + String(appConfig.mqttPort));
    return;
  }
  netClient.stop();  // Close test connection

  String clientId = "esp32-doorbell-" + String(random(0xffff), HEX);

  // LWT (Last Will Testament) for bridge state
  String willTopic = String(appConfig.mqttTopic) + "/bridge/state";
  const char* willMessage = "offline";

  bool connected = false;
  if (appConfig.mqttAuthEnabled && strlen(appConfig.mqttUsername) > 0) {
    logDebug("MQTT: Using auth: " + String(appConfig.mqttUsername));
    connected = mqtt.connect(clientId.c_str(), appConfig.mqttUsername, appConfig.mqttPassword,
                             willTopic.c_str(), 0, true, willMessage);
  } else {
    logDebug("MQTT: No auth");
    connected = mqtt.connect(clientId.c_str(), willTopic.c_str(), 0, true, willMessage);
  }

  if (connected) {
    log("MQTT: Connected");

    // Subscribe to command topic
    String cmdTopic = String(appConfig.mqttTopic) + "/set";
    mqtt.subscribe(cmdTopic.c_str());
    log("MQTT: Subscribed to " + cmdTopic);

    // Subscribe to configured trigger topics
    for (int i = 0; i < appConfig.mqttTriggerCount; i++) {
      if (appConfig.mqttTriggers[i].enabled && strlen(appConfig.mqttTriggers[i].topic) > 0) {
        mqtt.subscribe(appConfig.mqttTriggers[i].topic);
        log("MQTT: Subscribed to trigger: " + String(appConfig.mqttTriggers[i].topic));
      }
    }

    // Publish bridge info (if WebSocket connected) and doorbell state after reconnect
    // This overwrites the LWT "offline" message with "online"
    if (wsConnected) {
      publishBridgeInfo();
    }
    publishDoorbellState(activeRequestId.length() > 0);
  } else {
    int state = mqtt.state();
    String errorMsg;
    switch (state) {
      case -4: errorMsg = "MQTT_CONNECTION_TIMEOUT"; break;
      case -3: errorMsg = "MQTT_CONNECTION_LOST"; break;
      case -2: errorMsg = "MQTT_CONNECT_FAILED"; break;
      case -1: errorMsg = "MQTT_DISCONNECTED"; break;
      case 1: errorMsg = "MQTT_CONNECT_BAD_PROTOCOL"; break;
      case 2: errorMsg = "MQTT_CONNECT_BAD_CLIENT_ID"; break;
      case 3: errorMsg = "MQTT_CONNECT_UNAVAILABLE"; break;
      case 4: errorMsg = "MQTT_CONNECT_BAD_CREDENTIALS"; break;
      case 5: errorMsg = "MQTT_CONNECT_UNAUTHORIZED"; break;
      default: errorMsg = "UNKNOWN"; break;
    }
    log("MQTT: Failed, rc=" + String(state) + " (" + errorMsg + ")");
  }
}

void publishDoorbellState(bool ringing) {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["status"] = ringing ? "ringing" : "idle";
  if (ringing && activeRequestId.length() > 0) {
    doc["request_id"] = activeRequestId;
    doc["device_id"] = activeDeviceId;
  }

  String payload;
  serializeJson(doc, payload);

  String topic = String(appConfig.mqttTopic) + "/doorbell";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
  log("MQTT: Published doorbell state: " + payload);
}

// Helper to check if a JSON value matches the trigger value string
static bool valueMatches(JsonVariant value, const char* triggerValue) {
  if (value.isNull()) return false;

  String triggerStr = String(triggerValue);
  triggerStr.toLowerCase();

  if (value.is<bool>()) {
    bool bVal = value.as<bool>();
    return (bVal && (triggerStr == "true" || triggerStr == "1")) ||
           (!bVal && (triggerStr == "false" || triggerStr == "0"));
  }

  if (value.is<int>() || value.is<float>()) {
    return String(value.as<float>(), 2) == triggerStr ||
           String(value.as<int>()) == triggerStr;
  }

  if (value.is<const char*>()) {
    String valStr = value.as<const char*>();
    valStr.toLowerCase();
    return valStr == triggerStr;
  }

  return false;
}

// Execute a trigger action
static void executeTriggerAction(MqttTriggerAction action, const char* label) {
  switch (action) {
    case MQTT_ACTION_RING:
      log("MQTT Trigger: Executing RING action (" + String(label) + ")");
      unifiTriggerRing();
      break;

    case MQTT_ACTION_DISMISS:
      log("MQTT Trigger: Executing DISMISS action (" + String(label) + ")");
      if (activeRequestId.length() > 0 && activeDeviceId.length() > 0) {
        if (unifiDismissCall(activeDeviceId, activeRequestId)) {
          activeRequestId = "";
          activeDeviceId = "";
          activeConnectedUahId = "";
          publishDoorbellState(false);
        }
      } else {
        log("MQTT Trigger: No active call to dismiss");
      }
      break;

    default:
      break;
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  log("MQTT: Received [" + String(topic) + "]: " + message);

  String topicStr = String(topic);

  // Check command topic
  if (topicStr.endsWith("/set")) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (!error) {
      String action = doc["action"] | "";

      if (action == "dismiss" || action == "cancel" || action == "end_call") {
        if (activeRequestId.length() > 0 && activeDeviceId.length() > 0) {
          if (unifiDismissCall(activeDeviceId, activeRequestId)) {
            activeRequestId = "";
            activeDeviceId = "";
            activeConnectedUahId = "";
            publishDoorbellState(false);
          }
        } else {
          log("MQTT: No active doorbell call to dismiss");
        }
      }
      else if (action == "ring") {
        unifiTriggerRing();
      }
    }
    return;
  }

  // Check trigger topics
  for (int i = 0; i < appConfig.mqttTriggerCount; i++) {
    MqttTriggerConfig& trigger = appConfig.mqttTriggers[i];

    if (!trigger.enabled) continue;
    if (strcmp(topic, trigger.topic) != 0) continue;

    // Topic matches - parse JSON and check field
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      log("MQTT Trigger: Failed to parse JSON: " + String(error.c_str()));
      continue;
    }

    // Get the JSON field value
    JsonVariant fieldValue = doc[trigger.jsonField];
    if (fieldValue.isNull()) {
      log("MQTT Trigger: Field '" + String(trigger.jsonField) + "' not found");
      continue;
    }

    // Check if value matches
    if (valueMatches(fieldValue, trigger.triggerValue)) {
      log("MQTT Trigger: Match! Field '" + String(trigger.jsonField) +
                 "' = '" + String(trigger.triggerValue) + "'");
      executeTriggerAction(trigger.action, trigger.label);
    }
  }
}

void publishMqttLog(const String& message) {
  if (!mqtt.connected()) return;
  if (!appConfig.mqttEnabled) return;

  String topic = String(appConfig.mqttTopic) + "/bridge/logs";
  mqtt.publish(topic.c_str(), message.c_str(), false);  // Not retained
}
