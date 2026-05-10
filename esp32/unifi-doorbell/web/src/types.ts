export interface Config {
  network?: {
    wifiSsid?: string
    wifiPassword?: string
  }
  mqtt?: {
    enabled?: boolean
    server?: string
    port?: number
    topic?: string
    authEnabled?: boolean
    username?: string
    password?: string
  }
  web?: {
    username?: string
    password?: string
  }
  unifi?: {
    host?: string
    /** Developer-API port (default 12445). Legacy endpoints remain on 443. */
    port?: number
    username?: string
    password?: string
    /** Bearer API token for the official developer API. Write-only on POST. */
    apiToken?: string
    /** Read-only flag returned by GET /api/config — true if a token is saved. */
    apiTokenSet?: boolean
  }
  doorbell?: {
    deviceId?: string
    deviceName?: string
    doorName?: string
  }
  gpios?: GpioConfig[]
  mqttTriggers?: MqttTrigger[]
}

export interface GpioConfig {
  enabled: boolean
  pin: number
  action: 'ring_button' | 'door_contact' | 'generic'
  pullMode: 'up' | 'down'
  label: string
  debounceMs?: number
  holdMs?: number
}

export interface MqttTrigger {
  enabled: boolean
  topic: string
  jsonField: string
  triggerValue: string
  action: 'dismiss' | 'ring'
  label: string
}

export interface Status {
  doorbell?: {
    active?: boolean
  }
  network?: {
    connected?: boolean
    type?: 'ethernet' | 'wifi'
    ip?: string
  }
  unifi?: {
    wsConnected?: boolean
    loggedIn?: boolean              // legacy context (username/password)
    developerApiReady?: boolean     // dev-api context (Bearer token)
    configured?: boolean            // legacy creds present
    apiTokenSet?: boolean           // API token present
    setupIncomplete?: boolean       // legacy creds present, token missing
    error?: string
  }
  mqtt?: {
    connected?: boolean
  }
  system?: {
    heap?: number
    uptime?: number
  }
}

export interface TopologyDevice {
  id: string
  name: string
  type: string
  /** User-assigned display name from the controller (preferred for UI). */
  alias?: string
  /** Whether the device is currently online. */
  is_online?: boolean
  /** Legacy field — kept optional for backwards-compat with cached responses. */
  mac?: string
}

export interface TopologyResponse {
  success: boolean
  message?: string
  canRetry?: boolean
  readers?: TopologyDevice[]
}

export interface ConnectionTestResult {
  success: boolean
  message: string
  token?: { ok: boolean; message: string }
  login?: { ok: boolean; message: string }
}

export type Tab = 'status' | 'setup' | 'settings' | 'system'

export interface VersionInfo {
  version: string
  board: string
}

export interface FirmwareManifest {
  name: string
  version: string
  builds: Array<{
    chipFamily: string
    parts: Array<{
      path: string
      offset: number
    }>
  }>
}

export interface UpdateInfo {
  available: boolean
  currentVersion: string
  latestVersion: string
  board: string
  manifestUrl?: string
}
