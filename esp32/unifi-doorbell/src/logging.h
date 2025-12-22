#pragma once

#include <Arduino.h>
#include <time.h>

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

inline void logPrint(const String& msg) {
  Serial.print(getIsoTimestamp());
  Serial.print(" ");
  Serial.print(msg);
}

inline void logPrintln(const String& msg) {
  Serial.print(getIsoTimestamp());
  Serial.print(" ");
  Serial.println(msg);
}

inline void logPrintln() {
  Serial.println();
}
