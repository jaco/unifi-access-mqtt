## ADDED Requirements

### Requirement: Two independent authentication contexts

The firmware SHALL maintain two independent authentication contexts and use each only for its designated endpoint family:

- *Developer-API context*: Bearer API token. Used **only** for requests to `/api/v1/developer/...` on the configured `unifiPort`. Stateless. The firmware MUST NOT send cookies or `X-Csrf-Token` headers in this context.
- *Legacy context*: Session cookie (`Cookie: TOKEN=<sessionCookie>`) plus `X-Csrf-Token` header, established via the existing `unifiLogin()` flow against `https://<host>:443/api/auth/login`. Used **only** for requests to `/proxy/access/api/v2/...`. The firmware MUST NOT send `Authorization: Bearer` headers in this context.

The two contexts MUST NOT share state. A failure (e.g. 401) in one MUST NOT invalidate the other.

#### Scenario: Developer-API request carries only the Bearer header
- **WHEN** the firmware issues a request to `https://<host>:<unifiPort>/api/v1/developer/...`
- **THEN** the request includes exactly one `Authorization: Bearer <unifiApiToken>` header and no `Cookie` or `X-Csrf-Token` header

#### Scenario: Legacy request carries only the cookie + CSRF headers
- **WHEN** the firmware issues a request to `https://<host>:443/proxy/access/api/v2/...`
- **THEN** the request includes `Cookie: TOKEN=<sessionCookie>` and `X-Csrf-Token: <csrfToken>` headers and no `Authorization` header

#### Scenario: API token is missing
- **WHEN** the firmware attempts to issue a developer-API request and `unifiApiToken` is empty
- **THEN** the firmware aborts the request, sets `unifiLastError` to `"API token not configured"`, and returns failure WITHOUT opening a network connection
- **AND** the legacy login state (sessionCookie / csrfToken) is unaffected

#### Scenario: Username/password is missing
- **WHEN** the firmware attempts to dismiss a call and either `unifiUsername` or `unifiPassword` is empty
- **THEN** the firmware aborts the request, sets `unifiLastError` to `"Username/password not configured"`, and returns failure
- **AND** the API token is unaffected

#### Scenario: Server rejects the API token
- **WHEN** a developer-API request returns HTTP `401` or response code `CODE_AUTH_FAILED` / `CODE_ACCESS_TOKEN_INVALID`
- **THEN** the firmware sets `unifiLastError` to `"Token rejected"`, logs the event, and returns failure WITHOUT clearing the stored token (the user must update it via the web UI) and WITHOUT touching the legacy login state

#### Scenario: Server rejects the legacy session
- **WHEN** a legacy `reply_remote` request returns HTTP `401` or `403`
- **THEN** the firmware clears `sessionCookie` / `csrfToken` and re-runs `unifiLogin()` exactly once before retrying the dismiss; the API token is unaffected

### Requirement: API host and port configuration

The firmware SHALL store the controller host (hostname or IP, no scheme) and the API port as separate configuration fields. The default port SHALL be `12445`. All outbound TLS connections SHALL use `WiFiClientSecure::setInsecure()` to accept the controller's self-signed certificate.

#### Scenario: Default port is applied for fresh configurations
- **WHEN** a device is set up for the first time and the user does not change the port field
- **THEN** the firmware connects to `<host>:12445`

#### Scenario: User overrides the port
- **WHEN** the user enters port `54321` in the setup wizard
- **THEN** subsequent developer-API requests are sent to `https://<host>:54321/...`

### Requirement: Device discovery via the developer API

The firmware SHALL fetch the list of available devices using `GET /api/v1/developer/devices?refresh=true`, parse the response, and expose the subset that are doorbell-capable readers to the setup wizard. A device is considered doorbell-capable IFF its `capabilities` array contains the string `"remote_call"`. Model name (`type`) MUST NOT be used to determine doorbell capability. The firmware SHALL also pass through each device's `is_online` flag so the wizard can render offline devices distinctly.

#### Scenario: Successful device fetch returns only remote_call-capable readers
- **WHEN** the firmware calls `GET /api/v1/developer/devices?refresh=true` with a valid token
- **AND** the response contains a `UA-G3-Pro` device with capabilities including `"remote_call"`, a `UA-G3` device whose capabilities include `"is_reader"` but NOT `"remote_call"`, a `UA-Int-Viewer` (no `remote_call`), and a `UAH-DOOR` hub
- **THEN** the firmware returns a JSON list containing only the `UA-G3-Pro` device, with fields `{id, name, alias, type, is_online}`

