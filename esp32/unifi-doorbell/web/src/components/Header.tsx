import { DoorbellIcon } from './DoorbellIcon'

interface HeaderProps {
  connected: boolean
  onLogout: () => void
}

export function Header({ connected, onLogout }: HeaderProps) {
  return (
    <header class="app-header">
      <a href="/" class="app-title-link" onClick={(e) => { e.preventDefault(); location.reload() }}>
        <h1>
          <DoorbellIcon />
          UniFi Doorbell Fixes
        </h1>
      </a>
      <div class="header-actions">
        <div class="connection-badge">
          <span class={`dot ${connected ? '' : 'disconnected'}`}></span>
          <span>{connected ? 'Connected' : 'Disconnected'}</span>
        </div>
        <button class="btn btn-secondary" onClick={onLogout}>Logout</button>
      </div>
    </header>
  )
}
