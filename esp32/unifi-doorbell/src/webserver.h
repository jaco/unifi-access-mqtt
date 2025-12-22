#pragma once

#include <Arduino.h>

// Web server functions
void setupWebServer();
void webServerLoop();

// Broadcast status update to all WebSocket clients
void broadcastStatus();

// Broadcast doorbell event to all WebSocket clients
void broadcastDoorbellEvent(const String& event, const String& requestId = "", const String& deviceId = "");
