#include "network.h"
#include "config_manager.h"
#include "logging.h"
#include "unifi_api.h"
#include "websocket.h"

// WiFi.h must be included before ETH.h (ETH.h depends on WiFi types)
#include <WiFi.h>

#if defined(USE_ETHERNET)
  #include <ETH.h>
#endif

bool networkConnected = false;

// =============================================================================
// Ethernet Implementation
// =============================================================================

#if defined(USE_ETHERNET)

void onEthEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      logPrintln("ETH: Started");
      ETH.setHostname("unifi-doorbell");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      logPrintln("ETH: Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      logPrintln("ETH: Got IP: " + ETH.localIP().toString());
      networkConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      logPrintln("ETH: Disconnected");
      disconnectWebSocket();
      networkConnected = false;
      isLoggedIn = false;
      break;
    default:
      break;
  }
}

void setupNetwork() {
  logPrintln("Initializing Ethernet...");
  WiFi.onEvent(onEthEvent);
  ETH.begin();
}

void networkLoop() {
  // Ethernet handles itself via events
}

// =============================================================================
// WiFi Implementation
// =============================================================================

#elif defined(USE_WIFI)

static unsigned long lastWifiCheck = 0;
#define WIFI_CHECK_INTERVAL 10000

void setupNetwork() {
  logPrintln("Initializing WiFi...");

  if (!hasWifiCredentials()) {
    logPrintln("WiFi: No credentials configured");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("unifi-doorbell");
  WiFi.begin(appConfig.wifiSsid, appConfig.wifiPassword);

  logPrint("Connecting to WiFi: " + String(appConfig.wifiSsid));
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    logPrintln();
    logPrintln("WiFi: Connected, IP: " + WiFi.localIP().toString());
    // Disable power saving for more stable connection
    WiFi.setSleep(false);
    networkConnected = true;
  } else {
    logPrintln();
    logPrintln("WiFi: Connection failed, will retry...");
  }
}

void networkLoop() {
  unsigned long now = millis();

  // Skip if no credentials
  if (!hasWifiCredentials()) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!networkConnected) {
      logPrintln("WiFi: Reconnected, IP: " + WiFi.localIP().toString());
      WiFi.setSleep(false);
      networkConnected = true;
    }
  } else {
    if (networkConnected) {
      logPrintln("WiFi: Disconnected");
      disconnectWebSocket();
      networkConnected = false;
      isLoggedIn = false;
    }

    if (now - lastWifiCheck > WIFI_CHECK_INTERVAL) {
      lastWifiCheck = now;
      logPrintln("WiFi: Attempting reconnect...");
      WiFi.disconnect();
      WiFi.begin(appConfig.wifiSsid, appConfig.wifiPassword);
    }
  }
}

#else
  #error "Please define USE_ETHERNET or USE_WIFI in build flags"
#endif
