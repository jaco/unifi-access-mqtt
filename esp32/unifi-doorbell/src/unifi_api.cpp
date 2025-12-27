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

// Session state
String csrfToken = "";
String sessionCookie = "";
String userId = "";
String userName = "";
bool isLoggedIn = false;
String unifiLastError = "";

// Resolved device IDs
String resolvedDoorbellDeviceId = "";
String resolvedViewerIds[4];
int resolvedViewerCount = 0;

// Internal helper functions
static String readHttpResponse(WiFiClientSecure& client);
static int extractStatusCode(const String& response);
static String extractHeader(const String& response, const String& headerName);
static String extractCookie(const String& response, const String& cookieName);

// =============================================================================
// Public API Functions
// =============================================================================

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

  // Resolve doorbell device ID
  String configuredId = appConfig.doorbellDeviceId;

  if (configuredId.indexOf(":") >= 0 || configuredId.indexOf("-") >= 0) {
    resolvedDoorbellDeviceId = normalizeMAC(configuredId);
    log("UniFi: Doorbell MAC " + configuredId + " -> ID " + resolvedDoorbellDeviceId);
  } else {
    resolvedDoorbellDeviceId = configuredId;
    log("UniFi: Doorbell ID: " + resolvedDoorbellDeviceId);
  }

  // Resolve viewer IDs from config
  for (int i = 0; i < appConfig.viewerCount && i < CFG_MAX_VIEWERS; i++) {
    String viewerId = appConfig.viewerIds[i];
    if (viewerId.length() == 0) continue;

    if (viewerId.indexOf(":") >= 0 || viewerId.indexOf("-") >= 0) {
      resolvedViewerIds[resolvedViewerCount++] = normalizeMAC(viewerId);
      log("UniFi: Viewer MAC " + viewerId + " -> ID " + resolvedViewerIds[resolvedViewerCount-1]);
    } else {
      resolvedViewerIds[resolvedViewerCount++] = viewerId;
      log("UniFi: Viewer ID: " + viewerId);
    }
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
  }

  return success;
}

// Streaming topology fetch - parses directly from HTTP stream with filtering
// This uses minimal memory by only extracting the fields we need
static bool fetchTopologyStreaming(JsonDocument& outputDoc) {
  WiFiClientSecure apiClient;
  apiClient.setInsecure();
  apiClient.setTimeout(30000);

  log("UniFi: Heap before fetch: " + String(ESP.getFreeHeap()));

  if (!apiClient.connect(appConfig.unifiHost, 443)) {
    log("UniFi: Connection failed");
    return false;
  }

  String path = "/proxy/access/api/v2/devices/topology4";

  apiClient.println("GET " + path + " HTTP/1.1");
  apiClient.println("Host: " + String(appConfig.unifiHost));
  apiClient.println("Accept: application/json");
  apiClient.println("X-Csrf-Token: " + csrfToken);
  apiClient.println("Cookie: TOKEN=" + sessionCookie);
  apiClient.println("Connection: close");
  apiClient.println();

  unsigned long timeout = millis() + 30000;
  bool isChunked = false;
  int httpStatus = 0;

  // Read status line
  if (apiClient.connected()) {
    String statusLine = apiClient.readStringUntil('\n');
    log("UniFi: Status: " + statusLine);
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx > 0) {
      int endIdx = statusLine.indexOf(' ', spaceIdx + 1);
      if (endIdx > 0) {
        httpStatus = statusLine.substring(spaceIdx + 1, endIdx).toInt();
      }
    }
  }

  // Read headers
  while (millis() < timeout && apiClient.connected()) {
    String line = apiClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;

    String lowerLine = line;
    lowerLine.toLowerCase();
    if (lowerLine.startsWith("transfer-encoding:") && lowerLine.indexOf("chunked") > 0) {
      isChunked = true;
    }
  }

  if (httpStatus >= 400) {
    apiClient.stop();
    log("UniFi: HTTP error " + String(httpStatus));
    return false;
  }

  // Create filter to only extract fields we need - drastically reduces memory usage
  // Structure: data[] -> floors[] -> doors[] -> device_groups[][]
  // Only fetch reader devices (viewers are ignored by UniFi API)
  JsonDocument filter;
  filter["data"][0]["floors"][0]["name"] = true;
  filter["data"][0]["floors"][0]["doors"][0]["name"] = true;
  filter["data"][0]["floors"][0]["doors"][0]["device_groups"][0][0]["device_type"] = true;
  filter["data"][0]["floors"][0]["doors"][0]["device_groups"][0][0]["unique_id"] = true;
  filter["data"][0]["floors"][0]["doors"][0]["device_groups"][0][0]["name"] = true;
  filter["data"][0]["floors"][0]["doors"][0]["device_groups"][0][0]["mac"] = true;

  log("UniFi: Parsing stream with filter (chunked=" + String(isChunked ? "yes" : "no") + ")");
  log("UniFi: Heap before parse: " + String(ESP.getFreeHeap()));

  // Wrap the client in ChunkedStream to handle chunked transfer encoding
  ChunkedStream stream(apiClient, isChunked);

  // Parse directly from stream with filter - only extracts specified fields
  JsonDocument inputDoc;
  DeserializationError error = deserializeJson(
    inputDoc,
    stream,
    DeserializationOption::Filter(filter),
    DeserializationOption::NestingLimit(30)
  );

  bool streamFinished = stream.isFinished();
  bool clientConnected = apiClient.connected();
  apiClient.stop();

  log("UniFi: Heap after parse: " + String(ESP.getFreeHeap()));
  log("UniFi: Stream finished: " + String(streamFinished ? "yes" : "no") +
             ", client connected: " + String(clientConnected ? "yes" : "no"));

  if (error) {
    log("UniFi: Parse error: " + String(error.c_str()));

    // Debug: show what was parsed
    String partial;
    serializeJson(inputDoc, partial);
    if (partial.length() > 200) {
      partial = partial.substring(0, 200) + "...";
    }
    log("UniFi: Partial data: " + partial);

    return false;
  }

  // Build output document with reader devices only
  outputDoc["success"] = true;
  JsonArray readers = outputDoc["readers"].to<JsonArray>();

  int deviceCount = 0;

  // Navigate: data[] -> floors[] -> doors[] -> device_groups[][]
  JsonArray dataArray = inputDoc["data"];
  for (JsonObject site : dataArray) {
    JsonArray floors = site["floors"];
    for (JsonObject floor : floors) {
      const char* floorName = floor["name"] | "";

      JsonArray doors = floor["doors"];
      for (JsonObject door : doors) {
        const char* doorName = door["name"] | "";

        JsonArray deviceGroups = door["device_groups"];
        for (JsonArray group : deviceGroups) {
          for (JsonObject device : group) {
            const char* deviceType = device["device_type"] | "";
            const char* deviceId = device["unique_id"] | "";
            const char* deviceName = device["name"] | "";
            const char* mac = device["mac"] | "";

            if (strlen(deviceId) == 0) continue;

            deviceCount++;

            // Check if it's a reader device (can be doorbell source)
            // Match UA-G2, UA-G3, and any device with "Reader" in the name
            bool isReader = (strstr(deviceType, "UA-G2") != nullptr ||
                             strstr(deviceType, "UA-G3") != nullptr ||
                             strstr(deviceType, "Reader") != nullptr);

            if (isReader) {
              // Build location string from floor/door names
              String location = String(floorName);
              if (strlen(doorName) > 0) {
                if (location.length() > 0) location += " / ";
                location += doorName;
              }

              JsonObject reader = readers.add<JsonObject>();
              reader["id"] = deviceId;
              reader["name"] = deviceName;
              reader["mac"] = mac;
              reader["type"] = deviceType;
              reader["location"] = location;
            }
          }
        }
      }
    }
  }

  log("UniFi: Found " + String(deviceCount) + " devices, " + String(readers.size()) + " readers");

  return true;
}

