#include "watchdog.h"
#include "logging.h"
#include "network.h"
#include "websocket.h"
#include "unifi_api.h"

#include <esp_task_wdt.h>
#include <esp_system.h>
#include <rom/rtc.h>

// Track when connectivity was last healthy
static unsigned long lastNetworkOk = 0;
static unsigned long lastWsOk = 0;
static bool livenessActive = false;

// =============================================================================
// Reset reason logging
// =============================================================================
static const char* resetReasonStr(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "exception/panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

void logResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  log("Reset reason: " + String(resetReasonStr(reason)));

  if (reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT ||
      reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT) {
    log("WARNING: Last reset was abnormal (" + String(resetReasonStr(reason)) + ")");
  }
}

// =============================================================================
// Hardware watchdog (Task WDT)
// =============================================================================
void setupWatchdog() {
  // Initialize the Task Watchdog Timer
  // esp_task_wdt_init(timeout_seconds, panic_on_trigger)
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
  esp_task_wdt_add(NULL);  // Subscribe current task (loopTask)

  log("Watchdog: Task WDT enabled (" + String(WDT_TIMEOUT_SECONDS) + "s timeout)");
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

// =============================================================================
// Software liveness monitor
// =============================================================================
void checkLiveness() {
  unsigned long now = millis();

  // Don't check liveness in the first 60 seconds (let connections establish)
  if (now < 60000) {
    lastNetworkOk = now;
    lastWsOk = now;
    return;
  }

  if (!livenessActive) {
    livenessActive = true;
    lastNetworkOk = now;
    lastWsOk = now;
    log("Watchdog: Liveness monitoring active");
  }

  // Update healthy timestamps
  if (networkConnected) {
    lastNetworkOk = now;
  }

  if (wsConnected) {
    lastWsOk = now;
  }

  // If not logged in, WebSocket can't be connected - use login state for WS check
  if (!isLoggedIn) {
    // Still within network timeout? Don't also penalize WS timeout
    if (networkConnected) {
      // Network is fine but can't log in - let the WS timeout handle it
    } else {
      // No network - only network timeout applies
      lastWsOk = now;
    }
  }

  // Check network liveness
  if (now - lastNetworkOk > LIVENESS_NET_TIMEOUT) {
    log("WATCHDOG: Network unreachable for " + String((now - lastNetworkOk) / 1000) + "s - restarting!");
    delay(100);  // Allow log to flush
    ESP.restart();
  }

  // Check WebSocket liveness (only meaningful if network is up)
  if (networkConnected && (now - lastWsOk > LIVENESS_WS_TIMEOUT)) {
    log("WATCHDOG: WebSocket disconnected for " + String((now - lastWsOk) / 1000) + "s - restarting!");
    delay(100);
    ESP.restart();
  }

  // Check for zombie WebSocket (connected but no traffic)
  // ESP-IDF client pings every 15s, so we should see regular activity
  unsigned long lastActivity = getLastWsActivity();
  if (wsConnected && lastActivity > 0 && (now - lastActivity > LIVENESS_WS_ACTIVITY_TIMEOUT)) {
    log("WATCHDOG: WebSocket zombie - connected but no activity for " +
        String((now - lastActivity) / 1000) + "s - restarting!");
    delay(100);
    ESP.restart();
  }
}
