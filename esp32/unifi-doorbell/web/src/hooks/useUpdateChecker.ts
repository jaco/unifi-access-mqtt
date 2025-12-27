import { useState, useEffect, useCallback } from 'preact/hooks'
import type { UpdateInfo, VersionInfo } from '../types'
import api from '../api/api'

const CHECK_INTERVAL = 60 * 60 * 1000 // Check every hour

function compareVersions(current: string, latest: string): number {
  // Handle 'dev' version - always consider it older
  if (current === 'dev') return -1
  if (latest === 'dev') return 1

  const currentParts = current.replace(/^v/, '').split('.').map(Number)
  const latestParts = latest.replace(/^v/, '').split('.').map(Number)

  for (let i = 0; i < Math.max(currentParts.length, latestParts.length); i++) {
    const c = currentParts[i] ?? 0
    const l = latestParts[i] ?? 0
    if (c < l) return -1
    if (c > l) return 1
  }
  return 0
}

export function useUpdateChecker() {
  const [versionInfo, setVersionInfo] = useState<VersionInfo | null>(null)
  const [updateInfo, setUpdateInfo] = useState<UpdateInfo | null>(null)
  const [checking, setChecking] = useState(false)
  const [lastChecked, setLastChecked] = useState<Date | null>(null)

  const checkForUpdate = useCallback(async () => {
    setChecking(true)
    try {
      const version = await api.getVersion()
      setVersionInfo(version)

      if (!version.board || version.board === 'unknown') {
        setUpdateInfo(null)
        return
      }

      const manifest = await api.getLatestManifest(version.board)
      if (!manifest) {
        setUpdateInfo({
          available: false,
          currentVersion: version.version,
          latestVersion: 'unknown',
          board: version.board
        })
        return
      }

      const comparison = compareVersions(version.version, manifest.version)
      setUpdateInfo({
        available: comparison < 0,
        currentVersion: version.version,
        latestVersion: manifest.version,
        board: version.board,
        manifestUrl: `https://mqtt-home.github.io/unifi-access-mqtt/`
      })
      setLastChecked(new Date())
    } catch (err) {
      console.error('Update check failed:', err)
    } finally {
      setChecking(false)
    }
  }, [])

  // Check on mount and periodically
  useEffect(() => {
    checkForUpdate()
    const interval = setInterval(checkForUpdate, CHECK_INTERVAL)
    return () => clearInterval(interval)
  }, [checkForUpdate])

  return {
    versionInfo,
    updateInfo,
    checking,
    lastChecked,
    checkForUpdate
  }
}
