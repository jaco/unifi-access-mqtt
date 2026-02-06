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
#define NIGHTLY_RELOGIN_HOUR    2       // 2:00 CET/CEST

// =============================================================================
// State
// =============================================================================
static unsigned long lastLoginAttempt = 0;
static unsigned long lastWsReconnect = 0;
static unsigned long lastStatusReport = 0;
static bool bridgeInfoPublished = false;  // Track if we published online state
static int lastReloginDay = -1;           // Track last nightly re-login day

// =============================================================================
// Nightly re-login check
// =============================================================================
static void checkNightlyRelogin() {
  time_t now_time = time(nullptr);
  if (now_time < 1700000000) return;  // NTP not synced yet

  struct tm timeinfo;
  localtime_r(&now_time, &timeinfo);

  // Check if it's the re-login hour and we haven't done it today
  if (timeinfo.tm_hour == NIGHTLY_RELOGIN_HOUR && timeinfo.tm_yday != lastReloginDay) {
    lastReloginDay = timeinfo.tm_yday;
    log("Nightly re-login triggered at " + String(timeinfo.tm_hour) + ":" +
        String(timeinfo.tm_min) + " (day " + String(timeinfo.tm_yday) + ")");
    forceRelogin();
  }
}

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
    log("Starting in AP mode for initial configuration...");
    setupApMode();
    setupWebServer();
    log("Setup complete - waiting for configuration via web UI");
    return;
  }

  // Normal operation mode
  setupNetwork();

  // Configure NTP with CET/CEST timezone
  // CET = UTC+1, CEST = UTC+2 (with automatic DST handling)
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time sync...");
  time_t now_time = time(nullptr);
  int attempts = 0;
  while (now_time < 1700000000 && attempts < 20) {
    delay(500);
    now_time = time(nullptr);
    attempts++;
  }
  if (now_time > 1700000000) {
    struct tm timeinfo;
    localtime_r(&now_time, &timeinfo);
    log("NTP time synced: " + String((long)now_time) + " (local: " +
        String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ")");
  } else {
    Serial.println("NTP sync failed, using fallback time");
  }

  // Initialize MQTT
  setupMqtt();

  // Initialize Web Server
  setupWebServer();

  log("Setup complete");
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
      mqttLoop();  // Keep MQTT alive after blocking login
      unifiBootstrap();
      connectWebSocket();
      mqttLoop();  // Keep MQTT alive after blocking WS connect
      // Use fresh millis() since login took time, and reset failure counter
      lastWsReconnect = millis();
      resetWsReconnectFailures();
    }
  }

  // WebSocket handling
  if (isLoggedIn) {
    websocketLoop();
    sendWsPing();

    // Publish bridge info once WebSocket is connected
    if (wsConnected && !bridgeInfoPublished) {
      bridgeInfoPublished = true;
      publishBridgeInfo();
    }

    // Reconnect WebSocket if needed
    // Use fresh millis() since login may have taken time
    unsigned long currentMs = millis();
    if (!wsConnected && (currentMs - lastWsReconnect > WS_RETRY_INTERVAL)) {
      bridgeInfoPublished = false;  // Reset so we republish after reconnect
      lastWsReconnect = currentMs;
      incrementWsReconnectFailures();

      if (getWsReconnectFailures() >= WS_MAX_FAILURES) {
        log("WebSocket: Too many failures, forcing re-login...");
        disconnectWebSocket();
        isLoggedIn = false;
        bridgeInfoPublished = false;  // Reset so we republish after re-login
        resetWsReconnectFailures();
        lastLoginAttempt = 0;
      } else {
        log("WebSocket: Reconnect attempt " + String(getWsReconnectFailures()));
        connectWebSocket();
        mqttLoop();  // Keep MQTT alive after blocking WS connect
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
    log("Cleared stale doorbell state");
    publishDoorbellState(false);
  }

  // Periodic status report
  if (now - lastStatusReport > STATUS_REPORT_INTERVAL) {
    lastStatusReport = now;
    printSystemStatus();
  }

  // Nightly re-login check (forces fresh authentication at 2:00 CET)
  checkNightlyRelogin();

  delay(10);
}
