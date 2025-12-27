#include "status.h"
#include "logging.h"
#include "config.h"
#include "network.h"
#include "websocket.h"
#include "mqtt_client.h"
#include "unifi_api.h"
#include <esp_system.h>

// PIN_STATUS_LED and PIN_NEOPIXEL are defined via build flags in platformio.ini

#ifdef PIN_NEOPIXEL
  #include <Adafruit_NeoPixel.h>
  static Adafruit_NeoPixel neopixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

#define LED_BLINK_INTERVAL 250  // 250ms on, 250ms off

static unsigned long lastLedToggle = 0;
static bool ledState = false;
static int currentMode = -1;  // -1=unknown, 0=off, 1=connected, 2=ringing

void setupStatusLed() {
  #ifdef PIN_NEOPIXEL
    neopixel.begin();
    neopixel.setBrightness(30);  // Low brightness to not be blinding
    neopixel.clear();
    neopixel.show();
    log("Status LED: NeoPixel on GPIO " + String(PIN_NEOPIXEL));
  #elif PIN_STATUS_LED >= 0
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
    log("Status LED: GPIO " + String(PIN_STATUS_LED));
  #else
    log("Status LED: disabled");
  #endif
}

void printSystemStatus() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minFreeHeap = ESP.getMinFreeHeap();
  uint32_t heapSize = ESP.getHeapSize();
  uint32_t usedHeap = heapSize - freeHeap;
  float heapUsagePercent = (float)usedHeap / heapSize * 100.0;

  // Use logDebug for system status (Serial + WebSocket only, no MQTT)
  logDebug("--- System Status ---");
  logDebug("  Heap: " + String(usedHeap / 1024) + "KB / " + String(heapSize / 1024) + "KB (" + String(heapUsagePercent, 1) + "% used)");
  logDebug("  Free: " + String(freeHeap / 1024) + "KB, Min free: " + String(minFreeHeap / 1024) + "KB");
  logDebug("  CPU: " + String(ESP.getCpuFreqMHz()) + " MHz");
  logDebug("  Uptime: " + String(millis() / 1000 / 60) + " min");
  logDebug("  WS: " + String(wsConnected ? "connected" : "disconnected") + " (reconnects: " + String(getWsReconnectCount()) + "), MQTT: " + String(mqtt.connected() ? "connected" : "disconnected"));
}

void updateStatusLed(bool isRinging) {
  #ifdef PIN_NEOPIXEL
    unsigned long now = millis();
    bool isConnected = networkConnected && isLoggedIn && wsConnected;

    if (isRinging) {
      // Blink green while ringing
      if (now - lastLedToggle >= LED_BLINK_INTERVAL) {
        lastLedToggle = now;
        ledState = !ledState;
        if (ledState) {
          neopixel.setPixelColor(0, neopixel.Color(0, 255, 0));  // Green
        } else {
          neopixel.clear();
        }
        neopixel.show();
      }
      currentMode = 2;
    } else if (isConnected) {
      // Solid blue when connected
      if (currentMode != 1) {
        currentMode = 1;
        neopixel.setPixelColor(0, neopixel.Color(0, 0, 255));  // Blue
        neopixel.show();
      }
    } else {
      // Off when disconnected
      if (currentMode != 0) {
        currentMode = 0;
        neopixel.clear();
        neopixel.show();
      }
    }
  #elif PIN_STATUS_LED >= 0
    if (isRinging) {
      // Blink LED while ringing (250ms on, 250ms off)
      unsigned long now = millis();
      if (now - lastLedToggle >= LED_BLINK_INTERVAL) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
      }
    } else {
      // Solid on when fully connected, off otherwise
      bool shouldBeOn = networkConnected && isLoggedIn && wsConnected;
      if (ledState != shouldBeOn) {
        ledState = shouldBeOn;
        digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
      }
    }
  #endif
}
