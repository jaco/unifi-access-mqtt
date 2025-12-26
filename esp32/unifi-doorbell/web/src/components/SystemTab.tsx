import { useState, useRef, useEffect } from 'preact/hooks'
import api from '../api/api'

interface LogEntry {
  timestamp: string
  message: string
}

interface SystemTabProps {
  version: string
  logs: LogEntry[]
  onClearLogs: () => void
}

export function SystemTab({ version, logs, onClearLogs }: SystemTabProps) {
  const [selectedFile, setSelectedFile] = useState<File | null>(null)
  const [uploading, setUploading] = useState(false)
  const [uploadProgress, setUploadProgress] = useState(0)
  const [uploadStatus, setUploadStatus] = useState('')
  const [autoScroll, setAutoScroll] = useState(true)
  const logContainerRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (autoScroll && logContainerRef.current) {
      logContainerRef.current.scrollTop = logContainerRef.current.scrollHeight
    }
  }, [logs, autoScroll])

  const handleFileSelect = (e: Event) => {
    const input = e.target as HTMLInputElement
    if (input.files && input.files[0]) {
      setSelectedFile(input.files[0])
    }
  }

  const handleUpload = async () => {
    if (!selectedFile) {
      alert('Please select a firmware file')
      return
    }

    if (!selectedFile.name.endsWith('.bin')) {
      alert('Please select a .bin file')
      return
    }

    if (!confirm('Upload firmware and update? The device will reboot after update.')) {
      return
    }

    setUploading(true)
    setUploadProgress(0)
    setUploadStatus('Uploading...')

    try {
      const result = await api.uploadFirmware(selectedFile, (percent) => {
        setUploadProgress(percent)
        setUploadStatus(`Uploading: ${percent}%`)
      })

      if (result.success) {
        setUploadStatus('Update complete! Rebooting...')
        setTimeout(() => location.reload(), 10000)
      } else {
        alert('Update failed: ' + (result.message ?? 'Unknown error'))
        setUploading(false)
        setUploadStatus('')
      }
    } catch (err) {
      alert('Upload failed: ' + (err as Error).message)
      setUploading(false)
      setUploadStatus('')
    }
  }

  const handleReboot = async () => {
    if (!confirm('Reboot the device?')) return
    try {
      await api.reboot()
      alert('Device is rebooting...')
    } catch {
      // Expected - device reboots and connection drops
    }
  }

  const handleReset = async () => {
    if (!confirm('This will erase all configuration and reboot. Continue?')) return
    if (!confirm('Are you sure? This cannot be undone!')) return
    try {
      await api.factoryReset()
      alert('Configuration reset. Device is rebooting...')
    } catch {
      // Expected - device reboots and connection drops
    }
  }

  const escapeHtml = (text: string) => {
    const div = document.createElement('div')
    div.textContent = text
    return div.innerHTML
  }

  return (
    <section>
      <div class="card">
        <div class="card-header">
          <h2>Firmware Update</h2>
        </div>
        <div class="card-body">
          <p style={{ marginBottom: '16px' }}>
            Version: <strong style={{ fontFamily: 'monospace', fontSize: '12px' }}>{version}</strong>
          </p>
          <div class="form-group">
            <label>Select firmware file (.bin)</label>
            <input
              type="file"
              accept=".bin"
              onChange={handleFileSelect}
              disabled={uploading}
            />
          </div>
          {uploading && (
            <div style={{ marginBottom: '16px' }}>
              <div class="progress-bar">
                <div class="progress-fill" style={{ width: `${uploadProgress}%` }}></div>
              </div>
              <p class="form-hint" style={{ marginTop: '8px' }}>{uploadStatus}</p>
            </div>
          )}
          <button class="btn btn-primary" onClick={handleUpload} disabled={uploading || !selectedFile}>
            Upload Firmware
          </button>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>System Actions</h2>
        </div>
        <div class="card-body">
          <div class="btn-group">
            <button class="btn btn-secondary" onClick={handleReboot}>Reboot Device</button>
            <button class="btn btn-danger" onClick={handleReset}>Factory Reset</button>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>System Log</h2>
        </div>
        <div class="card-body">
          <div
            ref={logContainerRef}
            class="log-container"
          >
            {logs.map((log, i) => (
              <div key={i}>
                <span style={{ color: 'var(--text-muted)' }}>{log.timestamp}</span>{' '}
                <span dangerouslySetInnerHTML={{ __html: escapeHtml(log.message) }} />
              </div>
            ))}
          </div>
          <div style={{ display: 'flex', gap: '12px', alignItems: 'center', marginTop: '12px' }}>
            <button class="btn btn-secondary" onClick={onClearLogs}>Clear</button>
            <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer' }}>
              <input
                type="checkbox"
                checked={autoScroll}
                onChange={(e) => setAutoScroll((e.target as HTMLInputElement).checked)}
                style={{ width: '16px', height: '16px', accentColor: 'var(--accent-blue)' }}
              />
              Auto-scroll
            </label>
          </div>
        </div>
      </div>
    </section>
  )
}
