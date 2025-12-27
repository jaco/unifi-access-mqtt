#pragma once

#include <Arduino.h>
#include <time.h>

// Forward declarations for log broadcast (implemented in webserver.cpp)
void broadcastLog(const String& timestamp, const String& message);       // Serial + WebSocket + MQTT
void broadcastLogLocal(const String& timestamp, const String& message);  // Serial + WebSocket only

inline String getIsoTimestamp() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return "[+" + String(millis() / 1000) + "s]";
  }
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

// Main logging function - Serial + WebSocket + MQTT
inline void log(const String& msg) {
  String timestamp = getIsoTimestamp();
  Serial.print(timestamp);
  Serial.print(" ");
  Serial.println(msg);
  broadcastLog(timestamp, msg);
}

// Debug logging - Serial + WebSocket only (no MQTT)
inline void logDebug(const String& msg) {
  String timestamp = getIsoTimestamp();
  Serial.print(timestamp);
  Serial.print(" ");
  Serial.println(msg);
  broadcastLogLocal(timestamp, msg);
}
