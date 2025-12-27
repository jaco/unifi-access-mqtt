#include "webserver.h"
#include "config_manager.h"
#include "logging.h"
#include "network.h"
#include "unifi_api.h"
#include "websocket.h"
#include "mqtt_client.h"
#include "gpio.h"
#include "ap_mode.h"
#include "jwt.h"

#include <WiFi.h>
#if defined(USE_ETHERNET)
  #include <ETH.h>
#endif
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/base64.h>

// Firmware version - defined by build script, fallback for local builds
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Board type - defined by platformio.ini build flags
#ifndef BOARD_TYPE
#define BOARD_TYPE "unknown"
#endif

// Web server on port 80
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Status broadcast interval
static unsigned long lastStatusBroadcast = 0;
#define STATUS_BROADCAST_INTERVAL 5000

// WiFi test state machine (for non-blocking test in AP mode)
enum WifiTestState { WIFI_TEST_IDLE, WIFI_TEST_CONNECTING, WIFI_TEST_SUCCESS, WIFI_TEST_FAILED };
static WifiTestState wifiTestState = WIFI_TEST_IDLE;
static String wifiTestIp = "";
static unsigned long wifiTestStartTime = 0;

// Initialize JWT with persistent secret
static void initJwtSecret() {
    if (appConfig.jwtSecretInitialized) {
        // Load secret from config
        setJwtSecret(appConfig.jwtSecret);
        log("WebServer: JWT secret loaded from config");
    } else {
        // Generate new secret and save it
        generateJwtSecret();
        memcpy(appConfig.jwtSecret, getJwtSecret(), 32);
        appConfig.jwtSecretInitialized = true;
        saveConfig();
        log("WebServer: Generated and saved new JWT secret");
    }
}

// Check authentication using JWT
static bool checkAuth(AsyncWebServerRequest* request) {
    // In AP mode, skip authentication for easier setup
    if (apModeActive) {
        return true;
    }

    // Check for token in cookie
    if (request->hasHeader("Cookie")) {
        String cookie = request->header("Cookie");
        int tokenIdx = cookie.indexOf("auth_token=");
        if (tokenIdx >= 0) {
            int end = cookie.indexOf(";", tokenIdx);
            String token = (end > 0) ? cookie.substring(tokenIdx + 11, end) : cookie.substring(tokenIdx + 11);

            // Validate JWT token
            String username = validateJwtToken(token);
            if (username.length() > 0) {
                return true;
            }
        }
    }
    return false;
}

// Send 401 response
static void sendUnauthorized(AsyncWebServerRequest* request) {
    request->send(401, "application/json", "{\"success\":false,\"message\":\"Unauthorized\"}");
}

// Forward declarations
static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
static String getStatusJson();

