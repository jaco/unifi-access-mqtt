import type { Tab } from '../types'

interface NavigationProps {
  activeTab: Tab
  onTabChange: (tab: Tab) => void
}

const tabs: { id: Tab; label: string }[] = [
  { id: 'status', label: 'Status' },
  { id: 'setup', label: 'Setup' },
  { id: 'settings', label: 'Settings' },
  { id: 'system', label: 'System' }
]

export function Navigation({ activeTab, onTabChange }: NavigationProps) {
  return (
    <nav class="app-nav">
      {tabs.map(tab => (
        <a
          key={tab.id}
          class={`nav-tab ${activeTab === tab.id ? 'active' : ''}`}
          onClick={() => onTabChange(tab.id)}
        >
          {tab.label}
        </a>
      ))}
    </nav>
  )
}
