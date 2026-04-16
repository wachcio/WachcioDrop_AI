import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { ChevronDown, ChevronUp, Trash2, Save, X } from 'lucide-react'
import { apiGetSchedule, apiSetSchedule, apiDelSchedule, apiGetGroups, ScheduleEntry, Group } from '../api/client'
import { getScheduleMode } from './Settings'
import { Tooltip } from '../components/Tooltip'

const DAYS = ['Pon', 'Wt', 'Śr', 'Czw', 'Pt', 'Sob', 'Nie']
const DAYS_FULL = ['Poniedziałek', 'Wtorek', 'Środa', 'Czwartek', 'Piątek', 'Sobota', 'Niedziela']
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

function ToggleSwitch({
  checked, onChange, tip,
}: {
  checked: boolean
  onChange: (v: boolean) => void
  tip?: string
}) {
  const btn = (
    <button
      onClick={e => { e.stopPropagation(); onChange(!checked) }}
      className={`relative inline-flex w-10 h-5 rounded-full transition-colors shrink-0
        ${checked ? 'bg-green-500' : 'bg-gray-300'}`}
    >
      <span className={`absolute top-0.5 left-0.5 w-4 h-4 bg-white rounded-full shadow
        transition-transform ${checked ? 'translate-x-5' : ''}`} />
    </button>
  )
  if (!tip) return btn
  return <Tooltip text={tip}>{btn}</Tooltip>
}

// ─── Day pills ─────────────────────────────────────────────────────────────────

function DayPills({ mask }: { mask: number }) {
  return (
    <div className="flex gap-0.5 flex-wrap">
      {DAYS.map((d, i) => (
        <Tooltip key={i} text={DAYS_FULL[i]}>
          <span
            className={`text-xs px-1 py-0.5 rounded font-medium cursor-default
              ${mask & (1 << i) ? 'bg-green-100 text-green-700' : 'text-gray-300'}`}
          >
            {d}
          </span>
        </Tooltip>
      ))}
    </div>
  )
}

// ─── EditForm ─────────────────────────────────────────────────────────────────

