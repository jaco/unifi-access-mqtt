export function DoorbellIcon({ size = 32 }: { size?: number }) {
  return (
    <svg viewBox="0 0 24 24" width={size} height={size}>
      <path d="M12 3L4 9v12h16V9l-8-6zm0 2.5L18 10v9H6v-9l6-4.5zM12 13a2 2 0 100 4 2 2 0 000-4z"/>
    </svg>
  )
}

export function CheckIcon({ size = 64 }: { size?: number }) {
  return (
    <svg viewBox="0 0 24 24" width={size} height={size} style={{ fill: 'var(--accent-green)' }}>
      <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>
    </svg>
  )
}
