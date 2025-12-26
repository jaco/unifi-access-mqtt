import { useEffect, useRef, useState, useCallback } from 'preact/hooks'
import type { Status } from '../types'

interface LogEntry {
  timestamp: string
  message: string
}

export function useWebSocket() {
  const [connected, setConnected] = useState(false)
  const [status, setStatus] = useState<Status>({})
  const [ringing, setRinging] = useState(false)
  const [logs, setLogs] = useState<LogEntry[]>([])
  const wsRef = useRef<WebSocket | null>(null)
  const reconnectTimerRef = useRef<number>()

  const connect = useCallback(() => {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'
    const ws = new WebSocket(`${protocol}//${location.host}/ws`)

    ws.onopen = () => {
      setConnected(true)
      if (reconnectTimerRef.current) {
        clearTimeout(reconnectTimerRef.current)
        reconnectTimerRef.current = undefined
      }
    }

    ws.onclose = () => {
      setConnected(false)
      reconnectTimerRef.current = window.setTimeout(connect, 3000)
    }

    ws.onerror = () => ws.close()

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data)
        if (data.type === 'status') {
          setStatus(data)
          setRinging(data.doorbell?.active ?? false)
        } else if (data.type === 'doorbell') {
          setRinging(data.event === 'ring')
        } else if (data.type === 'log') {
          setLogs(prev => {
            const next = [...prev, { timestamp: data.timestamp, message: data.message }]
            return next.slice(-200) // Keep last 200 entries
          })
        }
      } catch {
        // Ignore parse errors
      }
    }

    wsRef.current = ws
  }, [])

  const disconnect = useCallback(() => {
    if (reconnectTimerRef.current) {
      clearTimeout(reconnectTimerRef.current)
    }
    wsRef.current?.close()
  }, [])

  const clearLogs = useCallback(() => {
    setLogs([])
  }, [])

  useEffect(() => {
    return () => {
      if (reconnectTimerRef.current) {
        clearTimeout(reconnectTimerRef.current)
      }
      wsRef.current?.close()
    }
  }, [])

  return { connected, status, ringing, logs, connect, disconnect, clearLogs }
}