#### Scenario: Reader exists but is offline
- **WHEN** a reader has `"remote_call"` in its capabilities and `is_online` equal to `false`
- **THEN** the firmware INCLUDES the reader in the returned list with `is_online: false` so the wizard can render it as disabled / unselectable

#### Scenario: Empty device list
- **WHEN** the response contains no devices with `"remote_call"` capability
- **THEN** the firmware returns an empty array and the wizard displays "No doorbell-capable readers found"

#### Scenario: Network failure during fetch
- **WHEN** the TCP connection to `<host>:<port>` fails or times out
- **THEN** the firmware retries up to 3 times with a 1-second delay between attempts, and on final failure returns `{"success":false,"message":"...","canRetry":true}`

### Requirement: Device id is used verbatim

The firmware SHALL use the `id` field returned by `GET /api/v1/developer/devices` as the value for `{device_id}` in subsequent `POST /api/v1/developer/devices/{device_id}/doorbell` calls without any transformation. MAC-address normalization MUST NOT be performed on configured device ids.

#### Scenario: Trigger uses the listed id verbatim
- **WHEN** the wizard saves a reader whose listed `id` is `84784806928f`
- **AND** the firmware later triggers the doorbell on that reader
- **THEN** the request URL is exactly `/api/v1/developer/devices/84784806928f/doorbell` with no normalization, no colons, no dashes, no case changes

### Requirement: Trigger doorbell ring via the developer API

The firmware SHALL trigger doorbell rings via `POST /api/v1/developer/devices/{device_id}/doorbell` where `{device_id}` is the configured reader's `id`. Every trigger request MUST carry the body `{"cancel": true}` so the call is idempotent against any stale ring left over from a previous failed trigger. The body MUST NOT be `{}`.

#### Scenario: Ring on configured reader
- **WHEN** a GPIO ring trigger fires and the firmware is in the developer-API context (token configured)
- **THEN** the firmware sends `POST /api/v1/developer/devices/{configuredDeviceId}/doorbell` with `Authorization: Bearer <token>`, `Content-Type: application/json`, and body `{"cancel": true}`
- **AND** on HTTP 200 with `code: "SUCCESS"` it logs `"Doorbell ring triggered"` and returns success

