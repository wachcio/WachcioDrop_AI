import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { ChevronDown, ChevronUp, Trash2, Save, X } from 'lucide-react'
import { apiGetSchedule, apiSetSchedule, apiDelSchedule, ScheduleEntry } from '../api/client'

const DAYS = ['Pon', 'Wt', 'Śr', 'Czw', 'Pt', 'Sob', 'Nie']
const SECTIONS = [1, 2, 3, 4, 5, 6, 7, 8]

function formatDur(s: number): string {
  if (s === 0)   return '∞'
  if (s < 60)    return `${s}s`
  if (s < 3600)  return `${Math.floor(s / 60)}min`
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return m ? `${h}h${m}m` : `${h}h`
}

// ─── Toggle switch ─────────────────────────────────────────────────────────────

function ToggleSwitch({ checked, onChange }: { checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <button
      onClick={e => { e.stopPropagation(); onChange(!checked) }}
      className={`relative inline-flex w-10 h-5 rounded-full transition-colors shrink-0
        ${checked ? 'bg-green-500' : 'bg-gray-300'}`}
    >
      <span className={`absolute top-0.5 left-0.5 w-4 h-4 bg-white rounded-full shadow
        transition-transform ${checked ? 'translate-x-5' : ''}`} />
    </button>
  )
}

// ─── Day pills ─────────────────────────────────────────────────────────────────

function DayPills({ mask }: { mask: number }) {
  return (
    <div className="flex gap-0.5 flex-wrap">
      {DAYS.map((d, i) => (
        <span
          key={i}
          className={`text-xs px-1 py-0.5 rounded font-medium
            ${mask & (1 << i) ? 'bg-green-100 text-green-700' : 'text-gray-300'}`}
        >
          {d}
        </span>
      ))}
    </div>
  )
}

// ─── EditForm ─────────────────────────────────────────────────────────────────

