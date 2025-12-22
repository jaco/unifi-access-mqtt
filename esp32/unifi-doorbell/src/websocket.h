#pragma once

#include <Arduino.h>

// WebSocket state (volatile for FreeRTOS task access)
extern volatile bool wsConnected;
extern String wsLastError;

// Active doorbell call state
extern String activeRequestId;
extern String activeDeviceId;
extern String activeConnectedUahId;
extern unsigned long activeCallTime;

// Deferred processing (volatile for FreeRTOS task access)
extern volatile bool pendingDoorbellStatePublish;
extern volatile bool pendingDoorbellRinging;
extern volatile bool pendingMessageProcess;
#define MESSAGE_BUFFER_SIZE 8192
extern char* pendingMessage;

// WebSocket functions
void initWebSocket();
void disconnectWebSocket();
void connectWebSocket();
void sendWsPing();
void websocketLoop();
void processWebSocketMessage();
void resetWsReconnectFailures();
int getWsReconnectFailures();
void incrementWsReconnectFailures();
int getWsReconnectCount();
