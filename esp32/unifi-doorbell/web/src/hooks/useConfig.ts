import { useState, useCallback } from 'preact/hooks'
import type { Config, GpioConfig, MqttTrigger } from '../types'
import api from '../api/api'

export function useConfig() {
  const [config, setConfig] = useState<Config>({})
  const [gpios, setGpios] = useState<GpioConfig[]>([])
  const [mqttTriggers, setMqttTriggers] = useState<MqttTrigger[]>([])
  const [loading, setLoading] = useState(false)

  const loadConfig = useCallback(async () => {
    setLoading(true)
    try {
      const data = await api.getConfig()
      setConfig(data)
      setGpios(data.gpios ?? [])
      setMqttTriggers(data.mqttTriggers ?? [])
    } finally {
      setLoading(false)
    }
  }, [])

  const saveSettings = useCallback(async (
    network: { wifiSsid: string; wifiPassword: string },
    mqtt: Config['mqtt'],
    web: { username: string; password: string }
  ) => {
    const update: Partial<Config> = {
      network,
      mqtt,
      web,
      gpios,
      mqttTriggers
    }
    return api.saveConfig(update)
  }, [gpios, mqttTriggers])

  const addGpio = useCallback((gpio: GpioConfig) => {
    setGpios(prev => [...prev, gpio])
  }, [])

  const removeGpio = useCallback((index: number) => {
    setGpios(prev => prev.filter((_, i) => i !== index))
  }, [])

  const updateGpio = useCallback((index: number, field: keyof GpioConfig, value: unknown) => {
    setGpios(prev => prev.map((g, i) => i === index ? { ...g, [field]: value } : g))
  }, [])

  const addMqttTrigger = useCallback((trigger: MqttTrigger) => {
    setMqttTriggers(prev => [...prev, trigger])
  }, [])

  const removeMqttTrigger = useCallback((index: number) => {
    setMqttTriggers(prev => prev.filter((_, i) => i !== index))
  }, [])

  const updateMqttTrigger = useCallback((index: number, field: keyof MqttTrigger, value: unknown) => {
    setMqttTriggers(prev => prev.map((t, i) => i === index ? { ...t, [field]: value } : t))
  }, [])

  return {
    config,
    gpios,
    mqttTriggers,
    loading,
    loadConfig,
    saveSettings,
    addGpio,
    removeGpio,
    updateGpio,
    addMqttTrigger,
    removeMqttTrigger,
    updateMqttTrigger
  }
}