#### Scenario: Ring while a previous ring is still active
- **WHEN** `unifiTriggerRing()` is called and a previous ring is still in flight on the controller
- **THEN** the request body `{"cancel": true}` causes the controller to cancel the previous ring AND start a fresh one (this is the controller's documented behavior for `cancel: true` and is the desired effect — see design.md)

#### Scenario: Ring fails because controller does not support the API
- **WHEN** the response code is `CODE_DEVICE_API_NOT_SUPPORTED` or HTTP 404
- **THEN** the firmware sets `unifiLastError` to `"Controller does not support developer API (requires UniFi Access 4.0.10+)"` and returns failure

#### Scenario: Ring fails because device is offline
- **WHEN** the response code is `CODE_DEVICE_DEVICE_OFFLINE`
- **THEN** the firmware sets `unifiLastError` to `"Reader is offline"` and returns failure

### Requirement: Dismiss an active ring via the legacy `reply_remote` endpoint

The firmware SHALL dismiss an active doorbell ring exclusively via `POST /proxy/access/api/v2/device/{device_id}/reply_remote` (legacy, undocumented) using the legacy authentication context. The developer-API endpoint MUST NOT be used to dismiss, because `cancel: true` on `POST /doorbell` re-triggers a fresh ring.

#### Scenario: Door contact opens while ring is active
- **WHEN** the door-contact GPIO transitions to "open" or an MQTT dismiss command arrives, and the firmware knows the active call's `request_id`
- **THEN** the firmware sends `POST /proxy/access/api/v2/device/{configuredDeviceId}/reply_remote` (port 443) with body `{"device_id":"...","response":"denied","request_id":"...","user_id":"...","user_name":"..."}` and logs `"Doorbell call dismissed"`

#### Scenario: Dismiss requested but no `request_id` known
- **WHEN** a dismiss event fires but the firmware does not have a current `request_id` (no active call has been observed)
- **THEN** the firmware skips the call and logs `"No active call to dismiss"` — it does NOT attempt the developer-API endpoint as a substitute

#### Scenario: Legacy session has expired
- **WHEN** the dismiss request returns HTTP 401 / 403
- **THEN** the firmware clears `sessionCookie` / `csrfToken`, re-runs `unifiLogin()` exactly once, and retries the dismiss; on second failure it sets `unifiLastError` to `"Dismiss failed: session re-login did not recover"` and returns failure

### Requirement: Setup wizard collects both API token and username/password

The setup wizard SHALL collect `host`, `unifiPort` (default 12445), `apiToken`, `username`, and `password`. The wizard SHALL validate the API token by calling `GET /api/v1/developer/devices` AND validate the username/password by calling `unifiLogin()`. The configuration SHALL be persisted only if BOTH probes succeed; partial success SHALL surface a per-credential error.

#### Scenario: Both probes succeed
- **WHEN** the user submits the form and both `GET /devices` (Bearer) and `POST /api/auth/login` (username/password) succeed
- **THEN** the wizard stores `unifiHost`, `unifiPort`, `unifiApiToken`, `unifiUsername`, `unifiPassword` in NVS and proceeds to the reader-picker step

#### Scenario: API token rejected, login OK
- **WHEN** the `GET /devices` probe returns HTTP 401 but the legacy login succeeds
- **THEN** the wizard does NOT persist the configuration and shows the inline error "API token rejected — trigger will not work"

#### Scenario: Login rejected, token OK
- **WHEN** `unifiLogin()` fails with invalid credentials but the `GET /devices` probe succeeds
- **THEN** the wizard does NOT persist the configuration and shows the inline error "Username or password rejected — dismiss will not work"

#### Scenario: Wrong developer-API port
- **WHEN** the `GET /devices` probe times out or the TCP connection is refused on `<unifiPort>`
- **THEN** the wizard does NOT persist the configuration and shows "Cannot reach controller at <host>:<unifiPort>. Check the port (default 12445) and that your controller runs UniFi Access 4.0.10+"

### Requirement: Additive migration for the API token

On first boot of firmware that supports this capability, the firmware SHALL preserve any existing `unifiUsername`, `unifiPassword`, `unifiHost` values in NVS unchanged. If `unifiApiToken` is empty, the firmware SHALL mark the UniFi configuration as incomplete and present a single-step "Add API token" prompt in the setup wizard.

#### Scenario: Device upgraded from older firmware (no token yet)
- **WHEN** the firmware boots and finds non-empty `unifiUsername` and `unifiPassword`, empty `unifiApiToken`
- **THEN** the firmware keeps the legacy credentials, marks UniFi setup incomplete, and the web UI displays a banner linking to a single-step form for `apiToken` (and editable `unifiPort`)
- **AND** dismiss-via-`reply_remote` MAY still operate during this transient state if the user receives a doorbell call before completing setup

#### Scenario: Fresh install
- **WHEN** the firmware boots on a device with no existing config
- **THEN** the wizard shows the full setup flow that collects host, port, API token, and username/password in order

#### Scenario: User completes the upgrade flow
- **WHEN** the user submits a valid API token via the upgrade form
- **THEN** the firmware persists `unifiApiToken` and `unifiPort`, marks UniFi setup complete, and resumes normal operation without requiring the user to re-enter username/password

### Requirement: Bundled OpenAPI document

The change SHALL include a checked-in OpenAPI 3.x document (`unifi-access-openapi.yaml`) describing the subset of the UniFi Access Developer API used by the firmware. The document MUST include `GET /api/v1/developer/devices` and `POST /api/v1/developer/devices/{device_id}/doorbell`, the `bearerAuth` security scheme, and JSON schemas for the request bodies and responses.

#### Scenario: Spec is checked into the repository
- **WHEN** a developer inspects the change folder
- **THEN** they find `unifi-access-openapi.yaml` containing both endpoints and the `bearerAuth` scheme

#### Scenario: Spec stays in sync with code at archive time
- **WHEN** the change is archived into `openspec/specs/unifi-official-api/`
- **THEN** the OpenAPI document is moved alongside the spec so future changes can refer to it