void setupWebServer() {
    // Initialize JWT with persistent secret
    initJwtSecret();

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        log("WebServer: LittleFS mount failed");
    } else {
        log("WebServer: LittleFS mounted");
    }

    // Start mDNS (doorbell.local)
    if (MDNS.begin("doorbell")) {
        MDNS.addService("http", "tcp", 80);
        log("WebServer: mDNS started: doorbell.local");
    }

    // WebSocket handler
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // API: Check if in AP mode (no auth required)
    server.on("/api/mode", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["apMode"] = apModeActive;
        doc["configured"] = appConfig.configured;
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // API: WiFi test start - initiates connection test
    server.on("/api/wifi/test", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body = "";
            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            if (index + len == total) {
                // Only allow in AP mode
                if (!apModeActive) {
                    request->send(403, "application/json", "{\"success\":false,\"message\":\"Only available in AP mode\"}");
                    body = "";
                    return;
                }

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);
                if (!error) {
                    String ssid = doc["ssid"] | "";
                    String password = doc["password"] | "";

                    if (ssid.length() == 0) {
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID is required\"}");
                        body = "";
                        return;
                    }

                    log("WebServer: Starting WiFi test to " + ssid);

                    // Start connection attempt (non-blocking)
                    WiFi.mode(WIFI_AP_STA);
                    WiFi.begin(ssid.c_str(), password.c_str());
                    wifiTestState = WIFI_TEST_CONNECTING;
                    wifiTestStartTime = millis();
                    wifiTestIp = "";

                    // Return immediately - frontend will poll for status
                    request->send(200, "application/json", "{\"status\":\"connecting\",\"message\":\"Testing connection...\"}");
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
                }
                body = "";
            }
        }
    );

    // API: WiFi test status - poll for connection result
    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!apModeActive) {
            request->send(403, "application/json", "{\"success\":false,\"message\":\"Only available in AP mode\"}");
            return;
        }

        // Check WiFi connection status
        if (wifiTestState == WIFI_TEST_CONNECTING) {
            if (WiFi.status() == WL_CONNECTED) {
                wifiTestIp = WiFi.localIP().toString();
                wifiTestState = WIFI_TEST_SUCCESS;
                log("WebServer: WiFi test successful, IP: " + wifiTestIp);
                // Don't disconnect yet - let /api/wifi/setup handle cleanup
            } else if (millis() - wifiTestStartTime > 15000) {
                // Timeout after 15 seconds
                wifiTestState = WIFI_TEST_FAILED;
                log("WebServer: WiFi test failed - timeout");
                WiFi.disconnect(true);
                WiFi.mode(WIFI_AP);
            }
        }

        // Return current status
        JsonDocument doc;
        if (wifiTestState == WIFI_TEST_CONNECTING) {
            doc["status"] = "connecting";
            doc["message"] = "Testing connection...";
        } else if (wifiTestState == WIFI_TEST_SUCCESS) {
            doc["status"] = "success";
            doc["success"] = true;
            doc["message"] = "Connection successful";
            doc["ip"] = wifiTestIp;
            wifiTestState = WIFI_TEST_IDLE;  // Reset for next test
        } else if (wifiTestState == WIFI_TEST_FAILED) {
            doc["status"] = "failed";
            doc["success"] = false;
            doc["message"] = "Could not connect. Check SSID and password.";
            wifiTestState = WIFI_TEST_IDLE;  // Reset for next test
        } else {
            doc["status"] = "idle";
        }

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // API: WiFi setup for AP mode (no auth required in AP mode)
    server.on("/api/wifi/setup", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body = "";
            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            if (index + len == total) {
                // Only allow in AP mode
                if (!apModeActive) {
                    request->send(403, "application/json", "{\"success\":false,\"message\":\"Only available in AP mode\"}");
                    body = "";
                    return;
                }

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);
                if (!error) {
                    String ssid = doc["ssid"] | "";
                    String password = doc["password"] | "";

                    if (ssid.length() == 0) {
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID is required\"}");
                    } else {
                        // Save WiFi credentials
                        strncpy(appConfig.wifiSsid, ssid.c_str(), CFG_MAX_SSID_LEN - 1);
                        strncpy(appConfig.wifiPassword, password.c_str(), CFG_MAX_PASSWORD_LEN - 1);
                        appConfig.configured = true;
                        saveConfig();

                        log("WebServer: WiFi configured via AP mode, rebooting...");
                        request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi configured. Rebooting...\"}");

                        // Delay to allow response to be sent, then reboot
                        delay(1000);
                        ESP.restart();
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
                }
                body = "";
            }
        }
    );

    // API: Login
    server.on("/api/auth/login", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body = "";
            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            if (index + len == total) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);
                if (!error) {
                    String username = doc["username"].as<String>();
                    String password = doc["password"].as<String>();

                    // Get expected credentials with fallback to defaults
                    const char* expectedUser = strlen(appConfig.webUsername) > 0 ? appConfig.webUsername : "admin";
                    const char* expectedPass = strlen(appConfig.webPassword) > 0 ? appConfig.webPassword : "admin";

                    if (username == expectedUser && password == expectedPass) {
                        // Create JWT token (valid for 24 hours)
                        String jwtToken = createJwtToken(username);

                        AsyncWebServerResponse* response = request->beginResponse(200, "application/json",
                            "{\"success\":true}");
                        // Set cookie with 24 hour expiration (matches JWT expiration)
                        response->addHeader("Set-Cookie", "auth_token=" + jwtToken + "; Path=/; Max-Age=86400; HttpOnly");
                        request->send(response);
                        log("WebServer: User logged in with JWT");
                    } else {
                        request->send(401, "application/json", "{\"success\":false,\"message\":\"Invalid credentials\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
                }
                body = "";
            }
        }
    );

    // API: Logout
    server.on("/api/auth/logout", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Just clear the cookie - JWT is stateless
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"success\":true}");
        response->addHeader("Set-Cookie", "auth_token=; Path=/; Max-Age=0");
        request->send(response);
    });

    // API: Check auth status
    server.on("/api/auth/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        bool authenticated = checkAuth(request);
        JsonDocument doc;
        doc["authenticated"] = authenticated;
        doc["configured"] = appConfig.configured;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // API: Get firmware version and board type
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json",
            "{\"version\":\"" FIRMWARE_VERSION "\",\"board\":\"" BOARD_TYPE "\"}");
    });

    // API: Get certificate
    server.on("/api/cert", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        String cert = loadCertificate();
        JsonDocument doc;
        doc["certificate"] = cert;
        doc["hasCertificate"] = cert.length() > 50;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // API: Save certificate
    server.on("/api/cert", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!checkAuth(request)) { sendUnauthorized(request); return; }

            static String body;
            if (index == 0) body = "";
            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }
            if (index + len == total) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);
                if (!error && doc["certificate"].is<const char*>()) {
                    String cert = doc["certificate"].as<String>();
                    if (saveCertificate(cert)) {
                        request->send(200, "application/json", "{\"success\":true}");
                    } else {
                        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save certificate\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
                }
                body = "";
            }
        }
    );

    // API: Fetch certificate from host (without verification)
    server.on("/api/fetchcert", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }

        String host = appConfig.unifiHost;
        if (host.length() == 0) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"No UniFi host configured\"}");
            return;
        }

        log("WebServer: Fetching certificate from " + host);

        WiFiClientSecure client;
        client.setInsecure(); // Don't verify - we're fetching the cert

        if (!client.connect(host.c_str(), 443)) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect to host\"}");
            return;
        }

        // Get the peer certificate
        const mbedtls_x509_crt* cert = client.getPeerCertificate();
        if (!cert) {
            client.stop();
            request->send(500, "application/json", "{\"success\":false,\"message\":\"No certificate received\"}");
            return;
        }

        // Convert DER to PEM format manually using base64
        size_t derLen = cert->raw.len;
        size_t b64Len = 0;

        // Calculate base64 output size
        mbedtls_base64_encode(NULL, 0, &b64Len, cert->raw.p, derLen);

        char* b64Buffer = (char*)malloc(b64Len + 1);
        if (!b64Buffer) {
            client.stop();
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
            return;
        }

        size_t actualLen = 0;
        int ret = mbedtls_base64_encode((unsigned char*)b64Buffer, b64Len, &actualLen, cert->raw.p, derLen);
        client.stop();

        if (ret != 0) {
            free(b64Buffer);
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to encode certificate\"}");
            return;
        }

        // Build PEM string with line breaks every 64 chars
        String pem = "-----BEGIN CERTIFICATE-----\n";
        for (size_t i = 0; i < actualLen; i += 64) {
            size_t lineLen = (actualLen - i < 64) ? (actualLen - i) : 64;
            pem += String(b64Buffer + i).substring(0, lineLen);
            pem += "\n";
        }
        pem += "-----END CERTIFICATE-----\n";

        free(b64Buffer);

        // Return the certificate
        JsonDocument doc;
        doc["success"] = true;
        doc["certificate"] = pem;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);

        log("WebServer: Certificate fetched successfully (" + String(pem.length()) + " bytes)");
    });

    // API: Test UniFi connection
    server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }

        if (!hasUnifiCredentials()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"No UniFi credentials configured\"}");
            return;
        }

        log("WebServer: Testing UniFi connection...");

        // Try to login
        if (unifiLogin()) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Connection successful!\"}");
        } else {
            request->send(200, "application/json", "{\"success\":false,\"message\":\"Login failed. Check credentials and certificate.\"}");
        }
    });

    // API: Get UniFi device topology
    server.on("/api/topology", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }

        if (!isLoggedIn) {
            // Try to login first
            if (!unifiLogin()) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Not connected to UniFi\"}");
                return;
            }
        }

        String topology = unifiGetTopology();
        request->send(200, "application/json", topology);
    });

    // API: Get configuration (passwords masked)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        String json = getConfigJson(true);
        request->send(200, "application/json", json);
    });

    // API: Update configuration
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!checkAuth(request)) { sendUnauthorized(request); return; }

            // Collect body
            static String body;
            if (index == 0) body = "";
            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }

            if (index + len == total) {
                // Complete body received
                if (updateConfigFromJson(body)) {
                    request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved. Reboot to apply.\"}");
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid configuration\"}");
                }
                body = "";
            }
        }
    );

    // API: Get system status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        String json = getStatusJson();
        request->send(200, "application/json", json);
    });

    // API: Trigger doorbell ring
    server.on("/api/control/ring", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        if (unifiTriggerRing()) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Ring failed\"}");
        }
    });

    // API: Dismiss active call
    server.on("/api/control/dismiss", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        if (activeRequestId.length() > 0) {
            if (unifiDismissCall(activeDeviceId, activeRequestId)) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Dismiss failed\"}");
            }
        } else {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"No active call\"}");
        }
    });

    // API: Reboot device
    server.on("/api/control/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
        delay(500);
        ESP.restart();
    });

    // API: Reset configuration
    server.on("/api/control/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!checkAuth(request)) { sendUnauthorized(request); return; }
        resetConfig();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration reset. Rebooting...\"}");
        delay(500);
        ESP.restart();
    });

    // API: OTA firmware upload
    server.on("/api/ota/upload", HTTP_POST,
        // Request handler (called when upload completes)
        [](AsyncWebServerRequest* request) {
            if (!checkAuth(request)) { sendUnauthorized(request); return; }

            bool success = !Update.hasError();
            if (success) {
                log("OTA: Sending success response...");
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Update complete, rebooting...\"}");
                delay(500);  // Give time for response to be sent
                log("OTA: Rebooting...");
                Serial.flush();
                ESP.restart();
            } else {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Update failed\"}");
            }
        },
        // File upload handler (called for each chunk)
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!checkAuth(request)) { return; }

            if (index == 0) {
                log("OTA: Starting update: " + filename);
                // Start update with max available size
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    log("OTA: Update.begin failed");
                    Update.printError(Serial);
                }
            }

            if (Update.isRunning()) {
                if (Update.write(data, len) != len) {
                    log("OTA: Write failed");
                    Update.printError(Serial);
                }
            }

            if (final) {
                if (Update.end(true)) {
                    log("OTA: Update complete, size: " + String(index + len));
                    // Don't restart here - let the request handler send response first
                } else {
                    log("OTA: Update.end failed");
                    Update.printError(Serial);
                }
            }
        }
    );

    // API: OTA filesystem upload
    server.on("/api/ota/filesystem", HTTP_POST,
        // Request handler (called when upload completes)
        [](AsyncWebServerRequest* request) {
            if (!checkAuth(request)) { sendUnauthorized(request); return; }

            bool success = !Update.hasError();
            if (success) {
                log("OTA: Sending success response...");
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Filesystem update complete, rebooting...\"}");
                delay(500);  // Give time for response to be sent
                log("OTA: Rebooting...");
                Serial.flush();
                ESP.restart();
            } else {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Filesystem update failed\"}");
            }
        },
        // File upload handler (called for each chunk)
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!checkAuth(request)) { return; }

            if (index == 0) {
                log("OTA: Starting filesystem update: " + filename);
                // Start filesystem update
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                    log("OTA: Filesystem Update.begin failed");
                    Update.printError(Serial);
                }
            }

            if (Update.isRunning()) {
                if (Update.write(data, len) != len) {
                    log("OTA: Filesystem write failed");
                    Update.printError(Serial);
                }
            }

            if (final) {
                if (Update.end(true)) {
                    log("OTA: Filesystem update complete, size: " + String(index + len));
                    // Don't restart here - let the request handler send response first
                } else {
                    log("OTA: Filesystem Update.end failed");
                    Update.printError(Serial);
                }
            }
        }
    );

    // Serve static files from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    log("WebServer: Started on port 80");
}

