#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

// MQTT client
extern PubSubClient mqtt;

// MQTT functions
void setupMqtt();
void mqttLoop();
void mqttReconnect();
void publishDoorbellState(bool ringing);
