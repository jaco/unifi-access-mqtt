## Context

The ESP32 doorbell firmware currently integrates with UniFi Access via undocumented controller endpoints (`/proxy/access/api/v2/...`) that require:

- A username/password login that returns a session cookie (`TOKEN`) and a CSRF token (`X-Csrf-Token`).
- A handcrafted `remote_call` JSON payload mimicking the WebRTC call setup the iOS / Web app sends, including a fabricated `request_id`, `agora_channel`, `room_id`, and a `notify_door_guards` array of viewer IDs.
- A separate `reply_remote` call with `response: "denied"` to dismiss an active call.
- A topology fetch (`/devices/topology4`) that returns a deeply nested document we have to filter while streaming because of the limited ESP32 heap.

This integration is brittle: Ubiquiti has changed the internal endpoints multiple times, the login flow occasionally breaks across firmware revisions, and the WebRTC payload is reverse-engineered. UniFi Access **4.0.10** ships a documented developer API with a Bearer-token auth scheme:

- `GET  /api/v1/developer/devices` — flat device list.
- `POST /api/v1/developer/devices/{device_id}/doorbell` — trigger ring (with optional `cancel: true` flag).

Empirical testing against a real controller showed the `cancel: true` flag does **not** behave as a pure dismiss: when no call is active it triggers a ring; when one is active it cancels it *and* triggers a fresh ring. There is no other documented endpoint anywhere in the API reference for ending a ring without re-triggering. As a result this change is a **partial** migration — the trigger and discovery flows move to the official API; the dismiss flow stays on the legacy `POST /proxy/access/api/v2/device/{id}/reply_remote` endpoint, which still requires the username/password login + CSRF + session cookie state.

The OpenAPI document for the subset of *official* endpoints we use is checked in alongside this change (`unifi-access-openapi.yaml`); it deliberately does not cover the legacy dismiss endpoint.

Constraints:

- **Hardware**: ESP32 / ESP32-S3 with limited heap (~200 KB free after WiFi). The streaming JSON parser stays.
- **TLS**: UniFi Console certs are self-signed → keep `WiFiClientSecure::setInsecure()`.
- **Port**: Developer API listens on **12445**, not 443.
- **Stakeholders**: existing users of the firmware; they will need to re-run the setup wizard.

## Goals / Non-Goals

**Goals:**

- Replace the unofficial `remote_call` flow with the official `POST /devices/{id}/doorbell` endpoint, always using `cancel: true` for idempotency against stale rings.
- Replace the `topology4` streaming parser with the simpler `GET /devices` flat list.
- Provide a documented OpenAPI subset (`unifi-access-openapi.yaml`) for the *official* endpoints we use, so the integration can be inspected, mocked, and code-generated against in the future.
- Keep the existing GPIO and MQTT trigger paths working unchanged from the user's perspective.
- Keep the existing dismiss flow (`reply_remote`) working without disruption — including its login + CSRF + cookie machinery.

**Non-Goals:**

- Migrating the Go MQTT gateway in `app/`. That can be done in a follow-up change.
- Removing the legacy login flow. We need it to keep `reply_remote` working; until Ubiquiti adds a documented dismiss endpoint, the login state machine stays.
- Implementing other developer API capabilities (users, visitors, access policies, NFC, etc.). Only the doorbell + device-list endpoints are in scope.
- Subscribing to the `wss://.../api/v1/developer/devices/notifications` WebSocket. The firmware is a one-way trigger; reading status events is not currently a feature.
- Validating the controller's TLS certificate. We continue to trust on first use via `setInsecure()`.

## Decisions

### Decision: Migrate the supported flows; retain legacy for dismiss

**Choice**: Use the official developer API for everything that has an official replacement (trigger, discovery). Keep the legacy login + `reply_remote` path as-is for dismiss.
**Why**: The official API has no pure-dismiss endpoint. `cancel: true` re-triggers a ring (verified empirically — see Context). Cleanly canceling an in-flight ring without re-ringing the reader is currently only possible via `reply_remote`.
**Alternatives considered**:
- *Drop dismiss entirely and accept that the doorbell rings until it times out* — regression for users who rely on the door-contact dismiss feature; rejected.
- *Wait for Ubiquiti to ship a dismiss endpoint* — open-ended timeline; we'd be stuck on the unofficial API for trigger as well; rejected.
- *Use the NFC-enrollment-session "wake reader" side effect to interrupt a ring* — undocumented for that purpose, requires `edit:credential` scope, displays the wrong UI on the reader; rejected.

