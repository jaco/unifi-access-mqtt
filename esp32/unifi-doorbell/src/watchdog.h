#pragma once

#include <Arduino.h>

// Hardware watchdog timeout (seconds) - resets ESP32 if loop() hangs
#define WDT_TIMEOUT_SECONDS 30

// Software liveness: restart if no WebSocket connection for this long (ms)
#define LIVENESS_WS_TIMEOUT    900000   // 15 minutes
// Software liveness: restart if no network for this long (ms)
#define LIVENESS_NET_TIMEOUT   600000   // 10 minutes
// Software liveness: restart if WebSocket appears connected but no traffic (ms)
// ESP-IDF client pings every 15s, so 5 minutes without any data means zombie connection
#define LIVENESS_WS_ACTIVITY_TIMEOUT 300000  // 5 minutes

void setupWatchdog();
void feedWatchdog();
void checkLiveness();
void logResetReason();
