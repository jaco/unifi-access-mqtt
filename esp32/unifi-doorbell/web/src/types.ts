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
    username?: string
    password?: string
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
    loggedIn?: boolean
    configured?: boolean
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
  mac: string
}

export interface TopologyResponse {
  success: boolean
  message?: string
  readers?: TopologyDevice[]
}

export type Tab = 'status' | 'setup' | 'settings' | 'system'
