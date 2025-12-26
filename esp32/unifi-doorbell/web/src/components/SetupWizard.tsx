import { useState, useEffect } from 'preact/hooks'
import type { Config, TopologyDevice } from '../types'
import { CheckIcon } from './DoorbellIcon'
import api from '../api/api'

interface SetupWizardProps {
  config: Config
}

export function SetupWizard({ config }: SetupWizardProps) {
  const [step, setStep] = useState(1)
  const [host, setHost] = useState(config.unifi?.host ?? '')
  const [username, setUsername] = useState(config.unifi?.username ?? '')
  const [password, setPassword] = useState('')
  const [hasExistingPassword, setHasExistingPassword] = useState(false)
  const [certificate, setCertificate] = useState('')
  const [certStatus, setCertStatus] = useState<{ type: string; message: string } | null>(null)
  const [fetchingCert, setFetchingCert] = useState(false)
  const [readers, setReaders] = useState<TopologyDevice[]>([])
  const [loadingReaders, setLoadingReaders] = useState(false)
  const [readersError, setReadersError] = useState('')
  const [selectedReader, setSelectedReader] = useState(config.doorbell?.deviceId ?? '')
  const [manualReaderId, setManualReaderId] = useState('')
  const [deviceName, setDeviceName] = useState(config.doorbell?.deviceName ?? '')
  const [doorName, setDoorName] = useState(config.doorbell?.doorName ?? '')
  const [saving, setSaving] = useState(false)
  const [saveStatus, setSaveStatus] = useState<{ type: string; message: string } | null>(null)

  useEffect(() => {
    setHost(config.unifi?.host ?? '')
    setUsername(config.unifi?.username ?? '')
    setSelectedReader(config.doorbell?.deviceId ?? '')
    setDeviceName(config.doorbell?.deviceName ?? '')
    setDoorName(config.doorbell?.doorName ?? '')
    if (config.unifi?.password?.includes('*')) {
      setHasExistingPassword(true)
    }
  }, [config])

  useEffect(() => {
    api.getCertificate().then(data => {
      if (data.certificate) setCertificate(data.certificate)
    })
  }, [])

  useEffect(() => {
    if (step === 3) {
      loadTopology()
    }
  }, [step])

  const loadTopology = async () => {
    setLoadingReaders(true)
    setReadersError('')
    try {
      const data = await api.getTopology()
      if (!data.success) {
        throw new Error(data.message ?? 'Failed to load devices')
      }
      setReaders(data.readers ?? [])
    } catch (err) {
      setReadersError((err as Error).message)
    } finally {
      setLoadingReaders(false)
    }
  }

  const handleFetchCert = async () => {
    setFetchingCert(true)
    setCertStatus(null)
    try {
      const data = await api.fetchCertificate()
      if (data.success && data.certificate) {
        setCertificate(data.certificate)
        await api.saveCertificate(data.certificate)
        setCertStatus({ type: 'success', message: 'Certificate fetched and saved successfully!' })
      } else {
        setCertStatus({ type: 'error', message: 'Failed: ' + (data.message ?? 'Unknown error') })
      }
    } catch (err) {
      setCertStatus({ type: 'error', message: 'Error: ' + (err as Error).message })
    } finally {
      setFetchingCert(false)
    }
  }

  const handleTestConnection = async () => {
    setCertStatus({ type: 'info', message: 'Testing connection...' })
    const result = await api.testConnection()
    setCertStatus({ type: result.success ? 'success' : 'error', message: result.message })
  }

  const handleNext = async () => {
    if (step === 1) {
      if (!host || !username || (!password && !hasExistingPassword)) {
        alert('Please fill in all UniFi Access fields')
        return
      }
      const configUpdate: Partial<Config> = {
        unifi: { host, username }
      }
      if (password) {
        configUpdate.unifi!.password = password
      }
      await api.saveConfig(configUpdate)
    }

    if (step === 2 && certificate.includes('BEGIN CERTIFICATE')) {
      await api.saveCertificate(certificate)
    }

    if (step === 3 && !selectedReader) {
      alert('Please select a doorbell device')
      return
    }

    setStep(step + 1)
  }

  const handleSetManualReader = () => {
    if (!manualReaderId.trim()) {
      alert('Please enter a device ID or MAC address')
      return
    }
    setSelectedReader(manualReaderId)
  }

  const handleSaveAndReboot = async () => {
    setSaving(true)
    setSaveStatus({ type: 'info', message: 'Saving configuration...' })

    const configUpdate: Partial<Config> = {
      unifi: { host, username },
      doorbell: {
        deviceId: selectedReader,
        deviceName,
        doorName
      }
    }

    if (password) {
      configUpdate.unifi!.password = password
    }

    try {
      await api.saveConfig(configUpdate)
      setSaveStatus({ type: 'success', message: 'Configuration saved! Rebooting device...' })
      await api.reboot()
      setSaveStatus({ type: 'success', message: 'Device is rebooting. Page will refresh in 10 seconds...' })
      setTimeout(() => location.reload(), 10000)
    } catch (err) {
      setSaveStatus({ type: 'error', message: 'Error: ' + (err as Error).message })
      setSaving(false)
    }
  }

  const getStepClass = (s: number) => {
    if (s < step) return 'wizard-step complete'
    if (s === step) return 'wizard-step active'
    return 'wizard-step'
  }

  return (
    <div class="wizard">
      <div class="wizard-progress">
        {[1, 2, 3, 4].map(s => (
          <div
            key={s}
            class={getStepClass(s)}
            onClick={() => s < step && setStep(s)}
            style={{ cursor: s < step ? 'pointer' : 'default' }}
          >
            <div class="step-number">{s}</div>
            <div class="step-label">
              {s === 1 ? 'UniFi Access' : s === 2 ? 'Certificate' : s === 3 ? 'Doorbell' : 'Complete'}
            </div>
          </div>
        ))}
      </div>

      {step === 1 && (
        <div class="wizard-content">
          <h3 style={{ marginBottom: '8px' }}>Connect to UniFi Access</h3>
          <p class="form-hint" style={{ marginBottom: '24px' }}>
            Create a local user in UniFi Access (Settings &gt; Admins &gt; Add Admin) with at least "View Only" permissions.
          </p>
          <div class="form-group">
            <label>UniFi Access Host</label>
            <input
              type="text"
              placeholder="192.168.1.1 or unifi.local"
              value={host}
              onInput={(e) => setHost((e.target as HTMLInputElement).value)}
            />
            <p class="form-hint">IP address or hostname of your UniFi Access controller</p>
          </div>
          <div class="form-group">
            <label>Username</label>
            <input
              type="text"
              placeholder="doorbell-api"
              value={username}
              onInput={(e) => setUsername((e.target as HTMLInputElement).value)}
            />
          </div>
          <div class="form-group">
            <label>Password</label>
            <input
              type="password"
              placeholder={hasExistingPassword ? 'Password is set (leave blank to keep)' : 'Password'}
              value={password}
              onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
            />
          </div>
          <div class="wizard-actions">
            <div></div>
            <button class="btn btn-primary" onClick={handleNext}>Next</button>
          </div>
        </div>
      )}

      {step === 2 && (
        <div class="wizard-content">
          <h3 style={{ marginBottom: '8px' }}>SSL Certificate</h3>
          <p class="form-hint" style={{ marginBottom: '24px' }}>
            UniFi Access uses a self-signed certificate. We need to trust it for secure communication.
          </p>
          <div class="form-group">
            <div class="btn-group" style={{ marginBottom: '16px' }}>
              <button class="btn btn-primary" onClick={handleFetchCert} disabled={fetchingCert}>
                {fetchingCert && <span class="spinner"></span>}
                Fetch Certificate Automatically
              </button>
            </div>
            <p class="form-hint">
              Or extract manually: <code>openssl s_client -connect {host || 'HOST'}:443 -showcerts &lt;/dev/null 2&gt;/dev/null | openssl x509 -outform PEM</code>
            </p>
          </div>
          <div class="form-group">
            <label>Certificate (PEM format)</label>
            <textarea
              rows={8}
              placeholder={'-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----'}
              value={certificate}
              onInput={(e) => setCertificate((e.target as HTMLTextAreaElement).value)}
            />
          </div>
          {certStatus && (
            <div class={`alert alert-${certStatus.type}`}>{certStatus.message}</div>
          )}
          <div class="wizard-actions">
            <button class="btn btn-secondary" onClick={() => setStep(1)}>Back</button>
            <div class="btn-group">
              <button class="btn btn-secondary" onClick={handleTestConnection}>Test Connection</button>
              <button class="btn btn-primary" onClick={handleNext}>Next</button>
            </div>
          </div>
        </div>
      )}

      {step === 3 && (
        <div class="wizard-content">
          <h3 style={{ marginBottom: '8px' }}>Select Reader Device</h3>
          <p class="form-hint" style={{ marginBottom: '24px' }}>
            Choose which UniFi Access device will be used as the doorbell.
          </p>

          {loadingReaders && (
            <div class="alert alert-info">
              <div class="spinner"></div>
              Loading devices from UniFi Access...
            </div>
          )}

          {!loadingReaders && readers.length > 0 && (
            <div class="device-list">
              {readers.map(device => (
                <div
                  key={device.id}
                  class={`device-item ${selectedReader === device.id ? 'selected' : ''}`}
                  onClick={() => {
                    setSelectedReader(device.id)
                    if (!deviceName) setDeviceName(device.name)
                  }}
                >
                  <input
                    type="radio"
                    name="reader"
                    checked={selectedReader === device.id}
                    onChange={() => {}}
                  />
                  <div class="device-info">
                    <div class="name">{device.name || 'Unnamed Device'}</div>
                    <div class="meta">{device.type} - {device.mac}</div>
                  </div>
                </div>
              ))}
            </div>
          )}

          {!loadingReaders && readersError && (
            <div class="alert alert-error">
              <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
                <div>Failed to load devices: {readersError}</div>
                <div style={{ display: 'flex', gap: '12px' }}>
                  <button class="btn btn-primary" onClick={loadTopology}>Retry</button>
                  <span style={{ color: 'var(--text-muted)' }}>or enter a device ID/MAC manually below</span>
                </div>
              </div>
            </div>
          )}

          {!loadingReaders && readers.length === 0 && !readersError && (
            <div class="alert alert-error">
              No reader devices found. You can enter a device ID/MAC manually below.
            </div>
          )}

          {(readers.length === 0 || readersError) && (
            <div style={{ marginTop: '20px' }}>
              <div class="form-group">
                <label>Manual Entry (Device ID or MAC)</label>
                <div style={{ display: 'flex', gap: '8px' }}>
                  <input
                    type="text"
                    placeholder="e.g., aa:bb:cc:dd:ee:ff or device-uuid"
                    value={manualReaderId}
                    onInput={(e) => setManualReaderId((e.target as HTMLInputElement).value)}
                  />
                  <button class="btn btn-primary" onClick={handleSetManualReader}>Set</button>
                </div>
                <p class="form-hint">Enter the device ID (UUID) or MAC address.</p>
              </div>
            </div>
          )}

          <div class="form-group" style={{ marginTop: '20px' }}>
            <label>Device Name (optional)</label>
            <input
              type="text"
              placeholder="Front Door"
              value={deviceName}
              onInput={(e) => setDeviceName((e.target as HTMLInputElement).value)}
            />
          </div>
          <div class="form-group">
            <label>Door Name (optional)</label>
            <input
              type="text"
              placeholder="Main Entrance"
              value={doorName}
              onInput={(e) => setDoorName((e.target as HTMLInputElement).value)}
            />
          </div>

          <div class="wizard-actions">
            <button class="btn btn-secondary" onClick={() => setStep(2)}>Back</button>
            <button class="btn btn-primary" onClick={handleNext}>Next</button>
          </div>
        </div>
      )}

      {step === 4 && (
        <div class="wizard-content">
          <div style={{ textAlign: 'center', padding: '20px 0' }}>
            <CheckIcon />
            <h3 style={{ marginTop: '16px' }}>Setup Complete!</h3>
            <p class="form-hint" style={{ marginTop: '8px' }}>
              Your UniFi Doorbell is configured and ready to use.
            </p>
          </div>

          <div class="alert alert-info" style={{ flexDirection: 'column', alignItems: 'flex-start' }}>
            <strong>Configuration Summary:</strong>
            <div style={{ marginTop: '12px', color: 'var(--text-secondary)' }}>
              <div style={{ marginBottom: '8px' }}><strong>UniFi Host:</strong><br/>{host}</div>
              <div><strong>Doorbell Device:</strong><br/>{
                readers.find(r => r.id === selectedReader)?.name ?? selectedReader ?? 'Not selected'
              }</div>
            </div>
          </div>

          {saveStatus && (
            <div class={`alert alert-${saveStatus.type}`} style={{ marginTop: '16px' }}>
              {saveStatus.message}
            </div>
          )}

          <div class="wizard-actions">
            <button class="btn btn-secondary" onClick={() => setStep(3)} disabled={saving}>Back</button>
            <button class="btn btn-primary" onClick={handleSaveAndReboot} disabled={saving}>
              {saving ? 'Rebooting...' : 'Save & Reboot'}
            </button>
          </div>
        </div>
      )}
    </div>
  )
}
