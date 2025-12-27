#include "websocket.h"
#include "config_manager.h"
#include "logging.h"
#include "unifi_api.h"
#include "webserver.h"
#include "config.h"
#include <ArduinoJson.h>

// ESP-IDF websocket client
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_log.h"

static const char* TAG = "websocket";

// WebSocket state
volatile bool wsConnected = false;
String wsLastError = "";
static esp_websocket_client_handle_t wsClient = NULL;

// Active doorbell call state
String activeRequestId = "";
String activeDeviceId = "";
String activeConnectedUahId = "";
unsigned long activeCallTime = 0;

// Deferred processing
volatile bool pendingDoorbellStatePublish = false;
volatile bool pendingDoorbellRinging = false;
volatile bool pendingMessageProcess = false;

// Message buffer
char* pendingMessage = nullptr;

// Internal state
static int wsReconnectFailures = 0;
static int wsReconnectCount = 0;  // Total reconnects for monitoring

// Forward declarations
static void handleWebSocketMessage(const char* message);

// ESP-IDF WebSocket event handler - runs in websocket task context
static void websocket_event_handler(void* handler_args, esp_event_base_t base,
                                     int32_t event_id, void* event_data) {
  esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected");
      wsConnected = true;
      wsReconnectFailures = 0;
      wsLastError = "";  // Clear error on successful connection
      break;

    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected");
      wsConnected = false;
      if (wsLastError.isEmpty()) {
        wsLastError = "Disconnected";
      }
      break;

    case WEBSOCKET_EVENT_DATA:
      // Only process complete text frames
      if (data->op_code == 0x01 && data->data_len > 0 && data->data_ptr != nullptr) {
        // Fast check for remote_view events only
        if (memmem(data->data_ptr, data->data_len, "remote_view", 11) != nullptr) {
          if (!pendingMessageProcess && pendingMessage != nullptr) {
            size_t copyLen = (data->data_len < MESSAGE_BUFFER_SIZE - 1) ?
                             data->data_len : MESSAGE_BUFFER_SIZE - 1;
            memcpy(pendingMessage, data->data_ptr, copyLen);
            pendingMessage[copyLen] = '\0';
            pendingMessageProcess = true;
            ESP_LOGI(TAG, "Queued doorbell event (%d bytes)", data->data_len);
          }
        }
      }
      break;

    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGE(TAG, "Error");
      wsConnected = false;
      wsLastError = "Connection error";
      break;

    default:
      break;
  }
}

void initWebSocket() {
  if (pendingMessage == nullptr) {
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
      if (psramFound()) {
        pendingMessage = (char*)ps_malloc(MESSAGE_BUFFER_SIZE);
        if (pendingMessage) {
          log("WebSocket: Message buffer allocated in PSRAM (8KB)");
        }
      }
    #endif
    if (pendingMessage == nullptr) {
      pendingMessage = (char*)malloc(MESSAGE_BUFFER_SIZE);
      if (pendingMessage) {
        log("WebSocket: Message buffer allocated in RAM (8KB)");
      }
    }
    if (pendingMessage) {
      pendingMessage[0] = '\0';
    } else {
      log("WebSocket: ERROR - Failed to allocate message buffer!");
    }
  }
}

void disconnectWebSocket() {
  if (wsClient != NULL) {
    if (wsConnected) {
      log("WebSocket: Disconnecting...");
    }
    esp_websocket_client_close(wsClient, pdMS_TO_TICKS(1000));
    esp_websocket_client_destroy(wsClient);
    wsClient = NULL;
    wsConnected = false;
  }
}

// Static buffers for WebSocket config (must persist during connection)
static char wsUri[256];
static char wsHeaders[512];

