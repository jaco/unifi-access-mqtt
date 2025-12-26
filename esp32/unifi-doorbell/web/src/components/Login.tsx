import { useState } from 'preact/hooks'
import { DoorbellIcon } from './DoorbellIcon'
import api from '../api/api'

interface LoginProps {
  onSuccess: () => void
}

export function Login({ onSuccess }: LoginProps) {
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    setError('')
    setLoading(true)

    try {
      const result = await api.login(username, password)
      if (result.success) {
        onSuccess()
      } else {
        setError(result.message ?? 'Invalid credentials')
      }
    } catch (err) {
      setError('Connection failed: ' + (err as Error).message)
    } finally {
      setLoading(false)
    }
  }

  return (
    <div class="login-container">
      <div class="login-card">
        <div class="login-logo">
          <DoorbellIcon size={64} />
          <h1>UniFi Doorbell</h1>
        </div>
        <form onSubmit={handleSubmit}>
          <div class="form-group">
            <label for="login-username">Username</label>
            <input
              type="text"
              id="login-username"
              placeholder="admin"
              value={username}
              onInput={(e) => setUsername((e.target as HTMLInputElement).value)}
              required
            />
          </div>
          <div class="form-group">
            <label for="login-password">Password</label>
            <input
              type="password"
              id="login-password"
              placeholder="Password"
              value={password}
              onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
              required
            />
          </div>
          {error && <div class="alert alert-error">{error}</div>}
          <button type="submit" class="btn btn-primary" style={{ width: '100%' }} disabled={loading}>
            {loading ? <span class="spinner"></span> : 'Sign In'}
          </button>
        </form>
        <p style={{ textAlign: 'center', marginTop: '24px', color: 'var(--text-muted)', fontSize: '13px' }}>
          Default: admin / admin
        </p>
      </div>
    </div>
  )
}
