## 1. Configuration schema (additive)

- [x] 1.1 Add `unifiApiToken` (String) and `unifiPort` (uint16, default 12445) to `AppConfig` in `src/config_manager.h`. **Keep** `unifiUsername`, `unifiPassword`, `unifiHost` unchanged.
- [x] 1.2 Add `hasUnifiApiToken()` returning true iff `unifiApiToken` and `unifiHost` are both non-empty. Keep `hasUnifiCredentials()` (now meaning username/password) as-is.
- [x] 1.3 Add NVS load/save for `uf_token` and `uf_port` keys in `config_manager.cpp`. Existing `uf_user` / `uf_pass` keys MUST continue to load/save unchanged.
- [x] 1.4 Add a "UniFi setup incomplete" computed flag returning true when `unifiApiToken` is empty. No NVS migration / wipe — existing creds are preserved.
- [x] 1.5 ~~Update `config-example.json` (root of repo) to include the new fields alongside the existing username/password.~~ **N/A** — that file is the Go MQTT gateway's config (out of scope per the proposal). The firmware uses NVS, not a JSON file.

## 2. UniFi API changes (`src/unifi_api.{h,cpp}`)

- [x] 2.1 **Keep** `unifiLogin()`, `forceRelogin()`, `csrfToken`, `sessionCookie`, `userId`, `userName`, `isLoggedIn`, and the `extractCookie` / `extractHeader` / `readHttpResponse` helpers. They remain in use for the dismiss path.
- [x] 2.2 Add a private helper `static bool sendDeveloperRequest(const char* method, const String& path, const String& body, JsonDocument* outJson, int* outHttpStatus)` that opens `WiFiClientSecure` to `<host>:<unifiPort>`, sends `Authorization: Bearer <unifiApiToken>` and optional `Content-Type: application/json`, and parses the response status + JSON body using the existing `ChunkedStream`. This helper MUST NOT touch `csrfToken` / `sessionCookie`.
- [x] 2.3 Rewrite `unifiTriggerRing()` to call `sendDeveloperRequest("POST", "/api/v1/developer/devices/" + deviceId + "/doorbell", "{\"cancel\":true}", ...)`. Always include `cancel: true`. Map `code: "SUCCESS"` to true; surface `CODE_DEVICE_API_NOT_SUPPORTED`, `CODE_DEVICE_DEVICE_OFFLINE`, and `401`/`403` to user-friendly `unifiLastError` strings.
- [x] 2.4 **Do NOT touch `unifiDismissCall()`.** Its body, signature, error handling, and the legacy login state it depends on stay exactly as they are today.
- [x] 2.5 Rewrite `unifiGetTopology()` (rename to `unifiGetReaders()`) to call `sendDeveloperRequest("GET", "/api/v1/developer/devices?refresh=true", "", ...)`, iterate the nested array, filter to devices whose `capabilities` array contains `"remote_call"`, and emit `{success, readers:[{id,name,alias,type,is_online}]}` — do NOT filter by model-name (`type`) string.
- [x] 2.6 Update `unifiBootstrap()` to drop MAC normalization (`resolvedDoorbellDeviceId = appConfig.doorbellDeviceId` directly), delete `normalizeMAC()`. Note: the dev-API probe is performed on demand by callers (e.g. `/api/topology`, `/api/test`) rather than synchronously inside `unifiBootstrap()` — this avoids blocking startup on a slow/unreachable controller.
- [x] 2.7 Add diagnostic logs: every developer-API request logs `method`, `path`, `httpStatus`, and `code` from the JSON body. Keep existing legacy-path logs unchanged.

## 3. Web setup wizard

- [x] 3.1 Update web setup wizard (`SetupWizard.tsx`) to add `Port` (default 12445) and `API Token` fields **alongside** the existing host/username/password fields. Show help text describing how to create a token in the UniFi Portal.
- [x] 3.2 Enhance `/api/test` in `src/webserver.cpp` to validate both auth contexts independently and return per-credential status (`token: {ok, message}`, `login: {ok, message}`). The wizard's "Save" path still uses `/api/config`; this endpoint serves both as the wizard's pre-save probe and the on-demand "re-test credentials" button.
- [x] 3.3 Update the reader-picker to render `alias` prominently and `name · type` as subtext; offline readers (`is_online: false`) are rendered greyed out and not selectable with an "(offline)" suffix. Floor/door breadcrumbs were never carried over from `topology4` → already moot.
- [x] 3.4 Add a "Test ring" button on step 4 that calls `unifiTriggerRing()` (via `/api/control/ring`) and surfaces the result inline.
- [x] 3.5 Add a passive "Trigger / Dismiss: ✓ / ✗" pair on step 4, populated from the `/api/test` probe result.
- [x] 3.6 Add an "upgrade banner" inside step 1 that appears when `unifi.apiTokenSet === false` but legacy creds exist (`needsTokenUpgrade`). The user keeps their existing username/password and only needs to paste a token. The "API token / password" inputs have a "Token is set (leave blank to keep)" placeholder so re-entry is optional.
- [x] 3.7 The web UI never returns saved tokens or passwords in `GET /api/config` — passwords are masked with `********` and tokens are reported only as `apiTokenSet: boolean`. Sentinel `********` on POST means "keep existing" (already the convention for passwords; mirrored for the token).

## 4. README and docs

- [x] 4.1 Update top-level `README.md` "UniFi Access Setup" section to describe API-token creation in the UniFi Portal (Access > Settings > General > Advanced > API Token), the version requirement (4.0.10+ for the trigger), and the default port 12445.
- [x] 4.2 Document why both an API token AND username/password are required: trigger uses the official API, dismiss uses the legacy `reply_remote` endpoint that has no Bearer-token equivalent yet.
- [x] 4.3 Add an "Upgrading from a previous firmware version" note: existing setups continue to work for dismiss; users should add an API token to enable trigger via the official API.
- [x] 4.4 Reference `openspec/changes/use-unifi-official-api/unifi-access-openapi.yaml` from the README; note that it covers only the official endpoints.

## 5. Verification

- [ ] 5.1 Build for `esp32-poe`, `esp32-s3-zero`, and `esp32-s3-wroom` environments and confirm no compile errors / warnings introduced.
- [ ] 5.2 Flash to a real device, run through the setup wizard end-to-end against a UniFi Access 4.0.10+ controller, and confirm the reader list populates and both probes succeed.
- [ ] 5.3 Trigger a ring via the GPIO button and confirm the reader rings; trigger via MQTT and confirm the same. Verify the request payload is `{"cancel": true}`.
- [ ] 5.4 With a ring active, open the door contact and confirm the call dismisses cleanly (does NOT re-ring). Verify in the UniFi Access UI that the ring stops.
- [ ] 5.5 Idempotency: trigger twice in rapid succession (button-mash) and confirm only one ring is heard at any given moment — the second trigger should cancel-and-restart cleanly.
- [ ] 5.6 Misconfigure the token and confirm the wizard surfaces "API token rejected" while still accepting valid username/password; misconfigure the password symmetrically. Misconfigure the port and confirm the connection-refused message.
- [ ] 5.7 Confirm the upgrade flow: flash on top of a device running the previous firmware, verify legacy creds are preserved, the upgrade banner appears, and adding only the token completes setup.
- [ ] 5.8 Confirm a fresh install with no prior config still presents the full setup flow and works end-to-end.
