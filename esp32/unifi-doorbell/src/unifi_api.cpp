#include "unifi_api.h"
#include "config_manager.h"
#include "logging.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// =============================================================================
// ChunkedStream - Wrapper to transparently handle HTTP chunked transfer encoding
// =============================================================================
class ChunkedStream : public Stream {
private:
  WiFiClientSecure& client;
  bool isChunked;
  int remainingInChunk;
  bool finished;
  bool needChunkSize;
  unsigned long timeout;
  int yieldCounter;

  bool waitForData(unsigned long maxWait) {
    unsigned long start = millis();
    while (!client.available() && client.connected() && millis() - start < maxWait) {
      delay(1);
      if (++yieldCounter % 50 == 0) yield();
    }
    return client.available() > 0;
  }

  bool readNextChunkSize() {
    if (!waitForData(5000)) {
      finished = true;
      return false;
    }

    // Read chunk size line (hex number followed by CRLF)
    String chunkLine = "";
    unsigned long start = millis();
    while (millis() - start < 2000) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') break;
        if (c != '\r') chunkLine += c;
      } else {
        delay(1);
      }
    }

    remainingInChunk = (int)strtol(chunkLine.c_str(), NULL, 16);
    needChunkSize = false;

    if (remainingInChunk == 0) {
      finished = true;
      return false;
    }
    return true;
  }

  void skipChunkTrailer() {
    // Skip CRLF after chunk data
    unsigned long start = millis();
    int crlfCount = 0;
    while (crlfCount < 2 && millis() - start < 1000) {
      if (client.available()) {
        char c = client.read();
        if (c == '\r' || c == '\n') crlfCount++;
        else break;  // Unexpected char, stop
      } else {
        delay(1);
      }
    }
    needChunkSize = true;
  }

public:
  ChunkedStream(WiFiClientSecure& c, bool chunked)
    : client(c), isChunked(chunked), remainingInChunk(0), finished(false),
      needChunkSize(true), yieldCounter(0) {
    timeout = millis() + 60000;  // 60 second timeout
  }

  int available() override {
    if (finished || millis() > timeout) return 0;

    if (!isChunked) {
      return client.available();
    }

    // Need to read next chunk size?
    if (needChunkSize) {
      if (!readNextChunkSize()) return 0;
    }

    return min(remainingInChunk, (int)client.available());
  }

  int read() override {
    if (finished || millis() > timeout) return -1;

    if (!isChunked) {
      return client.available() ? client.read() : -1;
    }

    // Ensure we have a valid chunk
    if (needChunkSize) {
      if (!readNextChunkSize()) return -1;
    }

    if (remainingInChunk <= 0) return -1;

    // Wait for data if needed
    if (!client.available()) {
      if (!waitForData(2000)) return -1;
    }

    int c = client.read();
    if (c >= 0) {
      remainingInChunk--;
      if (remainingInChunk == 0) {
        skipChunkTrailer();
      }
    }
    return c;
  }

  int peek() override {
    if (available() == 0) return -1;
    return client.peek();
  }

  size_t readBytes(char* buffer, size_t length) override {
    size_t totalRead = 0;

    while (totalRead < length && !finished && millis() < timeout) {
      if (!isChunked) {
        // Non-chunked: read directly
        if (!client.available()) {
          if (!waitForData(2000)) break;
        }
        size_t bytesRead = client.readBytes(buffer + totalRead, length - totalRead);
        totalRead += bytesRead;
        if (bytesRead == 0) break;
      } else {
        // Chunked: respect chunk boundaries
        if (needChunkSize) {
          if (!readNextChunkSize()) break;
        }

        if (remainingInChunk <= 0) break;

        // Wait for data
        if (!client.available()) {
          if (!waitForData(2000)) break;
        }

        size_t toRead = min((size_t)remainingInChunk, length - totalRead);
        toRead = min(toRead, (size_t)client.available());

        if (toRead > 0) {
          size_t bytesRead = client.readBytes(buffer + totalRead, toRead);
          totalRead += bytesRead;
          remainingInChunk -= bytesRead;

          if (remainingInChunk == 0) {
            skipChunkTrailer();
          }
        }
      }

      // Yield periodically
      if (++yieldCounter % 20 == 0) yield();
    }

    return totalRead;
  }

  size_t write(uint8_t) override { return 0; }  // Not used

  bool isFinished() { return finished; }
};

// Legacy context state
String csrfToken = "";
String sessionCookie = "";
String userId = "";
String userName = "";
bool isLoggedIn = false;
String unifiLastError = "";

