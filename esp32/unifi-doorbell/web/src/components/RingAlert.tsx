import api from '../api/api'

interface RingAlertProps {
  active: boolean
}

export function RingAlert({ active }: RingAlertProps) {
  const handleDismiss = async () => {
    const result = await api.dismissCall()
    if (!result.success) {
      alert('Failed to dismiss: ' + (result.message ?? 'Unknown error'))
    }
  }

  return (
    <div class={`ring-alert ${active ? 'active' : ''}`}>
      <strong>Doorbell is ringing!</strong>
      <button class="btn btn-secondary" onClick={handleDismiss}>Dismiss</button>
    </div>
  )
}