function EditForm({
  entry, onChange, onSave, onCancel, saving,
}: {
  entry: ScheduleEntry
  onChange: (e: ScheduleEntry) => void
  onSave: () => void
  onCancel: () => void
  saving: boolean
}) {
  const set = (patch: Partial<ScheduleEntry>) => onChange({ ...entry, ...patch })

  return (
    <div className="border-t border-gray-100 mt-3 pt-4 flex flex-col gap-4">

      {/* Czas */}
      <div className="flex items-center gap-4 flex-wrap">
        <div>
          <label className="text-xs text-gray-500 block mb-1">Godzina</label>
          <input type="number" min={0} max={23} value={entry.hour}
            onChange={e => set({ hour: +e.target.value })}
            className="w-16 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
              focus:outline-none focus:ring-2 focus:ring-green-400" />
        </div>
        <div>
          <label className="text-xs text-gray-500 block mb-1">Minuta</label>
          <input type="number" min={0} max={59} value={entry.minute}
            onChange={e => set({ minute: +e.target.value })}
            className="w-16 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
              focus:outline-none focus:ring-2 focus:ring-green-400" />
        </div>
        <div>
          <label className="text-xs text-gray-500 block mb-1">Czas [s]</label>
          <input type="number" min={0} max={14400} value={entry.duration_sec}
            onChange={e => set({ duration_sec: +e.target.value })}
            className="w-24 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
              focus:outline-none focus:ring-2 focus:ring-green-400" />
        </div>
      </div>

      {/* Dni */}
      <div>
        <label className="text-xs text-gray-500 block mb-2">Dni tygodnia</label>
        <div className="flex gap-1.5 flex-wrap">
          {DAYS.map((d, i) => {
            const on = !!(entry.days_mask & (1 << i))
            return (
              <button
                key={i}
                onClick={() => set({ days_mask: entry.days_mask ^ (1 << i) })}
                className={`px-3 py-1 rounded-full text-xs font-medium transition-colors
                  ${on ? 'bg-green-500 text-white' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
              >
                {d}
              </button>
            )
          })}
        </div>
      </div>

      {/* Sekcje */}
      <div>
        <label className="text-xs text-gray-500 block mb-2">Sekcje</label>
        <div className="flex gap-1.5 flex-wrap">
          {SECTIONS.map(s => {
            const on = !!(entry.section_mask & (1 << (s - 1)))
            return (
              <button
                key={s}
                onClick={() => set({ section_mask: entry.section_mask ^ (1 << (s - 1)) })}
                className={`w-10 h-10 rounded-xl text-sm font-bold transition-colors
                  ${on ? 'bg-green-500 text-white shadow-sm' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
              >
                {s}
              </button>
            )
          })}
        </div>
      </div>

      {/* Przyciski */}
      <div className="flex gap-2">
        <button
          onClick={onSave}
          disabled={saving}
          className="flex items-center gap-1.5 bg-green-600 hover:bg-green-700 text-white
            px-4 py-2 rounded-xl text-sm font-medium transition-colors disabled:opacity-50"
        >
          <Save size={15} />
          {saving ? 'Zapisywanie…' : 'Zapisz'}
        </button>
        <button
          onClick={onCancel}
          className="flex items-center gap-1.5 bg-gray-100 hover:bg-gray-200 text-gray-600
            px-4 py-2 rounded-xl text-sm font-medium transition-colors"
        >
          <X size={15} />
          Anuluj
        </button>
      </div>
    </div>
  )
}

// ─── Schedule ─────────────────────────────────────────────────────────────────

export default function Schedule() {
  const [entries, setEntries] = useState<ScheduleEntry[]>([])
  const [editing, setEditing] = useState<ScheduleEntry | null>(null)
  const [expanded, setExpanded] = useState<number | null>(null)
  const [saving, setSaving]   = useState(false)

  const load = () => apiGetSchedule().then(r => setEntries(r.data))
  useEffect(() => { load() }, [])

  const toggleEnabled = async (e: ScheduleEntry) => {
    try {
      await apiSetSchedule(e.id, { ...e, enabled: !e.enabled })
      load()
    } catch {
      toast.error('Błąd zapisu')
    }
  }

  const expandRow = (id: number, entry: ScheduleEntry) => {
    if (expanded === id) {
      setExpanded(null)
      setEditing(null)
    } else {
      setExpanded(id)
      setEditing({ ...entry })
    }
  }

  const save = async () => {
    if (!editing) return
    setSaving(true)
    try {
      await apiSetSchedule(editing.id, editing)
      toast.success('Harmonogram zapisany')
      setExpanded(null)
      setEditing(null)
      load()
    } catch {
      toast.error('Błąd zapisu')
    }
    setSaving(false)
  }

  const del = async (id: number) => {
    if (!confirm('Wyczyścić ten wpis?')) return
    try {
      await apiDelSchedule(id)
      toast.success('Wpis usunięty')
      load()
    } catch {
      toast.error('Błąd usuwania')
    }
  }

  return (
    <div className="flex flex-col gap-3">
      <h2 className="text-sm font-semibold text-gray-700 uppercase tracking-wide mb-1">
        Harmonogram
      </h2>

      {entries.map(e => {
        const isOpen = expanded === e.id
        const activeSections = SECTIONS.filter(s => e.section_mask & (1 << (s - 1)))

        return (
          <div
            key={e.id}
            className={`bg-white rounded-2xl shadow-sm border transition-colors
              ${isOpen ? 'border-green-300' : 'border-gray-100'}`}
          >
            {/* Row */}
            <div
              className="flex items-center gap-3 p-3 cursor-pointer select-none"
              onClick={() => expandRow(e.id, e)}
            >
              {/* Toggle */}
              <ToggleSwitch checked={e.enabled} onChange={() => toggleEnabled(e)} />

              {/* ID */}
              <span className="text-xs text-gray-400 w-5 shrink-0">#{e.id}</span>

              {/* Time */}
              <span className={`font-mono font-bold text-sm w-12 shrink-0
                ${e.enabled ? 'text-gray-800' : 'text-gray-400'}`}>
                {String(e.hour).padStart(2, '0')}:{String(e.minute).padStart(2, '0')}
              </span>

              {/* Days */}
              <div className="flex-1 min-w-0">
                <DayPills mask={e.days_mask} />
              </div>

              {/* Duration */}
              <span className="text-xs text-gray-500 shrink-0 hidden sm:block">
                {formatDur(e.duration_sec)}
              </span>

              {/* Active sections */}
              <div className="hidden sm:flex gap-1">
                {activeSections.map(s => (
                  <span key={s}
                    className="w-5 h-5 bg-green-100 text-green-700 rounded text-xs
                      font-bold flex items-center justify-center">
                    {s}
                  </span>
                ))}
                {activeSections.length === 0 && (
                  <span className="text-xs text-gray-300">brak</span>
                )}
              </div>

              {/* Delete */}
              <button
                onClick={ev => { ev.stopPropagation(); del(e.id) }}
                className="text-gray-300 hover:text-red-400 transition-colors shrink-0"
              >
                <Trash2 size={16} />
              </button>

              {/* Expand arrow */}
              <span className="text-gray-400 shrink-0">
                {isOpen ? <ChevronUp size={16} /> : <ChevronDown size={16} />}
              </span>
            </div>

            {/* Edit form */}
            {isOpen && editing && (
              <div className="px-4 pb-4">
                <EditForm
                  entry={editing}
                  onChange={setEditing}
                  onSave={save}
                  onCancel={() => { setExpanded(null); setEditing(null) }}
                  saving={saving}
                />
              </div>
            )}
          </div>
        )
      })}
    </div>
  )
}