// Developer-API context state
bool developerApiReady = false;

// Resolved device IDs
String resolvedDoorbellDeviceId = "";
String resolvedViewerIds[4];
int resolvedViewerCount = 0;

// Internal helper functions (legacy context)
static String readHttpResponse(WiFiClientSecure& client);
static String extractHeader(const String& response, const String& headerName);
static String extractCookie(const String& response, const String& cookieName);

// =============================================================================
// Internal helper for auth failure detection
// =============================================================================

static bool isAuthFailure(int statusCode) {
  return statusCode == 401 || statusCode == 403;
}

static void handleAuthFailure(int statusCode) {
  log("UniFi: Auth failure (HTTP " + String(statusCode) + "), forcing re-login");
  isLoggedIn = false;
  sessionCookie = "";
  csrfToken = "";
  unifiLastError = "Session expired";
}

// =============================================================================
// Public API Functions
// =============================================================================

void forceRelogin() {
  log("UniFi: Forcing re-login...");
  isLoggedIn = false;
  sessionCookie = "";
  csrfToken = "";
}

bool unifiLogin() {
  unifiLastError = "";  // Clear previous error

  if (!hasUnifiCredentials()) {
    log("UniFi: No credentials configured");
    return false;
  }

  log("UniFi: Logging in to " + String(appConfig.unifiHost) + "...");

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(appConfig.unifiHost, 443)) {
    log("UniFi: Connection failed");
    unifiLastError = "Connection failed";
    return false;
  }

  // Step 1: Get initial CSRF token
  client.println("GET / HTTP/1.1");
  client.println("Host: " + String(appConfig.unifiHost));
  client.println("Connection: keep-alive");
  client.println();

  String response = readHttpResponse(client);
  csrfToken = extractHeader(response, "X-Csrf-Token");
  log("UniFi: Got initial CSRF token");

  client.stop();
  delay(100);

  // Step 2: Login
  if (!client.connect(appConfig.unifiHost, 443)) {
    log("UniFi: Reconnection failed");
    unifiLastError = "Reconnection failed";
    return false;
  }

  JsonDocument loginDoc;
  loginDoc["username"] = appConfig.unifiUsername;
  loginDoc["password"] = appConfig.unifiPassword;
  loginDoc["token"] = "";
  loginDoc["rememberMe"] = true;

  String loginBody;
  serializeJson(loginDoc, loginBody);

  client.println("POST /api/auth/login HTTP/1.1");
  client.println("Host: " + String(appConfig.unifiHost));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(loginBody.length()));
  if (csrfToken.length() > 0) {
    client.println("X-Csrf-Token: " + csrfToken);
  }
  client.println("Connection: close");
  client.println();
  client.print(loginBody);

  response = readHttpResponse(client);

  // Extract new CSRF token
  String newToken = extractHeader(response, "X-Updated-Csrf-Token");
  if (newToken.length() == 0) {
    newToken = extractHeader(response, "X-Csrf-Token");
  }
  if (newToken.length() > 0) {
    csrfToken = newToken;
  }

  // Extract session cookie
  sessionCookie = extractCookie(response, "TOKEN");

  client.stop();

  if (sessionCookie.length() > 0) {
    log("UniFi: Login successful");
    userId = appConfig.unifiUsername;
    userName = appConfig.unifiUsername;
    isLoggedIn = true;
    return true;
  }

  log("UniFi: Login failed - no session cookie");
  unifiLastError = "Login failed";
  return false;
}

bool unifiBootstrap() {
  log("UniFi: Resolving device IDs...");

  // Reset viewer count for re-bootstrap
  resolvedViewerCount = 0;

  // The developer API returns IDs in the same form we use to call it back —
  // no MAC-style normalization needed. Whatever was stored is used verbatim.
  resolvedDoorbellDeviceId = appConfig.doorbellDeviceId;
  if (resolvedDoorbellDeviceId.length() > 0) {
    log("UniFi: Doorbell ID: " + resolvedDoorbellDeviceId);
  }

  // Viewer IDs are part of the legacy `notify_door_guards` payload and are no
  // longer used by the official trigger endpoint, but we keep populating the
  // list in case the rest of the codebase (status / WebSocket consumers)
  // still references it.
  for (int i = 0; i < appConfig.viewerCount && i < CFG_MAX_VIEWERS; i++) {
    String viewerId = appConfig.viewerIds[i];
    if (viewerId.length() == 0) continue;
    resolvedViewerIds[resolvedViewerCount++] = viewerId;
    log("UniFi: Viewer ID: " + viewerId);
  }

  log("UniFi: Bootstrap complete - " + String(resolvedViewerCount) + " viewers configured");
  return true;
}

