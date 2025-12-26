import { useState, useEffect } from 'preact/hooks'
import type { Config, GpioConfig, MqttTrigger } from '../types'
import api from '../api/api'

interface SettingsTabProps {
  config: Config
  gpios: GpioConfig[]
  mqttTriggers: MqttTrigger[]
  onAddGpio: (gpio: GpioConfig) => void
  onRemoveGpio: (index: number) => void
  onUpdateGpio: (index: number, field: keyof GpioConfig, value: unknown) => void
  onAddMqttTrigger: (trigger: MqttTrigger) => void
  onRemoveMqttTrigger: (index: number) => void
  onUpdateMqttTrigger: (index: number, field: keyof MqttTrigger, value: unknown) => void
  onReload: () => void
}

const gpioPresets: Record<string, { name: string; pins: Omit<GpioConfig, 'enabled' | 'debounceMs' | 'holdMs'>[] }> = {
  'olimex-poe': {
    name: 'Olimex ESP32-POE',
    pins: [
      { pin: 34, label: 'BUT1 Button', action: 'ring_button', pullMode: 'up' },
      { pin: 36, label: 'GPIO36', action: 'door_contact', pullMode: 'up' },
      { pin: 39, label: 'GPIO39', action: 'generic', pullMode: 'up' },
      { pin: 32, label: 'GPIO32', action: 'generic', pullMode: 'up' },
      { pin: 33, label: 'GPIO33', action: 'generic', pullMode: 'up' },
      { pin: 35, label: 'GPIO35', action: 'generic', pullMode: 'up' }
    ]
  },
  'esp32-s3-zero': {
    name: 'ESP32-S3-Zero',
    pins: [
      { pin: 0, label: 'BOOT Button', action: 'ring_button', pullMode: 'up' },
      { pin: 1, label: 'GPIO1', action: 'door_contact', pullMode: 'up' },
      { pin: 2, label: 'GPIO2', action: 'generic', pullMode: 'up' },
      { pin: 3, label: 'GPIO3', action: 'generic', pullMode: 'up' },
      { pin: 4, label: 'GPIO4', action: 'generic', pullMode: 'up' }
    ]
  }
}

