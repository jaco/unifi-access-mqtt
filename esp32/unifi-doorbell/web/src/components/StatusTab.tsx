import type { Status } from '../types'
import api from '../api/api'

interface StatusTabProps {
  status: Status
  ringing: boolean
}

export function StatusTab({ status, ringing }: StatusTabProps) {
  const handleTriggerRing = async () => {
    const result = await api.triggerRing()
    if (!result.success) {
      alert('Failed to trigger ring: ' + (result.message ?? 'Unknown error'))
    }
  }

  const formatUptime = (seconds?: number) => {
    if (seconds === undefined) return '--'

    const minutes = Math.floor(seconds / 60)
    const hours = Math.floor(minutes / 60)
    const days = Math.floor(hours / 24)
    const years = Math.floor(days / 365)
    const months = Math.floor((days % 365) / 30)
    const remainingDays = days % 30
    const remainingHours = hours % 24
    const remainingMinutes = minutes % 60

    const parts: string[] = []
    if (years > 0) parts.push(`${years}y`)
    if (months > 0) parts.push(`${months}mo`)
    if (remainingDays > 0) parts.push(`${remainingDays}d`)
    if (remainingHours > 0) parts.push(`${remainingHours}h`)
    if (remainingMinutes > 0) parts.push(`${remainingMinutes}m`)

    return parts.length > 0 ? parts.join(' ') : '0m'
  }

  const formatHeap = (bytes?: number) => {
    if (bytes === undefined) return '--'
    return `${Math.round(bytes / 1024)} KB`
  }

  return (
    <section>
      <div class="card">
        <div class="card-header">
          <h2>System Status</h2>
        </div>
        <div class="card-body">
          <div class="status-grid">
            <div class="status-item">
              <div class="label">Doorbell</div>
              <div class={`value ${ringing ? 'ringing' : ''}`}>
                {status.doorbell === undefined
                  ? '--'
                  : ringing ? 'Ringing' : 'Idle'}
              </div>
            </div>
            <div class="status-item">
              <div class="label">Network</div>
              <div class={`value ${status.network?.connected ? 'connected' : status.network ? 'disconnected' : ''}`}>
                {status.network === undefined
                  ? '--'
                  : status.network.connected
                    ? (status.network.type === 'ethernet' ? 'Ethernet' : 'WiFi')
                    : 'Disconnected'}
              </div>
            </div>
            <div class="status-item">
              <div class="label">IP Address</div>
              <div class="value">{status.network?.ip ?? '--'}</div>
            </div>
            <div class="status-item">
              <div class="label">UniFi Access</div>
              <div class={`value ${status.unifi?.wsConnected ? 'connected' : ''}`}>
                {status.unifi?.wsConnected ? 'Connected'
                  : status.unifi?.loggedIn ? 'Logged In'
                  : status.unifi?.error ? status.unifi.error
                  : status.unifi?.configured ? 'Logging In...'
                  : '--'}
              </div>
            </div>
            <div class="status-item">
              <div class="label">MQTT</div>
              <div class={`value ${status.mqtt?.connected ? 'connected' : ''}`}>
                {status.mqtt === undefined
                  ? '--'
                  : status.mqtt.connected
                    ? 'Connected'
                    : 'Disabled'}
              </div>
            </div>
            <div class="status-item">
              <div class="label">Free Memory</div>
              <div class="value">{formatHeap(status.system?.heap)}</div>
            </div>
            <div class="status-item">
              <div class="label">Uptime</div>
              <div class="value">{formatUptime(status.system?.uptime)}</div>
            </div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>Quick Actions</h2>
        </div>
        <div class="card-body">
          <div class="btn-group">
            <button class="btn btn-primary" onClick={handleTriggerRing}>Test Ring</button>
          </div>
        </div>
      </div>
    </section>
  )
}