bool unifiDismissCall(const String& deviceId, const String& requestId) {
  if (!isLoggedIn || requestId.length() == 0) {
    log("UniFi: Cannot dismiss - not logged in or no request ID");
    return false;
  }

  log("UniFi: Dismissing doorbell call: " + requestId);

  JsonDocument doc;
  doc["device_id"] = deviceId;
  doc["response"] = "denied";
  doc["request_id"] = requestId;
  doc["user_id"] = userId;
  doc["user_name"] = userName;

  String body;
  serializeJson(doc, body);

  String path = "/proxy/access/api/v2/device/" + deviceId + "/reply_remote";

  WiFiClientSecure apiClient;
  apiClient.setInsecure();
  apiClient.setTimeout(10000);

  if (!apiClient.connect(appConfig.unifiHost, 443)) {
    log("UniFi: Connection failed");
    return false;
  }

  apiClient.println("POST " + path + " HTTP/1.1");
  apiClient.println("Host: " + String(appConfig.unifiHost));
  apiClient.println("Content-Type: application/json");
  apiClient.println("Content-Length: " + String(body.length()));
  apiClient.println("X-Csrf-Token: " + csrfToken);
  apiClient.println("Cookie: TOKEN=" + sessionCookie);
  apiClient.println("Connection: close");
  apiClient.println();
  apiClient.print(body);

  String statusLine = apiClient.readStringUntil('\n');
  int statusCode = 0;
  int spaceIdx = statusLine.indexOf(' ');
  if (spaceIdx > 0) {
    int endIdx = statusLine.indexOf(' ', spaceIdx + 1);
    if (endIdx > 0) {
      statusCode = statusLine.substring(spaceIdx + 1, endIdx).toInt();
    }
  }

  while (apiClient.available()) {
    apiClient.read();
  }
  apiClient.stop();

  bool success = (statusCode >= 200 && statusCode < 300);
  if (success) {
    log("UniFi: Doorbell call dismissed");
  } else {
    log("UniFi: Dismiss failed, status: " + String(statusCode));
    if (isAuthFailure(statusCode)) {
      handleAuthFailure(statusCode);
    }
  }

  return success;
}

// =============================================================================
// Developer-API context (Bearer token, port unifiPort)
//
// All requests go through sendDeveloperRequest(). It opens a fresh
// WiFiClientSecure to <host>:<unifiPort>, sends the Bearer token, and parses
// the JSON response (optionally with a filter for large payloads).
// It MUST NOT touch csrfToken / sessionCookie / isLoggedIn — those belong to
// the legacy context.
// =============================================================================