### Decision: Both auth schemes coexist

**Choice**: Keep `unifiUsername` and `unifiPassword` for the legacy login. Add a new `unifiApiToken` for the developer API. Both are required for full functionality.
**Why**: Forced by the previous decision. The legacy `reply_remote` accepts only the cookie + CSRF auth, not Bearer; the official endpoints accept only Bearer.
**Layout**: Two outbound auth contexts, kept independent in code:
- *Developer-API context*: `Authorization: Bearer <unifiApiToken>` against `https://<host>:<unifiPort>/api/v1/developer/...`. Stateless. No cookie, no CSRF.
- *Legacy context*: `Cookie: TOKEN=<sessionCookie>` + `X-Csrf-Token: <csrfToken>` against `https://<host>:443/proxy/access/api/v2/...`. Stateful — relogin on 401/403.
**Trade-off**: Initial setup gets one extra field (the API token). The wizard explains why both are needed.

### Decision: Trigger always sends `cancel: true`

**Choice**: Every `POST /doorbell` from `unifiTriggerRing()` carries body `{"cancel": true}`. We never send a body of `{}`.
**Why**: Empirically the controller treats `cancel: true` as "if there's a previous ring, cancel it; then ring." That's exactly what we want when the user mashes the doorbell button or when an earlier trigger silently failed. Idempotent and self-healing against stale state.
**Trade-off**: A tiny extra payload byte count. Worth it.

### Decision: Dismiss continues to use legacy `reply_remote`

**Choice**: `unifiDismissCall(deviceId, requestId)` keeps its current implementation against `POST /proxy/access/api/v2/device/{id}/reply_remote` with body `{response:"denied", request_id, device_id, user_id, user_name}`. Its public signature does not change.
**Why**: It works today and the official API has no replacement. Risk of touching working code is nonzero, and the cost of leaving it is just retaining ~50 lines of the existing implementation.
**Caveat**: The dismiss code path still requires us to know the active call's `request_id`. Today this is sourced from MQTT events emitted by the Go gateway / WebSocket subscription elsewhere in the system. Out of scope for this change — we don't introduce a new source for `request_id`.

### Decision: Use `GET /devices` for the setup-wizard reader picker

