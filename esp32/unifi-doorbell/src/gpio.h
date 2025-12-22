#pragma once

#include <Arduino.h>
#include "config_manager.h"

// GPIO state structure (per pin)
struct GpioState {
    bool currentState;      // Current debounced state (true = active)
    bool lastRawState;      // Last raw reading
    bool triggered;         // True if action was triggered (prevents re-trigger)
    unsigned long lastChange;  // Timestamp of last state change
};

// GPIO state array
extern GpioState gpioStates[CFG_MAX_GPIO_PINS];

// GPIO functions
void setupGpio();
void checkGpioTriggers();

// Get GPIO state for web API
bool getGpioState(int index);
const char* getGpioStateString(int index);
