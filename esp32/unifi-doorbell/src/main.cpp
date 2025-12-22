/**
 * UniFi Access Doorbell Controller
 *
 * Standalone implementation - no external gateway required.
 * Connects directly to UniFi Access controller via WebSocket.
 *
 * Supported boards:
 * - Olimex ESP32-POE (Ethernet)
 * - Waveshare ESP32-S3-Zero (WiFi)
 * - ESP32-S3-WROOM-1 DevKit (WiFi)
 *
 * Features:
 * - Dismiss doorbell calls via GPIO (contact sensor) or MQTT
 * - Trigger doorbell calls via GPIO or MQTT
 * - Real-time doorbell detection via WebSocket
 */

#include <Arduino.h>
#include <esp_random.h>
#include <time.h>

#include "config.h"
#include "config_manager.h"
#include "logging.h"
#include "network.h"
#include "unifi_api.h"
#include "websocket.h"
#include "mqtt_client.h"
#include "gpio.h"
#include "status.h"
#include "webserver.h"
#include "ap_mode.h"

// =============================================================================
// Network type for display
// =============================================================================
#if defined(USE_ETHERNET)
  #define NETWORK_TYPE "Ethernet"
#elif defined(USE_WIFI)
  #define NETWORK_TYPE "WiFi"
#else
  #error "Please define USE_ETHERNET or USE_WIFI in build flags"
#endif

// =============================================================================
// Timing constants
// =============================================================================
#define LOGIN_RETRY_INTERVAL    30000
#define WS_RETRY_INTERVAL       10000
#define STATUS_REPORT_INTERVAL  60000
#define WS_MAX_FAILURES         5
#define STALE_CALL_TIMEOUT      300000  // 5 minutes

// =============================================================================
// State
// =============================================================================
static unsigned long lastLoginAttempt = 0;
static unsigned long lastWsReconnect = 0;
static unsigned long lastStatusReport = 0;

// =============================================================================
// Setup
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nUniFi Access Doorbell Controller (Standalone)");
  Serial.println("==============================================");
  Serial.print("Network: ");
  Serial.println(NETWORK_TYPE);

  // Load configuration from NVS (or migrate from config.h)
  loadConfig();

  // Initialize components
  setupGpio();
  setupStatusLed();
  initWebSocket();
  randomSeed(esp_random());

  // Check if we should start in AP mode (unconfigured WiFi device)
  if (shouldStartApMode()) {
    logPrintln("Starting in AP mode for initial configuration...");
    setupApMode();
    setupWebServer();
    logPrintln("Setup complete - waiting for configuration via web UI");
    return;
  }

  // Normal operation mode
  setupNetwork();

  // Configure NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time sync...");
  time_t now_time = time(nullptr);
  int attempts = 0;
  while (now_time < 1700000000 && attempts < 20) {
    delay(500);
    now_time = time(nullptr);
    attempts++;
  }
  if (now_time > 1700000000) {
    logPrintln("NTP time synced: " + String((long)now_time));
  } else {
    Serial.println("NTP sync failed, using fallback time");
  }

  // Initialize MQTT
  setupMqtt();

  // Initialize Web Server
  setupWebServer();

  logPrintln("Setup complete");
  printSystemStatus();
}

// =============================================================================
// Main Loop
// =============================================================================
void loop() {
  unsigned long now = millis();

  // AP mode handling - simplified loop for configuration only
  if (apModeActive) {
    apModeLoop();
    webServerLoop();
    delay(10);
    return;
  }

  // Network handling
  networkLoop();

  // Web server handling (runs regardless of UniFi connection)
  webServerLoop();

  // LED: blink when ringing, solid when connected, off otherwise
  bool isRinging = activeRequestId.length() > 0;
  updateStatusLed(isRinging);

  if (!networkConnected) {
    delay(100);
    return;
  }

  // Login to UniFi if needed
  if (!isLoggedIn && (now - lastLoginAttempt > LOGIN_RETRY_INTERVAL)) {
    lastLoginAttempt = now;
    if (unifiLogin()) {
      unifiBootstrap();
      connectWebSocket();
      // Use fresh millis() since login took time, and reset failure counter
      lastWsReconnect = millis();
      resetWsReconnectFailures();
    }
  }

  // WebSocket handling
  if (isLoggedIn) {
    websocketLoop();
    sendWsPing();

    // Reconnect WebSocket if needed
    // Use fresh millis() since login may have taken time
    unsigned long currentMs = millis();
    if (!wsConnected && (currentMs - lastWsReconnect > WS_RETRY_INTERVAL)) {
      lastWsReconnect = currentMs;
      incrementWsReconnectFailures();

      if (getWsReconnectFailures() >= WS_MAX_FAILURES) {
        logPrintln("WebSocket: Too many failures, forcing re-login...");
        disconnectWebSocket();
        isLoggedIn = false;
        resetWsReconnectFailures();
        lastLoginAttempt = 0;
      } else {
        logPrintln("WebSocket: Reconnect attempt " + String(getWsReconnectFailures()));
        connectWebSocket();
      }
    }
  }

  // Process deferred WebSocket messages (parsing moved out of callback)
  processWebSocketMessage();

  // MQTT handling
  if (!mqtt.connected()) {
    mqttReconnect();
    // Service WebSocket after potentially blocking MQTT reconnect
    if (isLoggedIn) websocketLoop();
  }
  mqttLoop();

  // Handle deferred doorbell state publish
  if (pendingDoorbellStatePublish) {
    pendingDoorbellStatePublish = false;
    publishDoorbellState(pendingDoorbellRinging);
  }

  // GPIO handling
  checkGpioTriggers();

  // Clear stale doorbell state
  unsigned long currentTime = millis();
  if (activeRequestId.length() > 0 && activeCallTime > 0 &&
      currentTime > activeCallTime && (currentTime - activeCallTime > STALE_CALL_TIMEOUT)) {
    activeRequestId = "";
    activeDeviceId = "";
    activeConnectedUahId = "";
    activeCallTime = 0;
    logPrintln("Cleared stale doorbell state");
    publishDoorbellState(false);
  }

  // Periodic status report
  if (now - lastStatusReport > STATUS_REPORT_INTERVAL) {
    lastStatusReport = now;
    printSystemStatus();
  }

  delay(10);
}
