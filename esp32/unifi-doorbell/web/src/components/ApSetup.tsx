import { useState } from 'preact/hooks'
import { DoorbellIcon } from './DoorbellIcon'
import api from '../api/api'

export function ApSetup() {
  const [ssid, setSsid] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [success, setSuccess] = useState('')
  const [loading, setLoading] = useState(false)
  const [finalMessage, setFinalMessage] = useState<{ ip?: string } | null>(null)

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    setError('')
    setSuccess('')

    if (!ssid.trim()) {
      setError('Please enter your WiFi network name')
      return
    }

    setLoading(true)

    try {
      await api.testWifi(ssid, password)

      // Poll for connection status
      let testResult = null
      for (let i = 0; i < 35; i++) {
        await new Promise(resolve => setTimeout(resolve, 500))
        const status = await api.getWifiStatus()

        if (status.status === 'success') {
          testResult = status
          break
        } else if (status.status === 'failed') {
          testResult = status
          break
        }
      }

      if (!testResult) {
        testResult = { success: false, message: 'Connection timed out' }
      }

      if (!testResult.success) {
        setError(testResult.message ?? 'Could not connect to WiFi. Please check your credentials.')
        setLoading(false)
        return
      }

      setSuccess('Connection successful! IP: ' + (testResult.ip ?? 'assigned'))
      await new Promise(resolve => setTimeout(resolve, 1000))

      // Save and reboot
      api.setupWifi(ssid, password).catch(() => {})

      setTimeout(() => {
        setFinalMessage({ ip: testResult.ip })
      }, 2000)

    } catch (err) {
      setError('Error: ' + (err as Error).message)
      setLoading(false)
    }
  }

  if (finalMessage) {
    return (
      <div class="login-container">
        <div class="login-card">
          <div class="login-logo">
            <DoorbellIcon size={64} />
            <h1>WiFi Setup</h1>
          </div>
          <div class="alert alert-success" style={{ flexDirection: 'column', alignItems: 'flex-start' }}>
            <div>Device is rebooting.</div>
            <div style={{ marginTop: '8px' }}>Connect to your WiFi and open:</div>
            <a href="http://doorbell.local" style={{ color: 'var(--accent-blue)' }}>doorbell.local</a>
            {finalMessage.ip && (
              <a href={`http://${finalMessage.ip}`} style={{ color: 'var(--accent-blue)' }}>{finalMessage.ip}</a>
            )}
            <button class="btn btn-primary" onClick={() => location.reload()} style={{ marginTop: '12px' }}>
              Reload
            </button>
          </div>
        </div>
      </div>
    )
  }

  return (
    <div class="login-container">
      <div class="login-card">
        <div class="login-logo">
          <DoorbellIcon size={64} />
          <h1>WiFi Setup</h1>
        </div>
        <p style={{ textAlign: 'center', marginBottom: '24px', color: 'var(--text-secondary)' }}>
          Connect your UniFi Doorbell to your WiFi network
        </p>
        <form onSubmit={handleSubmit}>
          <div class="form-group">
            <label for="ap-wifi-ssid">WiFi Network Name (SSID)</label>
            <input
              type="text"
              id="ap-wifi-ssid"
              placeholder="Your WiFi network"
              value={ssid}
              onInput={(e) => setSsid((e.target as HTMLInputElement).value)}
              required
            />
          </div>
          <div class="form-group">
            <label for="ap-wifi-password">WiFi Password</label>
            <input
              type="password"
              id="ap-wifi-password"
              placeholder="WiFi password"
              value={password}
              onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
            />
          </div>
          {error && <div class="alert alert-error">{error}</div>}
          {success && <div class="alert alert-success">{success}</div>}
          <button type="submit" class="btn btn-primary" style={{ width: '100%' }} disabled={loading}>
            {loading ? <><span class="spinner"></span> Testing connection...</> : 'Connect'}
          </button>
        </form>
        <p style={{ textAlign: 'center', marginTop: '24px', color: 'var(--text-muted)', fontSize: '13px' }}>
          After connecting, access the device at<br/><strong>http://doorbell.local</strong>
        </p>
      </div>
    </div>
  )
}
