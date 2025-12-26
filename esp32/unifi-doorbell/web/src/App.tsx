import { useState, useEffect } from 'preact/hooks'
import type { Tab } from './types'
import { useWebSocket } from './hooks/useWebSocket'
import { useConfig } from './hooks/useConfig'
import api from './api/api'

import { Login } from './components/Login'
import { ApSetup } from './components/ApSetup'
import { Header } from './components/Header'
import { Navigation } from './components/Navigation'
import { RingAlert } from './components/RingAlert'
import { StatusTab } from './components/StatusTab'
import { SetupWizard } from './components/SetupWizard'
import { SettingsTab } from './components/SettingsTab'
import { SystemTab } from './components/SystemTab'

type AppState = 'loading' | 'ap-setup' | 'login' | 'app'

export function App() {
  const [appState, setAppState] = useState<AppState>('loading')
  const [activeTab, setActiveTab] = useState<Tab>('status')
  const [version, setVersion] = useState('--')

  const {
    connected, status, ringing, logs,
    connect, disconnect, clearLogs
  } = useWebSocket()

  const {
    config, gpios, mqttTriggers, loadConfig,
    addGpio, removeGpio, updateGpio,
    addMqttTrigger, removeMqttTrigger, updateMqttTrigger
  } = useConfig()

  useEffect(() => {
    checkMode()
  }, [])

  const checkMode = async () => {
    try {
      const modeData = await api.getMode()
      if (modeData.apMode) {
        setAppState('ap-setup')
      } else {
        checkAuth()
      }
    } catch {
      checkAuth()
    }
  }

  const checkAuth = async () => {
    try {
      const authData = await api.getAuthStatus()
      if (authData.authenticated) {
        onLoginSuccess()
      } else {
        setAppState('login')
      }
    } catch {
      setAppState('login')
    }
  }

  const onLoginSuccess = async () => {
    setAppState('app')
    connect()
    loadConfig()
    try {
      const versionData = await api.getVersion()
      setVersion(`FW: ${versionData.version ?? '--'} | UI: 1.0.0`)
    } catch {
      setVersion('--')
    }
  }

  const handleLogout = async () => {
    await api.logout()
    disconnect()
    setAppState('login')
  }

  const handleTabChange = (tab: Tab) => {
    setActiveTab(tab)
  }

  if (appState === 'loading') {
    return (
      <div class="login-container">
        <div class="spinner" style={{ width: '40px', height: '40px' }}></div>
      </div>
    )
  }

  if (appState === 'ap-setup') {
    return <ApSetup />
  }

  if (appState === 'login') {
    return <Login onSuccess={onLoginSuccess} />
  }

  return (
    <div class="app-container">
      <Header connected={connected} onLogout={handleLogout} />
      <Navigation activeTab={activeTab} onTabChange={handleTabChange} />
      <RingAlert active={ringing} />

      <main class="app-content">
        {activeTab === 'status' && (
          <StatusTab status={status} ringing={ringing} />
        )}

        {activeTab === 'setup' && (
          <SetupWizard config={config} />
        )}

        {activeTab === 'settings' && (
          <SettingsTab
            config={config}
            gpios={gpios}
            mqttTriggers={mqttTriggers}
            onAddGpio={addGpio}
            onRemoveGpio={removeGpio}
            onUpdateGpio={updateGpio}
            onAddMqttTrigger={addMqttTrigger}
            onRemoveMqttTrigger={removeMqttTrigger}
            onUpdateMqttTrigger={updateMqttTrigger}
            onReload={loadConfig}
          />
        )}

        {activeTab === 'system' && (
          <SystemTab version={version} logs={logs} onClearLogs={clearLogs} />
        )}
      </main>
    </div>
  )
}