// Returns true if the request succeeded (HTTP 2xx and JSON parsed).
// The parsed body is written to outDoc; the HTTP status is written to outStatus.
// `filter` (optional) reduces memory by extracting only the listed fields.
static bool sendDeveloperRequest(const char* method,
                                 const String& path,
                                 const String& body,
                                 JsonDocument& outDoc,
                                 int& outStatus,
                                 const JsonDocument* filter = nullptr) {
  outStatus = 0;
  outDoc.clear();

  if (strlen(appConfig.unifiApiToken) == 0) {
    log("UniFi[dev]: API token not configured");
    unifiLastError = "API token not configured";
    return false;
  }

  uint16_t port = appConfig.unifiPort > 0 ? appConfig.unifiPort : 12445;

  WiFiClientSecure apiClient;
  apiClient.setInsecure();
  apiClient.setTimeout(15000);

  if (!apiClient.connect(appConfig.unifiHost, port)) {
    log(String("UniFi[dev]: ") + method + " " + path + " - connect to " +
        String(appConfig.unifiHost) + ":" + String(port) + " failed");
    unifiLastError = "Cannot reach controller (developer API)";
    return false;
  }

  apiClient.println(String(method) + " " + path + " HTTP/1.1");
  apiClient.println("Host: " + String(appConfig.unifiHost) + ":" + String(port));
  apiClient.println("Authorization: Bearer " + String(appConfig.unifiApiToken));
  apiClient.println("Accept: application/json");
  if (body.length() > 0) {
    apiClient.println("Content-Type: application/json");
    apiClient.println("Content-Length: " + String(body.length()));
  }
  apiClient.println("Connection: close");
  apiClient.println();
  if (body.length() > 0) {
    apiClient.print(body);
  }

  // Parse status line
  String statusLine = apiClient.readStringUntil('\n');
  {
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx > 0) {
      int endIdx = statusLine.indexOf(' ', spaceIdx + 1);
      if (endIdx > 0) {
        outStatus = statusLine.substring(spaceIdx + 1, endIdx).toInt();
      }
    }
  }

  // Walk headers, watch for chunked transfer encoding
  bool isChunked = false;
  unsigned long headerDeadline = millis() + 5000;
  while (millis() < headerDeadline && apiClient.connected()) {
    String line = apiClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
    String lower = line; lower.toLowerCase();
    if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") > 0) {
      isChunked = true;
    }
  }

  // Parse JSON body
  ChunkedStream stream(apiClient, isChunked);
  DeserializationError err;
  if (filter) {
    err = deserializeJson(outDoc, stream,
                          DeserializationOption::Filter(*filter),
                          DeserializationOption::NestingLimit(20));
  } else {
    err = deserializeJson(outDoc, stream,
                          DeserializationOption::NestingLimit(20));
  }
  apiClient.stop();

  // Diagnostic: method + path + status + JSON code (if present)
  const char* code = outDoc["code"] | "";
  log(String("UniFi[dev]: ") + method + " " + path +
      " -> HTTP " + String(outStatus) +
      (strlen(code) > 0 ? (String(" code=") + code) : String("")));

  if (err) {
    log(String("UniFi[dev]: parse error: ") + err.c_str());
    if (outStatus < 200 || outStatus >= 300) {
      // status was already bad; treat as overall failure
    }
    return false;
  }

  if (outStatus >= 200 && outStatus < 300) {
    return true;
  }

  // Map known error codes from the JSON body to a user-friendly message.
  String codeStr = String(code);
  if (codeStr == "CODE_AUTH_FAILED" || codeStr == "CODE_ACCESS_TOKEN_INVALID" || outStatus == 401) {
    unifiLastError = "Token rejected";
  } else if (codeStr == "CODE_DEVICE_API_NOT_SUPPORTED" || outStatus == 404) {
    unifiLastError = "Controller does not support developer API (requires UniFi Access 4.0.10+)";
  } else if (codeStr == "CODE_DEVICE_DEVICE_OFFLINE") {
    unifiLastError = "Reader is offline";
  } else if (codeStr == "CODE_DEVICE_DEVICE_NOT_FOUND") {
    unifiLastError = "Reader not found on controller";
  } else if (outStatus == 403) {
    unifiLastError = "Token lacks required permission";
  } else if (codeStr.length() > 0) {
    unifiLastError = String("Developer API error: ") + codeStr;
  } else {
    unifiLastError = String("Developer API HTTP ") + String(outStatus);
  }
  return false;
}

String unifiGetReaders() {
  if (!hasUnifiApiToken()) {
    return "{\"success\":false,\"message\":\"API token not configured\"}";
  }

  // Filter: only fields we render. The controller actually returns far more
  // (capabilities, location_id, connected_uah_id, etc.) — capabilities is the
  // one we MUST keep because it drives the doorbell-capable filter.
  JsonDocument filter;
  filter["data"][0][0]["id"] = true;
  filter["data"][0][0]["name"] = true;
  filter["data"][0][0]["alias"] = true;
  filter["data"][0][0]["type"] = true;
  filter["data"][0][0]["is_online"] = true;
  filter["data"][0][0]["capabilities"] = true;

  const int maxRetries = 3;
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    log("UniFi[dev]: Fetching device list (attempt " + String(attempt) + "/" + String(maxRetries) + ")");

    JsonDocument inputDoc;
    int httpStatus = 0;
    bool ok = sendDeveloperRequest("GET",
                                   "/api/v1/developer/devices?refresh=true",
                                   "", inputDoc, httpStatus, &filter);

    if (!ok) {
      if (attempt < maxRetries) { delay(1000); continue; }
      String errMsg = unifiLastError.length() > 0 ? unifiLastError : "Failed to load devices";
      JsonDocument out;
      out["success"] = false;
      out["message"] = errMsg;
      out["canRetry"] = true;
      String s; serializeJson(out, s);
      developerApiReady = false;
      return s;
    }

    // Build response: filter to devices whose `capabilities` array contains "remote_call".
    JsonDocument outputDoc;
    outputDoc["success"] = true;
    JsonArray readers = outputDoc["readers"].to<JsonArray>();

    int totalDevices = 0;
    JsonArray dataArray = inputDoc["data"];
    for (JsonArray group : dataArray) {
      for (JsonObject device : group) {
        totalDevices++;

        const char* deviceId = device["id"] | "";
        if (strlen(deviceId) == 0) continue;

        bool canRing = false;
        JsonArray caps = device["capabilities"];
        for (JsonVariant cap : caps) {
          const char* s = cap.as<const char*>();
          if (s && strcmp(s, "remote_call") == 0) { canRing = true; break; }
        }
        if (!canRing) continue;

        JsonObject reader = readers.add<JsonObject>();
        reader["id"] = deviceId;
        reader["name"] = device["name"] | "";
        reader["alias"] = device["alias"] | "";
        reader["type"] = device["type"] | "";
        // is_online may be absent on older firmware — default true so a missing
        // field doesn't accidentally grey out a working reader.
        reader["is_online"] = device["is_online"] | true;
      }
    }

    log("UniFi[dev]: " + String(totalDevices) + " devices, " +
        String(readers.size()) + " doorbell-capable readers");
    developerApiReady = true;

    String output;
    serializeJson(outputDoc, output);
    return output;
  }

  // Should not reach here.
  return "{\"success\":false,\"message\":\"Unexpected error\",\"canRetry\":true}";
}