void webServerLoop() {
    // Clean up disconnected WebSocket clients
    ws.cleanupClients();

    // Periodic status broadcast
    unsigned long now = millis();
    if (now - lastStatusBroadcast > STATUS_BROADCAST_INTERVAL) {
        lastStatusBroadcast = now;
        if (ws.count() > 0) {
            broadcastStatus();
        }
    }
}

void broadcastStatus() {
    if (ws.count() == 0) return;

    String json = getStatusJson();
    ws.textAll(json);
}

void broadcastDoorbellEvent(const String& event, const String& requestId, const String& deviceId) {
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "doorbell";
    doc["event"] = event;
    if (requestId.length() > 0) {
        doc["requestId"] = requestId;
    }
    if (deviceId.length() > 0) {
        doc["deviceId"] = deviceId;
    }

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void broadcastLog(const String& timestamp, const String& message) {
    // Publish to MQTT
    publishMqttLog(timestamp + " " + message);

    // Broadcast via WebSocket
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "log";
    doc["timestamp"] = timestamp;
    doc["message"] = message;

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void broadcastLogLocal(const String& timestamp, const String& message) {
    // WebSocket only - no MQTT (for debug/verbose logging)
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "log";
    doc["timestamp"] = timestamp;
    doc["message"] = message;

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            log("WebSocket client connected: " + String(client->id()));
            // Send initial status
            client->text(getStatusJson());
            break;

        case WS_EVT_DISCONNECT:
            log("WebSocket client disconnected: " + String(client->id()));
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // Complete text message
                data[len] = 0;
                String msg = String((char*)data);

                // Handle ping
                if (msg == "ping") {
                    client->text("pong");
                }
            }
            break;
        }

        case WS_EVT_ERROR:
            log("WebSocket error: " + String(client->id()));
            break;

        case WS_EVT_PONG:
            break;
    }
}

