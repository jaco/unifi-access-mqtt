#pragma once

#include <Arduino.h>

// AP mode state
extern bool apModeActive;

// AP mode functions
void setupApMode();
void apModeLoop();
void stopApMode();

// Check if device should start in AP mode
bool shouldStartApMode();

// Get AP SSID
String getApSsid();
