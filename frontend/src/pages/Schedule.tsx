import { useEffect, useState } from 'react'
import { apiGetSchedule, apiSetSchedule, apiDelSchedule, ScheduleEntry } from '../api/client'

const DAYS = ['Pon', 'Wt', 'Śr', 'Czw', 'Pt', 'Sob', 'Nie']

const CARD: React.CSSProperties = {
  background: '#fff', borderRadius: '8px', padding: '16px',
  boxShadow: '0 1px 4px rgba(0,0,0,.12)', marginBottom: '12px',
}

export default function Schedule() {
  const [entries, setEntries]   = useState<ScheduleEntry[]>([])
  const [editing, setEditing]   = useState<ScheduleEntry | null>(null)
  const [saving,  setSaving]    = useState(false)

  const load = () => apiGetSchedule().then(r => setEntries(r.data))
  useEffect(() => { load() }, [])

  const save = async () => {
    if (!editing) return
    setSaving(true)
    await apiSetSchedule(editing.id, editing)
    setSaving(false)
    setEditing(null)
    load()
  }

  const del = async (id: number) => {
    if (!confirm('Wyczyścić wpis?')) return
    await apiDelSchedule(id)
    load()
  }

  const toggleDay = (mask: number, bit: number) =>
    (mask & (1 << bit)) ? mask & ~(1 << bit) : mask | (1 << bit)

  const toggleSection = (mask: number, bit: number) =>
    (mask & (1 << bit)) ? mask & ~(1 << bit) : mask | (1 << bit)

  return (
    <div>
      <h2 style={{ margin: '0 0 16px', color: '#1f5e1f' }}>Harmonogram</h2>
      <p style={{ color: '#666', marginBottom: '16px', fontSize: '13px' }}>
        Kliknij wpis aby edytować. Maska sekcji: bit0=Sekcja1...bit7=Sekcja8.
      </p>

      {entries.map(e => (
        <div key={e.id} style={{ ...CARD, borderLeft: `4px solid ${e.enabled ? '#2a7c2a' : '#ccc'}` }}>
          {editing?.id === e.id ? (
            <EntryForm
              entry={editing}
              onChange={setEditing}
              onSave={save}
              onCancel={() => setEditing(null)}
              saving={saving}
            />
          ) : (
            <div style={{ display: 'flex', gap: '12px', alignItems: 'center', flexWrap: 'wrap' }}>
              <span style={{ fontWeight: 'bold', minWidth: '24px', color: '#888' }}>#{e.id}</span>
              <span style={{ color: e.enabled ? '#2a7c2a' : '#aaa' }}>
                {e.enabled ? '✓' : '✗'}
              </span>
              <span style={{ fontWeight: 'bold' }}>
                {String(e.hour).padStart(2,'0')}:{String(e.minute).padStart(2,'0')}
              </span>
              <span style={{ fontSize: '12px' }}>
                {DAYS.filter((_,i) => e.days_mask & (1<<i)).join(', ') || '—'}
              </span>
              <span style={{ fontSize: '12px', color: '#555' }}>
                {formatDur(e.duration_sec)} | S:{e.section_mask.toString(2).padStart(8,'0')}
              </span>
              <div style={{ marginLeft: 'auto', display: 'flex', gap: '8px' }}>
                <button onClick={() => setEditing({...e})}
                        style={{ padding: '4px 12px', cursor: 'pointer', borderRadius: '4px',
                                 background: '#e8f4e8', border: '1px solid #2a7c2a', color: '#1f5e1f' }}>
                  Edytuj
                </button>
                <button onClick={() => del(e.id)}
                        style={{ padding: '4px 12px', cursor: 'pointer', borderRadius: '4px',
                                 background: '#fde', border: '1px solid #c00', color: '#c00' }}>
                  Usuń
                </button>
              </div>
            </div>
          )}
        </div>
      ))}
    </div>
  )
}

function EntryForm({
  entry, onChange, onSave, onCancel, saving
}: {
  entry: ScheduleEntry
  onChange: (e: ScheduleEntry) => void
  onSave: () => void
  onCancel: () => void
  saving: boolean
}) {
  const set = (patch: Partial<ScheduleEntry>) => onChange({ ...entry, ...patch })

  return (
    <div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(160px,1fr))', gap: '12px', marginBottom: '12px' }}>
        <label style={LBL}>
          Aktywny
          <input type="checkbox" checked={entry.enabled}
                 onChange={e => set({ enabled: e.target.checked })} />
        </label>
        <label style={LBL}>
          Godzina
          <input type="number" min={0} max={23} value={entry.hour}
                 onChange={e => set({ hour: +e.target.value })} style={INP} />
        </label>
        <label style={LBL}>
          Minuta
          <input type="number" min={0} max={59} value={entry.minute}
                 onChange={e => set({ minute: +e.target.value })} style={INP} />
        </label>
        <label style={LBL}>
          Czas [s]
          <input type="number" min={0} max={14400} value={entry.duration_sec}
                 onChange={e => set({ duration_sec: +e.target.value })} style={INP} />
        </label>
      </div>

      <div style={{ marginBottom: '12px' }}>
        <div style={{ fontSize: '12px', color: '#555', marginBottom: '4px' }}>Dni tygodnia:</div>
        <div style={{ display: 'flex', gap: '6px', flexWrap: 'wrap' }}>
          {['Pon','Wt','Śr','Czw','Pt','Sob','Nie'].map((d, i) => (
            <label key={i} style={{ display: 'flex', alignItems: 'center', gap: '3px', fontSize: '13px' }}>
              <input type="checkbox" checked={!!(entry.days_mask & (1<<i))}
                     onChange={() => set({ days_mask: entry.days_mask ^ (1<<i) })} />
              {d}
            </label>
          ))}
        </div>
      </div>

      <div style={{ marginBottom: '12px' }}>
        <div style={{ fontSize: '12px', color: '#555', marginBottom: '4px' }}>Sekcje:</div>
        <div style={{ display: 'flex', gap: '6px', flexWrap: 'wrap' }}>
          {[1,2,3,4,5,6,7,8].map(s => (
            <label key={s} style={{ display: 'flex', alignItems: 'center', gap: '3px', fontSize: '13px' }}>
              <input type="checkbox" checked={!!(entry.section_mask & (1<<(s-1)))}
                     onChange={() => set({ section_mask: entry.section_mask ^ (1<<(s-1)) })} />
              S{s}
            </label>
          ))}
        </div>
      </div>

      <div style={{ display: 'flex', gap: '8px' }}>
        <button onClick={onSave} disabled={saving}
                style={{ padding: '8px 20px', background: '#2a7c2a', color: '#fff',
                         border: 'none', borderRadius: '4px', cursor: 'pointer' }}>
          {saving ? 'Zapisywanie...' : 'Zapisz'}
        </button>
        <button onClick={onCancel}
                style={{ padding: '8px 16px', background: '#eee', border: 'none',
                         borderRadius: '4px', cursor: 'pointer' }}>
          Anuluj
        </button>
      </div>
    </div>
  )
}

const LBL: React.CSSProperties = {
  display: 'flex', flexDirection: 'column', gap: '4px', fontSize: '12px', color: '#555'
}
const INP: React.CSSProperties = {
  padding: '6px', border: '1px solid #ccc', borderRadius: '4px', width: '100%'
}

function formatDur(s: number): string {
  if (s === 0) return '∞'
  if (s < 60)  return `${s}s`
  if (s < 3600) return `${Math.floor(s/60)}min`
  return `${Math.floor(s/3600)}h${Math.floor((s%3600)/60)}m`
}