static String getStatusJson() {
    JsonDocument doc;
    doc["type"] = "status";

    // System info
    doc["system"]["heap"] = ESP.getFreeHeap();
    doc["system"]["heapMin"] = ESP.getMinFreeHeap();
    doc["system"]["heapTotal"] = ESP.getHeapSize();
    doc["system"]["uptime"] = millis() / 1000;
    doc["system"]["cpuMhz"] = ESP.getCpuFreqMHz();

    // Network
    doc["network"]["connected"] = networkConnected;
    #if defined(USE_ETHERNET)
        doc["network"]["type"] = "ethernet";
        if (networkConnected) {
            doc["network"]["ip"] = ETH.localIP().toString();
        }
    #else
        doc["network"]["type"] = "wifi";
        if (networkConnected) {
            doc["network"]["ip"] = WiFi.localIP().toString();
        }
    #endif

    // UniFi
    doc["unifi"]["configured"] = hasUnifiCredentials();
    doc["unifi"]["loggedIn"] = isLoggedIn;
    doc["unifi"]["wsConnected"] = (bool)wsConnected;
    doc["unifi"]["wsReconnects"] = getWsReconnectCount();

    // Include error message if there's a connection problem
    if (!isLoggedIn && unifiLastError.length() > 0) {
        doc["unifi"]["error"] = unifiLastError;
    } else if (!wsConnected && wsLastError.length() > 0) {
        doc["unifi"]["error"] = wsLastError;
    }

    // MQTT
    doc["mqtt"]["connected"] = mqtt.connected();

    // Doorbell
    doc["doorbell"]["active"] = activeRequestId.length() > 0;
    if (activeRequestId.length() > 0) {
        doc["doorbell"]["requestId"] = activeRequestId;
        doc["doorbell"]["deviceId"] = activeDeviceId;
        doc["doorbell"]["duration"] = (millis() - activeCallTime) / 1000;
    }

    // Config
    doc["configured"] = appConfig.configured;

    // GPIO states
    JsonArray gpios = doc["gpios"].to<JsonArray>();
    for (int i = 0; i < appConfig.gpioCount; i++) {
        JsonObject gpio = gpios.add<JsonObject>();
        gpio["pin"] = appConfig.gpios[i].pin;
        gpio["label"] = appConfig.gpios[i].label;
        gpio["state"] = getGpioStateString(i);
        switch (appConfig.gpios[i].action) {
            case GPIO_ACTION_RING_BUTTON: gpio["action"] = "ring_button"; break;
            case GPIO_ACTION_DOOR_CONTACT: gpio["action"] = "door_contact"; break;
            case GPIO_ACTION_GENERIC: gpio["action"] = "generic"; break;
            default: gpio["action"] = "none"; break;
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}