void connectWebSocket() {
  if (!isLoggedIn) return;

  disconnectWebSocket();
  wsLastError = "";  // Clear error when attempting new connection

  log("WebSocket: Connecting via ESP-IDF client...");

  // Build URL and headers into static buffers
  snprintf(wsUri, sizeof(wsUri), "wss://%s/proxy/access/api/v2/ws/notification", appConfig.unifiHost);
  snprintf(wsHeaders, sizeof(wsHeaders), "Cookie: TOKEN=%s\r\n", sessionCookie.c_str());

  // Configure ESP-IDF websocket client
  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = wsUri;
  ws_cfg.headers = wsHeaders;
  ws_cfg.buffer_size = MESSAGE_BUFFER_SIZE;
  ws_cfg.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
  ws_cfg.pingpong_timeout_sec = 30;
  ws_cfg.ping_interval_sec = 15;
  ws_cfg.task_stack = 8192;
  ws_cfg.task_prio = 5;
  // TLS configuration - use dynamic certificate
  const char* cert = getCertificatePtr();
  if (cert && strlen(cert) > 50) {
    ws_cfg.cert_pem = cert;
  }
  ws_cfg.skip_cert_common_name_check = true;
  // TCP keepalive
  ws_cfg.keep_alive_enable = true;
  ws_cfg.keep_alive_idle = 5;
  ws_cfg.keep_alive_interval = 5;
  ws_cfg.keep_alive_count = 3;

  // Create client
  wsClient = esp_websocket_client_init(&ws_cfg);
  if (wsClient == NULL) {
    log("WebSocket: Failed to create client");
    return;
  }

  // Register event handler
  esp_websocket_register_events(wsClient, WEBSOCKET_EVENT_ANY,
                                 websocket_event_handler, NULL);

  // Start connection (runs in its own FreeRTOS task)
  esp_err_t err = esp_websocket_client_start(wsClient);
  if (err != ESP_OK) {
    log("WebSocket: Failed to start, err=" + String(err));
    esp_websocket_client_destroy(wsClient);
    wsClient = NULL;
  }
}

void sendWsPing() {
  // ESP-IDF client handles ping automatically via ping_interval_sec
}

void websocketLoop() {
  // ESP-IDF client runs in its own FreeRTOS task - no manual loop needed
}

void resetWsReconnectFailures() {
  wsReconnectFailures = 0;
}

int getWsReconnectFailures() {
  return wsReconnectFailures;
}

void incrementWsReconnectFailures() {
  wsReconnectFailures++;
  wsReconnectCount++;  // Track total reconnects
}

int getWsReconnectCount() {
  return wsReconnectCount;
}

void processWebSocketMessage() {
  if (!pendingMessageProcess) {
    return;
  }
  pendingMessageProcess = false;

  log("WebSocket: Processing doorbell event");
  handleWebSocketMessage(pendingMessage);
}

static void handleWebSocketMessage(const char* message) {
  if (message[0] != '{') {
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    log("WebSocket: JSON parse error: " + String(error.c_str()));
    return;
  }

  const char* event = doc["event"] | "";

  if (strcmp(event, "access.remote_view") == 0) {
    JsonObject data = doc["data"];
    if (!data.isNull()) {
      const char* requestId = data["request_id"] | "";
      const char* deviceId = data["device_id"] | "";
      const char* connectedUahId = data["connected_uah_id"] | "";

      if (strlen(requestId) > 0) {
        activeRequestId = requestId;
        activeDeviceId = deviceId;
        activeConnectedUahId = connectedUahId;
        activeCallTime = millis();

        log("WebSocket: Doorbell ring detected!");
        log("  request_id: " + activeRequestId);
        log("  device_id: " + activeDeviceId);

        pendingDoorbellStatePublish = true;
        pendingDoorbellRinging = true;

        // Broadcast to web UI clients
        broadcastDoorbellEvent("ring", activeRequestId, activeDeviceId);
      }
    }
  }

  if (strcmp(event, "access.remote_view.change") == 0) {
    JsonObject data = doc["data"];
    if (!data.isNull()) {
      const char* remoteCallRequestId = data["remote_call_request_id"] | "";

      if (strlen(remoteCallRequestId) > 0 && activeRequestId == remoteCallRequestId) {
        log("WebSocket: Doorbell call ended");
        activeRequestId = "";
        activeDeviceId = "";
        activeConnectedUahId = "";

        pendingDoorbellStatePublish = true;
        pendingDoorbellRinging = false;

        // Broadcast to web UI clients
        broadcastDoorbellEvent("idle");
      }
    }
  }
}
