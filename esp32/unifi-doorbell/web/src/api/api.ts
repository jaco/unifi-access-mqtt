import type { Config, TopologyResponse, VersionInfo, FirmwareManifest } from '../types'

const GITHUB_PAGES_BASE = 'https://mqtt-home.github.io/unifi-access-mqtt'

const api = {
  async getMode(): Promise<{ apMode: boolean }> {
    const res = await fetch('/api/mode')
    return res.json()
  },

  async getAuthStatus(): Promise<{ authenticated: boolean }> {
    const res = await fetch('/api/auth/status')
    return res.json()
  },

  async login(username: string, password: string): Promise<{ success: boolean; message?: string }> {
    const res = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password })
    })
    return res.json()
  },

  async logout(): Promise<void> {
    await fetch('/api/auth/logout', { method: 'POST' })
  },

  async getVersion(): Promise<VersionInfo> {
    const res = await fetch('/api/version')
    return res.json()
  },

  async getLatestManifest(board: string): Promise<FirmwareManifest | null> {
    try {
      const res = await fetch(`${GITHUB_PAGES_BASE}/manifest-${board}.json`)
      if (!res.ok) return null
      return res.json()
    } catch {
      return null
    }
  },

  async getConfig(): Promise<Config> {
    const res = await fetch('/api/config')
    return res.json()
  },

  async saveConfig(config: Partial<Config>): Promise<{ success: boolean; message?: string }> {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    })
    return res.json()
  },

  async getCertificate(): Promise<{ certificate?: string }> {
    const res = await fetch('/api/cert')
    return res.json()
  },

  async saveCertificate(certificate: string): Promise<void> {
    await fetch('/api/cert', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ certificate })
    })
  },

  async fetchCertificate(): Promise<{ success: boolean; certificate?: string; message?: string }> {
    const res = await fetch('/api/fetchcert', { method: 'POST' })
    return res.json()
  },

  async testConnection(): Promise<{ success: boolean; message: string }> {
    const res = await fetch('/api/test', { method: 'POST' })
    return res.json()
  },

  async getTopology(): Promise<TopologyResponse> {
    const res = await fetch('/api/topology')
    return res.json()
  },

  async testWifi(ssid: string, password: string): Promise<void> {
    await fetch('/api/wifi/test', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password })
    })
  },

  async getWifiStatus(): Promise<{ status: string; success?: boolean; message?: string; ip?: string }> {
    const res = await fetch('/api/wifi/status')
    return res.json()
  },

  async setupWifi(ssid: string, password: string): Promise<void> {
    await fetch('/api/wifi/setup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password })
    })
  },

  async triggerRing(): Promise<{ success: boolean; message?: string }> {
    const res = await fetch('/api/control/ring', { method: 'POST' })
    return res.json()
  },

  async dismissCall(): Promise<{ success: boolean; message?: string }> {
    const res = await fetch('/api/control/dismiss', { method: 'POST' })
    return res.json()
  },

  async reboot(): Promise<void> {
    await fetch('/api/control/reboot', { method: 'POST' })
  },

  async factoryReset(): Promise<void> {
    await fetch('/api/control/reset', { method: 'POST' })
  },

  async uploadFirmware(
    file: File,
    onProgress: (percent: number) => void
  ): Promise<{ success: boolean; message?: string }> {
    return new Promise((resolve, reject) => {
      const formData = new FormData()
      formData.append('firmware', file)

      const xhr = new XMLHttpRequest()
      xhr.open('POST', '/api/ota/upload', true)

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          onProgress(Math.round((e.loaded / e.total) * 100))
        }
      }

      xhr.onload = () => {
        if (xhr.status === 200) {
          resolve({ success: true })
        } else {
          try {
            resolve(JSON.parse(xhr.responseText))
          } catch {
            reject(new Error(xhr.statusText))
          }
        }
      }

      xhr.onerror = () => reject(new Error('Upload failed'))
      xhr.send(formData)
    })
  }
}

export default api
