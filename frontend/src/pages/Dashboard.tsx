import { useEffect, useState } from 'react'
import {
  apiGetStatus, apiGetSections, apiSectionOn, apiSectionOff, apiAllOff,
  SystemStatus, SectionsResponse
} from '../api/client'

const CARD: React.CSSProperties = {
  background: '#fff', borderRadius: '8px', padding: '16px',
  boxShadow: '0 1px 4px rgba(0,0,0,.12)', marginBottom: '16px',
}
const BTN = (active: boolean): React.CSSProperties => ({
  padding: '8px 16px', border: 'none', borderRadius: '6px', cursor: 'pointer',
  background: active ? '#c0392b' : '#2a7c2a', color: '#fff', fontWeight: 'bold',
  fontSize: '13px', transition: 'background .2s',
})

export default function Dashboard() {
  const [status,   setStatus]   = useState<SystemStatus | null>(null)
  const [sections, setSections] = useState<SectionsResponse | null>(null)
  const [duration, setDuration] = useState(300)
  const [error,    setError]    = useState('')

  const load = async () => {
    try {
      const [s, sec] = await Promise.all([apiGetStatus(), apiGetSections()])
      setStatus(s.data)
      setSections(sec.data)
      setError('')
    } catch {
      setError('Błąd połączenia z ESP32')
    }
  }

  useEffect(() => {
    load()
    const t = setInterval(load, 3000)
    return () => clearInterval(t)
  }, [])

  const toggleSection = async (id: number, active: boolean) => {
    if (active) {
      await apiSectionOff(id)
    } else {
      await apiSectionOn(id, duration)
    }
    load()
  }

  return (
    <div>
      <h2 style={{ margin: '0 0 16px', color: '#1f5e1f' }}>Dashboard</h2>

      {error && (
        <div style={{ background: '#fde', padding: '10px', borderRadius: '6px',
                      color: '#c00', marginBottom: '12px' }}>{error}</div>
      )}

      {status && (
        <div style={CARD}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(160px,1fr))', gap: '8px' }}>
            <Stat label="Czas"         value={status.time.replace('T', ' ')} />
            <Stat label="IP"           value={status.ip} />
            <Stat label="RSSI"         value={`${status.rssi} dBm`} />
            <Stat label="Uptime"       value={formatUptime(status.uptime_sec)} />
            <Stat label="Nawadnianie"  value={status.irrigation_today ? '✓ Aktywne' : '✗ Zablokowane'}
                  color={status.irrigation_today ? '#2a7c2a' : '#c0392b'} />
            <Stat label="Sekcje ON"    value={`${status.sections_active}`} />
          </div>
        </div>
      )}

      <div style={CARD}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px', marginBottom: '12px' }}>
          <h3 style={{ margin: 0, color: '#333' }}>Czas trwania:</h3>
          <select value={duration} onChange={e => setDuration(+e.target.value)}
                  style={{ padding: '6px', borderRadius: '4px', border: '1px solid #ccc' }}>
            {[60,120,300,600,900,1800,3600].map(s => (
              <option key={s} value={s}>{formatSec(s)}</option>
            ))}
          </select>
          <button onClick={() => apiAllOff().then(load)}
                  style={{ ...BTN(true), marginLeft: 'auto' }}>
            Wyłącz wszystko
          </button>
        </div>

        {sections && (
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(130px,1fr))', gap: '8px' }}>
            {sections.sections.map(sec => (
              <button key={sec.id}
                      onClick={() => toggleSection(sec.id, sec.active)}
                      style={BTN(sec.active)}>
                Sekcja {sec.id}<br/>
                <span style={{ fontSize: '11px' }}>{sec.active ? 'ON' : 'OFF'}</span>
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div style={{ padding: '8px', background: '#f8faf8', borderRadius: '6px' }}>
      <div style={{ fontSize: '11px', color: '#888', marginBottom: '2px' }}>{label}</div>
      <div style={{ fontWeight: 'bold', color: color ?? '#1a1a1a' }}>{value}</div>
    </div>
  )
}

function formatUptime(sec: number): string {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return `${h}h ${m}m`
}

function formatSec(s: number): string {
  if (s < 60)  return `${s}s`
  if (s < 3600) return `${s/60}min`
  return `${s/3600}h`
}