**Choice**: Replace the streaming `topology4` parser with the flat `GET /api/v1/developer/devices?refresh=true` response.
**Why**: The new endpoint returns a much smaller, flatter document — a nested array of devices grouped by hub/door — that fits comfortably in `JsonDocument`. Each device record carries the fields we need (`id`, `name`, `alias`, `type`) plus `capabilities`, `is_online`, and `connected_uah_id` that the docs do not advertise but the controller actually returns.
**Trade-off**: We lose floor / door grouping that `topology4` provided. The wizard now shows `alias` (which is the device's display alias, e.g. "Front Door — Reader") instead of `floor / door` breadcrumbs. Acceptable: alias is what users see in the UniFi Access UI.

### Decision: Filter readers by `capabilities ∋ "remote_call"`, not by model name

**Choice**: Treat a device as a doorbell-capable reader iff its `capabilities` array contains the string `"remote_call"`. Drop the model-name regex (`UA-G2*`, `UA-G3*`, `UA-INTERCOM*`).
**Why**: Empirically — see test runs against a real controller — `is_reader` and `remote_call` are independent. A site can have a reader that *can't* trigger a doorbell (e.g. `UA-G3-W` "Gartenhaus - Eingang" has `is_reader` but lacks `remote_call`), and the model-name regex would falsely include it. The `remote_call` capability is the controller's own authoritative marker for "this device can be told to ring."
**Bonus**: The same response surfaces `is_online`. The wizard can render offline readers as disabled to give a clearer signal than a failed `POST /doorbell` later.
**Alternative considered**: keep the regex *plus* require `remote_call` — rejected as redundant; the capability is sufficient and self-documenting.

### Decision: Drop MAC normalization entirely

**Choice**: The `id` returned by `GET /devices` is the device identifier the doorbell endpoint expects directly. Remove `normalizeMAC()` and the configured-id-vs-MAC branching in `unifiBootstrap()`.
**Why**: With the legacy topology endpoint we sometimes had to convert a MAC-style configured id into the controller's internal id. With the developer API, what we list and what we POST against are the same string. Simpler config = fewer footguns.

### Decision: Configurable port, default 12445

**Choice**: Add `unifiPort` to config, default 12445. Keep the host as a hostname/IP (no scheme).
**Why**: The developer API runs on a dedicated port. Some advanced users may proxy it; making it configurable is cheap.

### Decision: Keep `setInsecure()` and the streaming parser plumbing

**Choice**: We do not introduce certificate pinning, nor do we drop the chunked-stream JSON wrapper.
**Why**: Self-signed certs are the documented norm for the UniFi Console; full validation would require shipping CA bundles. The chunked-stream code is still useful for the (smaller) device list and any future endpoint we may add.

### Decision: Additive migration — keep existing creds, prompt for the new token

**Choice**: On first boot of the new firmware, existing `unifiUsername` / `unifiPassword` are kept in NVS. If `unifiApiToken` is empty, the firmware marks "UniFi setup incomplete" and routes the user to a wizard step that asks only for the API token (and confirms `unifiPort`). The reader picker still works because device discovery now uses the token.
**Why**: Username/password is still required for the dismiss flow, so wiping it would break dismiss. An additive migration is non-destructive and minimizes user friction.

## Risks / Trade-offs

- **[Risk] Users on UniFi Access < 4.0.10 can't use the new trigger flow.** → Mitigation: README states the version requirement; the setup wizard surfaces a clear error ("Controller does not support the developer API — requires UniFi Access 4.0.10+") if the probe `GET /devices` returns 404 or `CODE_DEVICE_API_NOT_SUPPORTED`. Such users should remain on the previous firmware version.
- **[Risk] Two auth contexts double the surface area.** → Mitigation: Encapsulate each in its own helper (`sendDeveloperRequest()` for Bearer, `sendLegacyRequest()` wrapping the existing login + CSRF dance). Each call site picks one explicitly. No shared mutable state between contexts.
- **[Risk] User updates only one credential and the other silently rots.** → Mitigation: The wizard's final "summary" step shows separate "Trigger: ✓ / ✗" and "Dismiss: ✓ / ✗" indicators by probing both endpoints. Misconfiguration of either is visible immediately, not at the moment of first use.
- **[Risk] Token leakage from NVS / web UI.** → Mitigation: The web UI never echoes the token after it is saved (matches existing behavior for the password field). NVS is on the device; physical access already implies trust. No new exposure vs. the current password storage.
- **[Risk] Only one device can ring per call (no `notify_door_guards` array).** → Mitigation: This matches the documented API. Most users have a single reader; multi-reader setups will need to invoke the endpoint per device. If demand appears, we can add a comma-separated `doorbellDeviceIds` config.
- **[Trade-off] Floor/door grouping disappears in the wizard.** → Acceptable: alias is sufficient for selection.

## Migration Plan

1. Ship new firmware version. Not breaking from a credentials standpoint — existing creds are preserved.
2. On boot, the new firmware:
   - Loads existing `unifiHost`, `unifiUsername`, `unifiPassword` as before.
   - Loads the new `unifiApiToken` and `unifiPort` (default 12445).
   - If `unifiApiToken` is empty, marks UniFi setup incomplete.
3. User opens `http://doorbell.local`. The wizard surfaces a banner: "Action required: enter your UniFi Access API token to enable doorbell triggering." It links to a single-step form for `apiToken` (and editable `unifiPort`).
4. Validate by issuing a probe `GET /devices` with the token; if 401 → "Token rejected", if connection refused on `<port>` → "Cannot reach controller at <host>:<port>; check the port (default 12445) and that your controller runs UniFi Access 4.0.10+".
5. Persist token; the device resumes normal operation. Existing username/password continues to authenticate dismiss calls untouched.

**Rollback**: users can flash the previous firmware. NVS still contains username/password; trigger reverts to the legacy `remote_call` path. No data loss.

## Open Questions

- Do we want to add a "Test ring" button in the wizard once the token + reader are picked? (Probably yes — defer to tasks.)
- Should the firmware probe the controller version (some endpoint surfaces it) and pre-emptively warn on < 4.0.10, instead of waiting for `CODE_DEVICE_API_NOT_SUPPORTED`? (Optional; deferred.)
- If/when Ubiquiti ships a documented dismiss endpoint, we can deprecate the legacy login flow in a follow-up change. Track this against future release notes.
