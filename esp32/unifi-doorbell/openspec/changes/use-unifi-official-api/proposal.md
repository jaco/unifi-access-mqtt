## Why

The current `unifi_api.cpp` integration relies entirely on Ubiquiti's **internal** controller endpoints (`/proxy/access/api/v2/.../remote_call`, `/reply_remote`, `/devices/topology4`) that require username/password login, CSRF tokens, and a session cookie. These endpoints are unofficial, undocumented, and have changed multiple times across controller releases. UniFi Access **4.0.10** introduces an official, documented developer API authenticated via a stable Bearer API token. Two of our three flows have direct official replacements (trigger ring, list devices); the third (dismiss an in-flight ring) does not — empirical testing showed `POST /doorbell` with `cancel: true` *also* re-triggers a ring when no call is active and even when one is active it cancels-and-rings semantics that don't match a clean dismissal. Migrating the supported flows to the official API removes the brittle WebRTC payload synthesis and the topology streaming, while we retain the legacy login + `reply_remote` path only where the official API has no equivalent.

## What Changes

- Add support for the official UniFi Access Developer API (Bearer token, port 12445).
- Replace the internal `remote_call` POST in `unifiTriggerRing()` with `POST /api/v1/developer/devices/{device_id}/doorbell` and **always** include `{"cancel": true}` in the body. This makes the trigger idempotent: if a previous ring is still active it is canceled before the new one starts; if not, the call simply rings.
- Replace the internal topology fetch in `unifiGetTopology()` with `GET /api/v1/developer/devices?refresh=true` and filter readers by `capabilities ∋ "remote_call"`.
- **Retain** the legacy login flow (username/password → CSRF → session cookie) and the legacy `POST /proxy/access/api/v2/device/{id}/reply_remote` call for the dismiss path. The official API has no pure-dismiss equivalent (see the test results above).
- Add a new `unifiApiToken` configuration field alongside the existing `unifiUsername` / `unifiPassword`. **Both are required**: the token authorizes the official API (trigger + discovery), the username/password authorizes the legacy API (dismiss).
- Add a configurable `unifiPort` field (default `12445`) for the developer API. The legacy endpoints continue to use port `443` on the same host.
- Bundle the OpenAPI document (`unifi-access-openapi.yaml`) describing the subset of *official* endpoints used; explicitly document that dismiss is out of scope for that document because it has no official equivalent.
- The web setup wizard gains a new "API Token" step. Existing username/password fields are unchanged.

## Capabilities

### New Capabilities
- `unifi-official-api`: Authenticate with the UniFi Access Developer API via Bearer API token, fetch the device list, and trigger doorbell rings on a specific reader through the documented `POST /api/v1/developer/devices/{device_id}/doorbell` endpoint with `cancel: true`. The legacy login + `reply_remote` dismissal path remains as the only supported way to *purely* cancel an in-flight ring without re-triggering, and is documented within this capability as a non-removable dependency.

### Modified Capabilities
<!-- No pre-existing OpenSpec specs in openspec/specs/ — this project is adopting OpenSpec with this change. -->

## Impact

- **Code**: `src/unifi_api.{h,cpp}` partially rewritten — `unifiTriggerRing()` and `unifiGetTopology()` (renamed `unifiGetReaders()`) re-pointed at the developer API and gain a new Bearer-token request helper; `unifiLogin()`, `forceRelogin()`, the CSRF/cookie state, and `unifiDismissCall()` are **kept** because they're still needed for `reply_remote`. `src/config_manager.{h,cpp}` gains `unifiApiToken` and `unifiPort` fields; existing username/password fields are unchanged. `src/webserver.cpp` setup-wizard endpoints add an API-token step. `web/setup.html` (and setup JS) updated to ask for the token in addition to existing credentials.
- **APIs (HTTP)**: Outbound HTTPS calls split across two endpoints. Trigger + discovery → `:<unifiPort>/api/v1/developer/...` with `Authorization: Bearer <token>`. Dismiss → `:443/proxy/access/api/v2/device/{id}/reply_remote` with the existing `Cookie: TOKEN=...` + `X-Csrf-Token: ...` headers (unchanged from today).
- **Configuration / persistence**: Additive NVS schema change. New fields are optional at first boot; existing devices are *not* wiped. The setup wizard will surface a one-time prompt asking the user to add an API token if it's missing. Trigger fails clearly until the token is supplied; dismiss continues to work.
- **Docs**: Top-level `README.md` "UniFi Access Setup" section gains a section on API token creation (Access > Settings > General > Advanced > API Token) and explains why both creds are required.
- **Dependencies**: No new libraries required (still `WiFiClientSecure` + `ArduinoJson`).
- **Compatibility**: Trigger via the new API requires UniFi Access **4.0.10 or later** on the controller. Dismiss continues to work on any controller that already worked with the previous firmware.
- **MQTT gateway** (`app/`, Go application): Out of scope for this change — only the ESP32 firmware in `esp32/unifi-doorbell/` is being migrated. The Go gateway can be migrated separately.