bool unifiTriggerRing() {
  if (!hasUnifiApiToken()) {
    log("UniFi[dev]: Cannot trigger - API token not configured");
    unifiLastError = "API token not configured";
    return false;
  }

  String deviceId = resolvedDoorbellDeviceId;
  if (deviceId.length() == 0) {
    deviceId = appConfig.doorbellDeviceId;
  }
  if (deviceId.length() == 0) {
    log("UniFi[dev]: Cannot trigger - no doorbell device id configured");
    unifiLastError = "Doorbell device not configured";
    return false;
  }

  log("UniFi[dev]: Triggering doorbell on device: " + deviceId);

  // Always send cancel:true so a stale ring (from a failed previous trigger)
  // is killed before the fresh one starts. Empirically the controller rings
  // a fresh call regardless of whether one was active.
  static const String body = "{\"cancel\":true}";
  String path = "/api/v1/developer/devices/" + deviceId + "/doorbell";

  JsonDocument resp;
  int httpStatus = 0;
  bool ok = sendDeveloperRequest("POST", path, body, resp, httpStatus);

  if (ok) {
    log("UniFi[dev]: Doorbell ring triggered");
    developerApiReady = true;
    return true;
  }

  log("UniFi[dev]: Trigger failed: " + unifiLastError);
  return false;
}

// =============================================================================
// Helper Functions
// =============================================================================

String generateRandomString(int length) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String result = "";
  for (int i = 0; i < length; i++) {
    result += charset[random(0, sizeof(charset) - 1)];
  }
  return result;
}

String generateUUID() {
  String uuid = "";
  for (int i = 0; i < 32; i++) {
    if (i == 8 || i == 12 || i == 16 || i == 20) uuid += "-";
    uuid += String(random(0, 16), HEX);
  }
  return uuid;
}

// =============================================================================
// Internal HTTP Helper Functions
// =============================================================================

static String readHttpResponse(WiFiClientSecure& client) {
  String response = "";
  unsigned long timeout = millis() + 10000;

  while (millis() < timeout) {
    while (client.available()) {
      response += (char)client.read();
    }
    if (response.indexOf("\r\n\r\n") > 0 && !client.available()) {
      delay(100);
      while (client.available()) {
        response += (char)client.read();
      }
      break;
    }
    delay(10);
  }

  return response;
}

static String extractHeader(const String& response, const String& headerName) {
  String search = "\r\n" + headerName + ": ";
  int idx = response.indexOf(search);
  if (idx < 0) {
    String lowerResp = response;
    String lowerSearch = search;
    lowerResp.toLowerCase();
    lowerSearch.toLowerCase();
    idx = lowerResp.indexOf(lowerSearch);
  }
  if (idx < 0) return "";

  int start = idx + search.length();
  int end = response.indexOf("\r\n", start);
  if (end < 0) return "";

  return response.substring(start, end);
}

static String extractCookie(const String& response, const String& cookieName) {
  String search = cookieName + "=";
  int idx = response.indexOf(search);
  if (idx < 0) return "";

  int start = idx + search.length();
  int end = response.indexOf(";", start);
  if (end < 0) end = response.indexOf("\r\n", start);
  if (end < 0) return "";

  return response.substring(start, end);
}