String unifiGetTopology() {
  if (!isLoggedIn) {
    log("UniFi: Cannot get topology - not logged in");
    return "{\"success\":false,\"message\":\"Not logged in\"}";
  }

  const int maxRetries = 3;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    log("UniFi: Fetching device topology (attempt " + String(attempt) + "/" + String(maxRetries) + ")...");

    JsonDocument outputDoc;
    bool success = fetchTopologyStreaming(outputDoc);

    if (!success) {
      log("UniFi: Attempt " + String(attempt) + " failed");
      if (attempt < maxRetries) {
        delay(1000);  // Wait before retry
        continue;
      }
      return "{\"success\":false,\"message\":\"Failed after " + String(maxRetries) + " attempts\",\"canRetry\":true}";
    }

    // Success - serialize and return
    log("UniFi: Successfully fetched topology on attempt " + String(attempt));

    String output;
    serializeJson(outputDoc, output);
    return output;
  }

  // Should not reach here, but return error just in case
  return "{\"success\":false,\"message\":\"Unexpected error\",\"canRetry\":true}";
}

bool unifiTriggerRing() {
  if (!isLoggedIn) {
    log("UniFi: Cannot trigger - not logged in");
    return false;
  }

  String deviceId = resolvedDoorbellDeviceId;
  if (deviceId.length() == 0) {
    deviceId = appConfig.doorbellDeviceId;
  }

  log("UniFi: Triggering doorbell ring on device: " + deviceId);

  String requestId = generateRandomString(32);
  String roomId = "PR-" + generateUUID();
  time_t now = time(nullptr);

  JsonDocument doc;
  doc["request_id"] = requestId;
  doc["agora_channel"] = roomId;
  doc["controller_id"] = deviceId;
  doc["device_id"] = deviceId;
  doc["device_name"] = appConfig.doorbellDeviceName;
  doc["door_name"] = appConfig.doorbellDoorName;
  doc["floor_name"] = "";
  doc["in_or_out"] = "in";
  doc["mode"] = "webrtc";
  doc["create_time_uid"] = now;
  doc["create_time"] = now;
  doc["room_id"] = roomId;

  JsonArray viewers = doc["notify_door_guards"].to<JsonArray>();
  for (int i = 0; i < resolvedViewerCount; i++) {
    viewers.add(resolvedViewerIds[i]);
  }

  String body;
  serializeJson(doc, body);

  log("UniFi: Viewers in notify list: " + String(resolvedViewerCount));

  String path = "/proxy/access/api/v2/device/" + deviceId + "/remote_call";

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
    log("UniFi: Doorbell ring triggered");
  } else {
    log("UniFi: Trigger failed, status: " + String(statusCode));
  }

  return success;
}

// =============================================================================
// Helper Functions
// =============================================================================

String normalizeMAC(const String& mac) {
  String normalized = mac;
  normalized.replace(":", "");
  normalized.replace("-", "");
  normalized.toLowerCase();
  return normalized;
}

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

static int extractStatusCode(const String& response) {
  int spaceIdx = response.indexOf(' ');
  if (spaceIdx < 0) return 0;
  int endIdx = response.indexOf(' ', spaceIdx + 1);
  if (endIdx < 0) return 0;
  return response.substring(spaceIdx + 1, endIdx).toInt();
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