export function SettingsTab({
  config, gpios, mqttTriggers,
  onAddGpio, onRemoveGpio, onUpdateGpio,
  onAddMqttTrigger, onRemoveMqttTrigger, onUpdateMqttTrigger,
  onReload
}: SettingsTabProps) {
  const [wifiSsid, setWifiSsid] = useState(config.network?.wifiSsid ?? '')
  const [wifiPassword, setWifiPassword] = useState('')
  const [mqttEnabled, setMqttEnabled] = useState(config.mqtt?.enabled ?? false)
  const [mqttServer, setMqttServer] = useState(config.mqtt?.server ?? '')
  const [mqttPort, setMqttPort] = useState(config.mqtt?.port ?? 1883)
  const [mqttTopic, setMqttTopic] = useState(config.mqtt?.topic ?? '')
  const [mqttAuth, setMqttAuth] = useState(config.mqtt?.authEnabled ?? false)
  const [mqttUsername, setMqttUsername] = useState(config.mqtt?.username ?? '')
  const [mqttPassword, setMqttPassword] = useState('')
  const [webUsername, setWebUsername] = useState(config.web?.username ?? '')
  const [webPassword, setWebPassword] = useState('')
  const [showGpioMenu, setShowGpioMenu] = useState(false)
  const [selectedBoard, setSelectedBoard] = useState('olimex-poe')
  const [showWiringDiagram, setShowWiringDiagram] = useState<'up' | 'down' | null>(null)
  const isEthernet = config.network?.wifiSsid === undefined

  const wiringDiagrams = {
    up: `
┌─────────────────────────────────┐
│           ESP32                 │
│                                 │
│  3.3V ──┬── GPIO (internal)     │
│         │   pull-up resistor    │
│         R   ~45kΩ               │
│         │                       │
│  GPIO ──┴──────┬────────────    │
│                │                │
└────────────────│────────────────┘
                 │
              [Button]
                 │
                GND

When button is OPEN:  GPIO reads HIGH (3.3V)
When button is PRESSED: GPIO reads LOW (GND)

Wiring: Connect one side of button/switch to GPIO,
        other side to GND.
`,
    down: `
┌─────────────────────────────────┐
│           ESP32                 │
│                                 │
│  GND ───┬── GPIO (internal)     │
│         │   pull-down resistor  │
│         R   ~45kΩ               │
│         │                       │
│  GPIO ──┴──────┬────────────    │
│                │                │
└────────────────│────────────────┘
                 │
              [Button]
                 │
               3.3V

When button is OPEN:  GPIO reads LOW (GND)
When button is PRESSED: GPIO reads HIGH (3.3V)

Wiring: Connect one side of button/switch to GPIO,
        other side to 3.3V.
`
  }

  useEffect(() => {
    setWifiSsid(config.network?.wifiSsid ?? '')
    setMqttEnabled(config.mqtt?.enabled ?? false)
    setMqttServer(config.mqtt?.server ?? '')
    setMqttPort(config.mqtt?.port ?? 1883)
    setMqttTopic(config.mqtt?.topic ?? '')
    setMqttAuth(config.mqtt?.authEnabled ?? false)
    setMqttUsername(config.mqtt?.username ?? '')
    setWebUsername(config.web?.username ?? '')
  }, [config])

  const handleSave = async () => {
    const result = await api.saveConfig({
      network: { wifiSsid, wifiPassword },
      mqtt: {
        enabled: mqttEnabled,
        server: mqttServer,
        port: mqttPort,
        topic: mqttTopic,
        authEnabled: mqttAuth,
        username: mqttUsername,
        password: mqttPassword
      },
      web: { username: webUsername, password: webPassword },
      gpios,
      mqttTriggers
    })

    if (result.success) {
      alert('Settings saved! Reboot to apply changes.')
    } else {
      alert('Failed to save: ' + (result.message ?? 'Unknown error'))
    }
  }

  const handleAddGpioFromPreset = (pin: typeof gpioPresets['olimex-poe']['pins'][0]) => {
    if (gpios.length >= 8) {
      alert('Maximum 8 GPIO pins allowed')
      return
    }
    if (gpios.some(g => g.pin === pin.pin)) {
      alert(`GPIO ${pin.pin} is already configured`)
      return
    }
    onAddGpio({
      enabled: true,
      pin: pin.pin,
      action: pin.action,
      pullMode: pin.pullMode,
      label: pin.label,
      debounceMs: 50,
      holdMs: 100
    })
    setShowGpioMenu(false)
  }

  const handleAddCustomGpio = () => {
    if (gpios.length >= 8) {
      alert('Maximum 8 GPIO pins allowed')
      return
    }
    onAddGpio({
      enabled: true,
      pin: 34,
      action: 'ring_button',
      pullMode: 'up',
      label: 'New GPIO',
      debounceMs: 50,
      holdMs: 100
    })
    setShowGpioMenu(false)
  }

  const handleAddMqttTrigger = () => {
    if (mqttTriggers.length >= 4) {
      alert('Maximum 4 MQTT triggers allowed')
      return
    }
    onAddMqttTrigger({
      enabled: true,
      topic: '',
      jsonField: 'contact',
      triggerValue: 'false',
      action: 'dismiss',
      label: 'New Trigger'
    })
  }

  const getGpioHelpText = (gpio: GpioConfig) => {
    const pullDesc = gpio.pullMode === 'up'
      ? 'Connect button between GPIO and GND.'
      : 'Connect button between GPIO and 3.3V.'

    switch (gpio.action) {
      case 'ring_button':
        return `Ring Button: Triggers a doorbell ring when pressed. ${pullDesc}`
      case 'door_contact':
        return `Door Contact: Dismisses active call when triggered. ${pullDesc}`
      case 'generic':
        const topic = mqttTopic || 'doorbell'
        const label = (gpio.label || 'gpio').toLowerCase().replace(/\s+/g, '_')
        return `Generic (MQTT): Publishes to ${topic}/gpio/${label}. ${pullDesc}`
      default:
        return pullDesc
    }
  }

  const getMqttTriggerHelpText = (trigger: MqttTrigger) => {
    const actionText = trigger.action === 'ring'
      ? 'trigger a doorbell ring'
      : 'dismiss the active call'

    if (!trigger.topic || !trigger.jsonField) {
      return 'Configure the topic and JSON field to subscribe to.'
    }

    return `When ${trigger.topic} receives a message with "${trigger.jsonField}": ${trigger.triggerValue}, this will ${actionText}.`
  }

  return (
    <section>
      {!isEthernet && (
        <div class="card">
          <div class="card-header">
            <h2>WiFi Network</h2>
          </div>
          <div class="card-body">
            <div class="form-group">
              <label>SSID</label>
              <input
                type="text"
                placeholder="Network name"
                value={wifiSsid}
                onInput={(e) => setWifiSsid((e.target as HTMLInputElement).value)}
              />
            </div>
            <div class="form-group">
              <label>Password</label>
              <input
                type="password"
                placeholder="Leave blank to keep current"
                value={wifiPassword}
                onInput={(e) => setWifiPassword((e.target as HTMLInputElement).value)}
              />
            </div>
          </div>
        </div>
      )}

      <div class="card">
        <div class="card-header">
          <h2>MQTT Integration</h2>
          <p>Optional: Publish doorbell events to an MQTT broker</p>
        </div>
        <div class="card-body">
          <div class="form-group">
            <div class="toggle-group">
              <label class="toggle">
                <input
                  type="checkbox"
                  checked={mqttEnabled}
                  onChange={(e) => setMqttEnabled((e.target as HTMLInputElement).checked)}
                />
                <span class="toggle-slider"></span>
              </label>
              <span>Enable MQTT</span>
            </div>
          </div>

          {mqttEnabled && (
            <>
              <div class="form-group">
                <label>Server</label>
                <input
                  type="text"
                  placeholder="mqtt.local"
                  value={mqttServer}
                  onInput={(e) => setMqttServer((e.target as HTMLInputElement).value)}
                />
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
                <div class="form-group">
                  <label>Port</label>
                  <input
                    type="number"
                    min={1}
                    max={65535}
                    value={mqttPort}
                    onInput={(e) => setMqttPort(parseInt((e.target as HTMLInputElement).value) || 1883)}
                  />
                </div>
                <div class="form-group">
                  <label>Topic</label>
                  <input
                    type="text"
                    placeholder="home/doorbell"
                    value={mqttTopic}
                    onInput={(e) => setMqttTopic((e.target as HTMLInputElement).value)}
                  />
                </div>
              </div>
              <div class="form-group">
                <div class="toggle-group">
                  <label class="toggle">
                    <input
                      type="checkbox"
                      checked={mqttAuth}
                      onChange={(e) => setMqttAuth((e.target as HTMLInputElement).checked)}
                    />
                    <span class="toggle-slider"></span>
                  </label>
                  <span>Authentication Required</span>
                </div>
              </div>
              {mqttAuth && (
                <>
                  <div class="form-group">
                    <label>Username</label>
                    <input
                      type="text"
                      value={mqttUsername}
                      onInput={(e) => setMqttUsername((e.target as HTMLInputElement).value)}
                    />
                  </div>
                  <div class="form-group">
                    <label>Password</label>
                    <input
                      type="password"
                      placeholder="Leave blank to keep current"
                      value={mqttPassword}
                      onInput={(e) => setMqttPassword((e.target as HTMLInputElement).value)}
                    />
                  </div>
                </>
              )}
            </>
          )}
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>Web Interface Security</h2>
        </div>
        <div class="card-body">
          <div class="form-group">
            <label>Username</label>
            <input
              type="text"
              value={webUsername}
              onInput={(e) => setWebUsername((e.target as HTMLInputElement).value)}
            />
          </div>
          <div class="form-group">
            <label>New Password</label>
            <input
              type="password"
              placeholder="Leave blank to keep current"
              value={webPassword}
              onInput={(e) => setWebPassword((e.target as HTMLInputElement).value)}
            />
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>GPIO Configuration</h2>
          <p>Configure physical buttons and sensors connected to GPIO pins</p>
        </div>
        <div class="card-body">
          {gpios.map((gpio, index) => (
            <div key={index} class="gpio-item">
              <div class="gpio-header">
                <span class="gpio-title">GPIO {gpio.pin} - {gpio.label || 'Unnamed'}</span>
                <button class="btn btn-danger btn-sm" onClick={() => onRemoveGpio(index)}>Remove</button>
              </div>
              <div class="gpio-fields">
                <div class="form-group">
                  <label>Pin Number</label>
                  <input
                    type="number"
                    min={0}
                    max={48}
                    value={gpio.pin}
                    onChange={(e) => onUpdateGpio(index, 'pin', parseInt((e.target as HTMLInputElement).value))}
                  />
                </div>
                <div class="form-group">
                  <label>Label</label>
                  <input
                    type="text"
                    value={gpio.label}
                    onChange={(e) => onUpdateGpio(index, 'label', (e.target as HTMLInputElement).value)}
                  />
                </div>
                <div class="form-group">
                  <label>Action</label>
                  <select
                    value={gpio.action}
                    onChange={(e) => onUpdateGpio(index, 'action', (e.target as HTMLSelectElement).value)}
                  >
                    <option value="ring_button">Ring Button</option>
                    <option value="door_contact">Door Contact</option>
                    <option value="generic">Generic (MQTT)</option>
                  </select>
                </div>
                <div class="form-group">
                  <label>Pull Mode</label>
                  <select
                    value={gpio.pullMode}
                    onChange={(e) => onUpdateGpio(index, 'pullMode', (e.target as HTMLSelectElement).value)}
                  >
                    <option value="up">Pull-Up (Active LOW)</option>
                    <option value="down">Pull-Down (Active HIGH)</option>
                  </select>
                </div>
              </div>
              <div class="gpio-help" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', gap: '12px' }}>
                <span>{getGpioHelpText(gpio)}</span>
                <button
                  class="btn btn-secondary btn-sm"
                  onClick={() => setShowWiringDiagram(gpio.pullMode)}
                  title="Show wiring diagram"
                  style={{ padding: '4px 10px', fontSize: '14px', fontWeight: 'bold', flexShrink: 0 }}
                >?</button>
              </div>
            </div>
          ))}
          <button class="btn btn-secondary" onClick={() => setShowGpioMenu(true)} style={{ marginTop: '16px' }}>
            + Add GPIO
          </button>
        </div>
      </div>

      {mqttEnabled && (
        <div class="card">
          <div class="card-header">
            <h2>MQTT Triggers</h2>
            <p>Subscribe to MQTT topics and trigger actions based on JSON field values</p>
          </div>
          <div class="card-body">
            {mqttTriggers.length === 0 && (
              <p class="form-hint">No MQTT triggers configured. Add a trigger to react to MQTT messages.</p>
            )}
            {mqttTriggers.map((trigger, index) => (
              <div key={index} class="mqtt-trigger-item">
                <div class="mqtt-trigger-header">
                  <span class="mqtt-trigger-title">{trigger.label || 'Unnamed Trigger'}</span>
                  <button class="btn btn-danger btn-sm" onClick={() => onRemoveMqttTrigger(index)}>Remove</button>
                </div>
                <div class="mqtt-trigger-fields">
                  <div class="form-group">
                    <label>Label</label>
                    <input
                      type="text"
                      placeholder="Front Door Contact"
                      value={trigger.label}
                      onChange={(e) => onUpdateMqttTrigger(index, 'label', (e.target as HTMLInputElement).value)}
                    />
                  </div>
                  <div class="form-group">
                    <label>Topic</label>
                    <input
                      type="text"
                      placeholder="zigbee2mqtt/door_sensor"
                      value={trigger.topic}
                      onChange={(e) => onUpdateMqttTrigger(index, 'topic', (e.target as HTMLInputElement).value)}
                    />
                  </div>
                  <div class="form-group">
                    <label>JSON Field</label>
                    <input
                      type="text"
                      placeholder="contact"
                      value={trigger.jsonField}
                      onChange={(e) => onUpdateMqttTrigger(index, 'jsonField', (e.target as HTMLInputElement).value)}
                    />
                  </div>
                  <div class="form-group">
                    <label>Trigger Value</label>
                    <input
                      type="text"
                      placeholder="false"
                      value={trigger.triggerValue}
                      onChange={(e) => onUpdateMqttTrigger(index, 'triggerValue', (e.target as HTMLInputElement).value)}
                    />
                  </div>
                  <div class="form-group">
                    <label>Action</label>
                    <select
                      value={trigger.action}
                      onChange={(e) => onUpdateMqttTrigger(index, 'action', (e.target as HTMLSelectElement).value)}
                    >
                      <option value="dismiss">Dismiss Call</option>
                      <option value="ring">Ring Doorbell</option>
                    </select>
                  </div>
                </div>
                <div class="mqtt-trigger-help">{getMqttTriggerHelpText(trigger)}</div>
              </div>
            ))}
            <button class="btn btn-secondary" onClick={handleAddMqttTrigger} style={{ marginTop: '16px' }}>
              + Add Trigger
            </button>
          </div>
        </div>
      )}

      <div class="btn-group">
        <button class="btn btn-primary" onClick={handleSave}>Save Settings</button>
        <button class="btn btn-secondary" onClick={onReload}>Reload</button>
      </div>

      {showGpioMenu && (
        <>
          <div
            style={{
              position: 'fixed', top: 0, left: 0, right: 0, bottom: 0,
              background: 'rgba(0,0,0,0.6)', zIndex: 999
            }}
            onClick={() => setShowGpioMenu(false)}
          />
          <div style={{
            position: 'fixed', top: '50%', left: '50%', transform: 'translate(-50%, -50%)',
            background: 'var(--bg-secondary)', border: '1px solid var(--border-color)',
            borderRadius: 'var(--radius)', boxShadow: 'var(--shadow)',
            zIndex: 1000, width: '90%', maxWidth: '600px', maxHeight: '85vh', overflow: 'hidden',
            display: 'flex', flexDirection: 'column'
          }}>
            <div style={{
              display: 'flex', justifyContent: 'space-between', alignItems: 'center',
              padding: '20px 24px', borderBottom: '1px solid var(--border-color)'
            }}>
              <h3 style={{ margin: 0, fontSize: '18px' }}>Add GPIO Pin</h3>
              <button
                style={{
                  background: 'none', border: 'none', fontSize: '24px',
                  cursor: 'pointer', color: 'var(--text-secondary)', padding: '4px 8px'
                }}
                onClick={() => setShowGpioMenu(false)}
              >&times;</button>
            </div>
            <div style={{ display: 'flex', flex: 1, overflow: 'hidden' }}>
              <div style={{
                width: '180px', borderRight: '1px solid var(--border-color)',
                padding: '16px', background: 'var(--bg-tertiary)', flexShrink: 0
              }}>
                <div style={{
                  fontSize: '11px', textTransform: 'uppercase', color: 'var(--text-muted)',
                  marginBottom: '12px', letterSpacing: '0.5px'
                }}>Select Board</div>
                {Object.entries(gpioPresets).map(([key, board]) => (
                  <div
                    key={key}
                    style={{
                      display: 'flex', alignItems: 'center', padding: '10px 12px', marginBottom: '6px',
                      borderRadius: 'var(--radius)', cursor: 'pointer',
                      background: key === selectedBoard ? 'rgba(47, 129, 247, 0.15)' : 'transparent',
                      border: key === selectedBoard ? '1px solid var(--accent-blue)' : '1px solid transparent'
                    }}
                    onClick={() => setSelectedBoard(key)}
                  >
                    <input type="radio" checked={key === selectedBoard} style={{ marginRight: '10px' }} onChange={() => {}} />
                    <label style={{ cursor: 'pointer', fontSize: '14px' }}>{board.name}</label>
                  </div>
                ))}
              </div>
              <div style={{ flex: 1, padding: '20px', overflowY: 'auto' }}>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(160px, 1fr))', gap: '12px' }}>
                  {gpioPresets[selectedBoard].pins.map(pin => {
                    const isUsed = gpios.some(g => g.pin === pin.pin)
                    return (
                      <div
                        key={pin.pin}
                        style={{
                          background: 'var(--bg-tertiary)', border: '1px solid var(--border-color)',
                          borderRadius: 'var(--radius)', padding: '16px', cursor: isUsed ? 'not-allowed' : 'pointer',
                          opacity: isUsed ? 0.5 : 1, transition: 'all 0.15s'
                        }}
                        onClick={() => !isUsed && handleAddGpioFromPreset(pin)}
                      >
                        <div style={{ fontSize: '20px', fontWeight: 700, color: 'var(--accent-blue)', marginBottom: '4px' }}>
                          GPIO {pin.pin}
                        </div>
                        <div style={{ fontSize: '14px', fontWeight: 500, color: 'var(--text-primary)', marginBottom: '4px' }}>
                          {pin.label}
                        </div>
                        <span style={{
                          display: 'inline-block', fontSize: '10px', textTransform: 'uppercase',
                          padding: '2px 6px', borderRadius: '3px', marginTop: '8px',
                          background: 'rgba(47, 129, 247, 0.15)', color: 'var(--accent-blue)'
                        }}>
                          {pin.action === 'ring_button' ? 'Ring' : pin.action === 'door_contact' ? 'Door' : 'Generic'}
                        </span>
                      </div>
                    )
                  })}
                </div>
                <div style={{ marginTop: '20px', paddingTop: '20px', borderTop: '1px solid var(--border-color)' }}>
                  <button
                    style={{
                      display: 'flex', alignItems: 'center', gap: '12px', width: '100%', padding: '16px',
                      background: 'var(--bg-tertiary)', border: '2px dashed var(--border-color)',
                      borderRadius: 'var(--radius)', cursor: 'pointer', transition: 'all 0.15s'
                    }}
                    onClick={handleAddCustomGpio}
                  >
                    <span style={{ fontSize: '24px', color: 'var(--accent-blue)' }}>+</span>
                    <div style={{ textAlign: 'left' }}>
                      <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>Custom GPIO</div>
                      <div style={{ fontSize: '13px', color: 'var(--text-muted)' }}>Add a blank GPIO with manual configuration</div>
                    </div>
                  </button>
                </div>
              </div>
            </div>
          </div>
        </>
      )}

      {showWiringDiagram && (
        <>
          <div
            style={{
              position: 'fixed', top: 0, left: 0, right: 0, bottom: 0,
              background: 'rgba(0,0,0,0.6)', zIndex: 999
            }}
            onClick={() => setShowWiringDiagram(null)}
          />
          <div style={{
            position: 'fixed', top: '50%', left: '50%', transform: 'translate(-50%, -50%)',
            background: 'var(--bg-secondary)', border: '1px solid var(--border-color)',
            borderRadius: 'var(--radius)', boxShadow: 'var(--shadow)',
            zIndex: 1000, width: '90%', maxWidth: '500px', maxHeight: '85vh', overflow: 'hidden'
          }}>
            <div style={{
              display: 'flex', justifyContent: 'space-between', alignItems: 'center',
              padding: '16px 20px', borderBottom: '1px solid var(--border-color)'
            }}>
              <h3 style={{ margin: 0, fontSize: '16px' }}>
                Wiring Diagram: {showWiringDiagram === 'up' ? 'Pull-Up (Active LOW)' : 'Pull-Down (Active HIGH)'}
              </h3>
              <button
                style={{
                  background: 'none', border: 'none', fontSize: '24px',
                  cursor: 'pointer', color: 'var(--text-secondary)', padding: '4px 8px'
                }}
                onClick={() => setShowWiringDiagram(null)}
              >&times;</button>
            </div>
            <div style={{ padding: '20px' }}>
              <pre style={{
                background: 'var(--bg-tertiary)',
                padding: '16px',
                borderRadius: 'var(--radius)',
                fontSize: '12px',
                lineHeight: '1.4',
                overflow: 'auto',
                fontFamily: "'SF Mono', Monaco, 'Cascadia Code', monospace",
                whiteSpace: 'pre',
                margin: 0
              }}>
                {wiringDiagrams[showWiringDiagram]}
              </pre>
            </div>
            <div style={{ padding: '0 20px 20px', display: 'flex', justifyContent: 'center', gap: '12px' }}>
              <button
                class={`btn ${showWiringDiagram === 'up' ? 'btn-primary' : 'btn-secondary'}`}
                onClick={() => setShowWiringDiagram('up')}
              >Pull-Up</button>
              <button
                class={`btn ${showWiringDiagram === 'down' ? 'btn-primary' : 'btn-secondary'}`}
                onClick={() => setShowWiringDiagram('down')}
              >Pull-Down</button>
            </div>
          </div>
        </>
      )}
    </section>
  )
}