function EditForm({
  entry, onChange, onSave, onCancel, saving, groups, mode,
}: {
  entry: ScheduleEntry
  onChange: (e: ScheduleEntry) => void
  onSave: () => void
  onCancel: () => void
  saving: boolean
  groups: Group[]
  mode: 'sections' | 'groups'
}) {
  const set = (patch: Partial<ScheduleEntry>) => onChange({ ...entry, ...patch })

  return (
    <div className="border-t border-gray-100 mt-3 pt-4 flex flex-col gap-4">

      {/* Czas */}
      <div className="flex items-center gap-4 flex-wrap">
        <div>
          <label className="text-xs text-gray-500 block mb-1">Godzina</label>
          <Tooltip text="Godzina uruchomienia (0–23)" pos="bottom">
            <input type="number" min={0} max={23} value={entry.hour}
              onChange={e => set({ hour: +e.target.value })}
              className="w-16 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
                focus:outline-none focus:ring-2 focus:ring-green-400" />
          </Tooltip>
        </div>
        <div>
          <label className="text-xs text-gray-500 block mb-1">Minuta</label>
          <Tooltip text="Minuta uruchomienia (0–59)" pos="bottom">
            <input type="number" min={0} max={59} value={entry.minute}
              onChange={e => set({ minute: +e.target.value })}
              className="w-16 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
                focus:outline-none focus:ring-2 focus:ring-green-400" />
          </Tooltip>
        </div>
        <div>
          <label className="text-xs text-gray-500 block mb-1">Czas [min]</label>
          <Tooltip text="Czas trwania nawadniania w minutach (0 = bez limitu)" pos="bottom">
            <input type="number" min={0} max={240} value={Math.round(entry.duration_sec / 60)}
              onChange={e => set({ duration_sec: +e.target.value * 60 })}
              className="w-24 border border-gray-200 rounded-lg px-2 py-1.5 text-center text-sm
                focus:outline-none focus:ring-2 focus:ring-green-400" />
          </Tooltip>
        </div>
      </div>

      {/* Dni */}
      <div>
        <label className="text-xs text-gray-500 block mb-2">Dni tygodnia</label>
        <div className="flex gap-1.5 flex-wrap">
          {DAYS.map((d, i) => {
            const on = !!(entry.days_mask & (1 << i))
            return (
              <Tooltip key={i} text={on ? `Wyłącz ${DAYS_FULL[i]}` : `Włącz ${DAYS_FULL[i]}`}>
                <button
                  onClick={() => set({ days_mask: entry.days_mask ^ (1 << i) })}
                  className={`px-3 py-1 rounded-full text-xs font-medium transition-colors
                    ${on ? 'bg-green-500 text-white' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
                >
                  {d}
                </button>
              </Tooltip>
            )
          })}
        </div>
      </div>

      {/* Sekcje lub Grupy */}
      {mode === 'sections' ? (
        <div>
          <label className="text-xs text-gray-500 block mb-2">Sekcje</label>
          <div className="flex gap-1.5 flex-wrap">
            {SECTIONS.map(s => {
              const on = !!(entry.section_mask & (1 << (s - 1)))
              return (
                <Tooltip key={s} text={on ? `Usuń sekcję ${s}` : `Dodaj sekcję ${s}`}>
                  <button
                    onClick={() => set({ section_mask: entry.section_mask ^ (1 << (s - 1)) })}
                    className={`w-10 h-10 rounded-xl text-sm font-bold transition-colors
                      ${on ? 'bg-green-500 text-white shadow-sm' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
                  >
                    {s}
                  </button>
                </Tooltip>
              )
            })}
          </div>
        </div>
      ) : (
        <div>
          <label className="text-xs text-gray-500 block mb-2">Grupy</label>
          <div className="flex gap-1.5 flex-wrap">
            {groups.filter(g => g.section_mask !== 0).map(g => {
              const on = !!(entry.group_mask & (1 << (g.id - 1)))
              return (
                <Tooltip key={g.id} text={on ? `Odznacz grupę "${g.name}"` : `Wybierz grupę "${g.name}"`}>
                  <button
                    onClick={() => set({ group_mask: on ? 0 : (1 << (g.id - 1)) })}
                    className={`px-3 py-2 rounded-xl text-xs font-semibold transition-colors
                      ${on ? 'bg-blue-500 text-white shadow-sm' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
                  >
                    {g.name}
                  </button>
                </Tooltip>
              )
            })}
            {groups.filter(g => g.section_mask !== 0).length === 0 && (
              <p className="text-xs text-gray-400 italic">Brak skonfigurowanych grup</p>
            )}
          </div>
        </div>
      )}

      {/* Przyciski */}
      <div className="flex gap-2">
        <Tooltip text="Zapisz wpis harmonogramu">
          <button
            onClick={onSave}
            disabled={saving}
            className="flex items-center gap-1.5 bg-green-600 hover:bg-green-700 text-white
              px-4 py-2 rounded-xl text-sm font-medium transition-colors disabled:opacity-50"
          >
            <Save size={15} />
            {saving ? 'Zapisywanie…' : 'Zapisz'}
          </button>
        </Tooltip>
        <Tooltip text="Anuluj edycję bez zapisywania">
          <button
            onClick={onCancel}
            className="flex items-center gap-1.5 bg-gray-100 hover:bg-gray-200 text-gray-600
              px-4 py-2 rounded-xl text-sm font-medium transition-colors"
          >
            <X size={15} />
            Anuluj
          </button>
        </Tooltip>
      </div>
    </div>
  )
}

// ─── Schedule ─────────────────────────────────────────────────────────────────

export default function Schedule() {
  const [entries,  setEntries]  = useState<ScheduleEntry[]>([])
  const [groups,   setGroups]   = useState<Group[]>([])
  const [editing,  setEditing]  = useState<ScheduleEntry | null>(null)
  const [expanded, setExpanded] = useState<number | null>(null)
  const [saving,   setSaving]   = useState(false)
  const [mode,     setMode]     = useState(getScheduleMode())

  const load = () => apiGetSchedule().then(r => setEntries(r.data))

  useEffect(() => {
    load()
    apiGetGroups().then(r => setGroups(r.data)).catch(() => {})
    const t = setInterval(load, 5_000)
    return () => clearInterval(t)
  }, [])

  useEffect(() => {
    const onFocus = () => setMode(getScheduleMode())
    window.addEventListener('focus', onFocus)
    return () => window.removeEventListener('focus', onFocus)
  }, [])

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
    // Wyczyść nieużywany mask zależnie od trybu
    const payload: ScheduleEntry = mode === 'sections'
      ? { ...editing, group_mask: 0 }
      : { ...editing, section_mask: 0 }
    try {
      await apiSetSchedule(editing.id, payload)
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
        const activeGroups = groups.filter(g => e.group_mask & (1 << (g.id - 1)))

        return (
          <div
            key={e.id}
            className={`bg-white rounded-2xl shadow-sm border transition-colors
              ${isOpen ? 'border-green-300' : 'border-gray-100'}`}
          >
            {/* Row */}
            <div
              className="flex items-center gap-3 p-3 cursor-pointer select-none rounded-2xl transition-colors duration-200 hover:bg-green-50"
              onClick={() => expandRow(e.id, e)}
            >
              {/* Toggle */}
              <ToggleSwitch
                checked={e.enabled}
                onChange={() => toggleEnabled(e)}
                tip={e.enabled ? 'Wyłącz ten wpis harmonogramu' : 'Włącz ten wpis harmonogramu'}
              />

              {/* ID */}
              <Tooltip text={`Wpis harmonogramu #${e.id}`}>
                <span className="text-xs text-gray-400 w-5 shrink-0 cursor-default">#{e.id}</span>
              </Tooltip>

              {/* Time */}
              <Tooltip text={`Uruchomienie o ${String(e.hour).padStart(2,'0')}:${String(e.minute).padStart(2,'0')}`}>
                <span className={`font-mono font-bold text-sm w-12 shrink-0 cursor-default
                  ${e.enabled ? 'text-gray-800' : 'text-gray-400'}`}>
                  {String(e.hour).padStart(2, '0')}:{String(e.minute).padStart(2, '0')}
                </span>
              </Tooltip>

              {/* Days */}
              <div className="flex-1 min-w-0">
                <DayPills mask={e.days_mask} />
              </div>

              {/* Duration */}
              <Tooltip text={`Czas trwania: ${formatDur(e.duration_sec)}`}>
                <span className="text-xs text-gray-500 shrink-0 hidden sm:block cursor-default">
                  {formatDur(e.duration_sec)}
                </span>
              </Tooltip>

              {/* Active sections / groups */}
              <div className="hidden sm:flex gap-1 flex-wrap">
                {mode === 'sections' ? (
                  activeSections.length > 0
                    ? activeSections.map(s => (
                        <Tooltip key={s} text={`Sekcja ${s} aktywna w tym wpisie`}>
                          <span
                            className="w-5 h-5 bg-green-100 text-green-700 rounded text-xs
                              font-bold flex items-center justify-center cursor-default">
                            {s}
                          </span>
                        </Tooltip>
                      ))
                    : <span className="text-xs text-gray-300">brak</span>
                ) : (
                  activeGroups.length > 0
                    ? activeGroups.map(g => (
                        <Tooltip key={g.id} text={`Grupa "${g.name}" aktywna w tym wpisie`}>
                          <span
                            className="px-1.5 py-0.5 bg-blue-100 text-blue-700 rounded text-xs font-medium cursor-default">
                            {g.name}
                          </span>
                        </Tooltip>
                      ))
                    : <span className="text-xs text-gray-300">brak</span>
                )}
              </div>

              {/* Delete */}
              <Tooltip text="Wyczyść ten wpis harmonogramu">
                <button
                  onClick={ev => { ev.stopPropagation(); del(e.id) }}
                  className="text-gray-300 hover:text-red-400 transition-colors shrink-0"
                >
                  <Trash2 size={16} />
                </button>
              </Tooltip>

              {/* Expand arrow */}
              <Tooltip text={isOpen ? 'Zwiń edytor' : 'Rozwiń edytor'}>
                <span className="text-gray-400 shrink-0">
                  {isOpen ? <ChevronUp size={16} /> : <ChevronDown size={16} />}
                </span>
              </Tooltip>
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
                  groups={groups}
                  mode={mode}
                />
              </div>
            )}
          </div>
        )
      })}
    </div>
  )
}
