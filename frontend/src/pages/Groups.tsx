import { useEffect, useState } from 'react'
import { apiGetGroups, apiSetGroup, apiActivateGroup, Group } from '../api/client'

const CARD: React.CSSProperties = {
  background: '#fff', borderRadius: '8px', padding: '16px',
  boxShadow: '0 1px 4px rgba(0,0,0,.12)', marginBottom: '12px',
}

export default function Groups() {
  const [groups,   setGroups]  = useState<Group[]>([])
  const [editing,  setEditing] = useState<Group | null>(null)
  const [duration, setDur]     = useState(300)
  const [saving,   setSaving]  = useState(false)

  const load = () => apiGetGroups().then(r => setGroups(r.data))
  useEffect(() => { load() }, [])

  const save = async () => {
    if (!editing) return
    setSaving(true)
    await apiSetGroup(editing.id, editing)
    setSaving(false)
    setEditing(null)
    load()
  }

  const activate = async (id: number) => {
    await apiActivateGroup(id, duration)
  }

  return (
    <div>
      <h2 style={{ margin: '0 0 8px', color: '#1f5e1f' }}>Grupy sekcji</h2>
      <div style={{ display: 'flex', alignItems: 'center', gap: '12px', marginBottom: '16px' }}>
        <span style={{ fontSize: '13px', color: '#555' }}>Czas aktywacji:</span>
        <select value={duration} onChange={e => setDur(+e.target.value)}
                style={{ padding: '5px', borderRadius: '4px', border: '1px solid #ccc' }}>
          {[60,120,300,600,900,1800,3600].map(s => (
            <option key={s} value={s}>{formatSec(s)}</option>
          ))}
        </select>
      </div>

      {groups.map(g => (
        <div key={g.id} style={CARD}>
          {editing?.id === g.id ? (
            <GroupForm group={editing} onChange={setEditing}
                       onSave={save} onCancel={() => setEditing(null)} saving={saving} />
          ) : (
            <div style={{ display: 'flex', gap: '12px', alignItems: 'center', flexWrap: 'wrap' }}>
              <span style={{ fontWeight: 'bold', minWidth: '24px', color: '#888' }}>#{g.id}</span>
              <span style={{ fontWeight: 'bold' }}>{g.name}</span>
              <span style={{ fontSize: '12px', color: '#555' }}>
                Sekcje: {[1,2,3,4,5,6,7,8]
                  .filter(i => g.section_mask & (1<<(i-1)))
                  .map(i => `S${i}`)
                  .join(', ') || '—'}
              </span>
              <div style={{ marginLeft: 'auto', display: 'flex', gap: '8px' }}>
                <button onClick={() => activate(g.id)}
                        disabled={g.section_mask === 0}
                        style={{ padding: '4px 12px', cursor: 'pointer', borderRadius: '4px',
                                 background: '#2a7c2a', border: 'none', color: '#fff',
                                 opacity: g.section_mask ? 1 : 0.4 }}>
                  Uruchom
                </button>
                <button onClick={() => setEditing({...g})}
                        style={{ padding: '4px 12px', cursor: 'pointer', borderRadius: '4px',
                                 background: '#e8f4e8', border: '1px solid #2a7c2a', color: '#1f5e1f' }}>
                  Edytuj
                </button>
              </div>
            </div>
          )}
        </div>
      ))}
    </div>
  )
}

function GroupForm({
  group, onChange, onSave, onCancel, saving
}: {
  group: Group
  onChange: (g: Group) => void
  onSave: () => void
  onCancel: () => void
  saving: boolean
}) {
  const set = (patch: Partial<Group>) => onChange({ ...group, ...patch })

  return (
    <div>
      <label style={{ display: 'block', marginBottom: '8px', fontSize: '12px', color: '#555' }}>
        Nazwa grupy
        <input value={group.name}
               onChange={e => set({ name: e.target.value })}
               style={{ display: 'block', padding: '6px', border: '1px solid #ccc',
                        borderRadius: '4px', width: '200px', marginTop: '4px' }} />
      </label>

      <div style={{ marginBottom: '12px' }}>
        <div style={{ fontSize: '12px', color: '#555', marginBottom: '4px' }}>Sekcje w grupie:</div>
        <div style={{ display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
          {[1,2,3,4,5,6,7,8].map(s => (
            <label key={s} style={{ display: 'flex', alignItems: 'center', gap: '4px',
                                     fontSize: '14px', cursor: 'pointer' }}>
              <input type="checkbox" checked={!!(group.section_mask & (1<<(s-1)))}
                     onChange={() => set({ section_mask: group.section_mask ^ (1<<(s-1)) })} />
              Sekcja {s}
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

function formatSec(s: number): string {
  if (s < 60)   return `${s}s`
  if (s < 3600) return `${s/60}min`
  return `${s/3600}h`
}
