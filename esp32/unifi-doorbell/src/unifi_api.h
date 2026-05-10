#pragma once

#include <Arduino.h>

// =============================================================================
// Authentication contexts
//
// This module talks to the UniFi Access controller through TWO independent
// HTTP contexts. They share a host but nothing else.
//
//   1. Developer-API context (Bearer API token):
//        Endpoint: https://<unifiHost>:<unifiPort>/api/v1/developer/...
//        Used for: trigger ring (POST /devices/{id}/doorbell with cancel:true),
//                  device discovery (GET /devices).
//        Auth:     Authorization: Bearer <unifiApiToken>
//        Stateless. State flag: developerApiReady.
//
//   2. Legacy context (username/password + CSRF + session cookie):
//        Endpoint: https://<unifiHost>:443/proxy/access/api/v2/...
//        Used for: dismiss call (POST /device/{id}/reply_remote).
//        Auth:     Cookie: TOKEN=<sessionCookie>, X-Csrf-Token: <csrfToken>
//        Stateful. State flag: isLoggedIn.
//
// Failures in one context MUST NOT invalidate the other.
// =============================================================================

// Legacy context state — kept for the dismiss path.
extern String csrfToken;
extern String sessionCookie;
extern String userId;
extern String userName;
extern bool isLoggedIn;
extern String unifiLastError;

// Developer-API context state.
extern bool developerApiReady;

// Resolved device IDs (now passed verbatim — no MAC normalization).
extern String resolvedDoorbellDeviceId;
extern String resolvedViewerIds[4];
extern int resolvedViewerCount;

// Legacy context (used only for dismiss).
bool unifiLogin();
void forceRelogin();        // Force re-authentication (clears session)
bool unifiDismissCall(const String& deviceId, const String& requestId);

// Developer-API context (trigger + discovery).
bool unifiTriggerRing();
String unifiGetReaders();  // GET /api/v1/developer/devices?refresh=true (filtered)

// Bootstrap: probes the developer API and resolves device IDs.
bool unifiBootstrap();

// Helper functions
String generateRandomString(int length);
String generateUUID();
