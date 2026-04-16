import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { Play, Pencil, Save, X, Droplets, Plus, Trash2 } from 'lucide-react'
import { apiGetGroups, apiSetGroup, apiDeleteGroup, apiActivateGroup, Group } from '../api/client'
import { Tooltip } from '../components/Tooltip'

const SECTIONS = [1, 2, 3, 4, 5, 6, 7, 8]

function formatSec(s: number): string {
  if (s < 60)   return `${s}s`
  if (s < 3600) return `${s / 60}min`
  return `${s / 3600}h`
}

// ─── GroupCard ────────────────────────────────────────────────────────────────

function GroupCard({
  group, duration, onEdit, onActivate,
}: {
  group: Group
  duration: number
  onEdit: (g: Group) => void
  onActivate: (id: number) => void
}) {
  const sections = SECTIONS.filter(s => group.section_mask & (1 << (s - 1)))
  const empty = sections.length === 0

  if (empty && group.name === `Grupa ${group.id}`) {
    return (
      <Tooltip text={`Skonfiguruj nową grupę nawadniania #${group.id}`} className="w-full">
        <button
          onClick={() => onEdit(group)}
          className="bg-white rounded-2xl border-2 border-dashed border-gray-200
            hover:border-green-300 hover:bg-green-50 p-4 flex flex-col items-center
            justify-center gap-2 transition-colors text-gray-400 hover:text-green-600 min-h-32 w-full"
        >
          <Plus size={24} />
          <span className="text-sm font-medium">Dodaj grupę {group.id}</span>
        </button>
      </Tooltip>
    )
  }

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4 flex flex-col gap-3 transition-colors duration-200 hover:bg-green-50">

      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="w-7 h-7 bg-green-100 text-green-700 rounded-full text-xs
            font-bold flex items-center justify-center shrink-0">
            {group.id}
          </span>
          <span className="font-semibold text-gray-800">{group.name}</span>
        </div>
        <Tooltip text={`Edytuj grupę "${group.name}"`}>
          <button
            onClick={() => onEdit(group)}
            className="text-gray-400 hover:text-green-600 transition-colors"
          >
            <Pencil size={16} />
          </button>
        </Tooltip>
      </div>

      {/* Sekcje */}
      <div className="flex gap-1.5 flex-wrap min-h-6">
        {sections.map(s => (
          <span key={s}
            className="flex items-center gap-1 bg-green-50 text-green-700 border border-green-200
              rounded-lg px-2 py-0.5 text-xs font-medium">
            <Droplets size={10} />
            S{s}
          </span>
        ))}
        {empty && <span className="text-xs text-gray-300 italic">Brak sekcji</span>}
      </div>

      {/* Activate */}
      <Tooltip text={empty ? 'Brak sekcji w grupie' : `Uruchom wszystkie sekcje grupy na ${formatSec(duration)}`}>
        <button
          onClick={() => onActivate(group.id)}
          disabled={empty}
          className={`flex items-center justify-center gap-2 rounded-xl py-2 text-sm font-medium
            transition-colors w-full ${empty
              ? 'bg-gray-100 text-gray-300 cursor-not-allowed'
              : 'bg-green-600 hover:bg-green-700 text-white shadow-sm'}`}
        >
          <Play size={15} />
          Uruchom ({formatSec(duration)})
        </button>
      </Tooltip>
    </div>
  )
}

// ─── EditModal ─────────────────────────────────────────────────────────────────

function EditModal({
  group, onChange, onSave, onCancel, onDelete, saving,
}: {
  group: Group
  onChange: (g: Group) => void
  onSave: () => void
  onCancel: () => void
  onDelete: () => void
  saving: boolean
}) {
  const set = (patch: Partial<Group>) => onChange({ ...group, ...patch })
  const isNew = group.section_mask === 0 && group.name === `Grupa ${group.id}`

  return (
    <div className="fixed inset-0 bg-black/40 flex items-end sm:items-center justify-center z-50 p-4">
      <div className="bg-white rounded-2xl shadow-xl w-full max-w-sm p-5 flex flex-col gap-4">

        <h3 className="font-bold text-gray-800">
          {isNew ? `Nowa grupa #${group.id}` : `Edytuj grupę #${group.id}`}
        </h3>

        <div>
          <label className="text-xs text-gray-500 block mb-1">Nazwa</label>
          <input
            value={group.name}
            onChange={e => set({ name: e.target.value })}
            maxLength={15}
            className="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm
              focus:outline-none focus:ring-2 focus:ring-green-400"
          />
        </div>

        <div>
          <label className="text-xs text-gray-500 block mb-2">Sekcje w grupie</label>
          <div className="grid grid-cols-4 gap-2">
            {SECTIONS.map(s => {
              const on = !!(group.section_mask & (1 << (s - 1)))
              return (
                <Tooltip key={s} text={on ? `Usuń sekcję ${s} z grupy` : `Dodaj sekcję ${s} do grupy`}>
                  <button
                    onClick={() => set({ section_mask: group.section_mask ^ (1 << (s - 1)) })}
                    className={`h-12 rounded-xl font-bold text-sm transition-colors w-full
                      ${on ? 'bg-green-500 text-white shadow-sm' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
                  >
                    <Droplets size={12} className={`mx-auto mb-0.5 ${on ? 'text-white' : 'text-gray-400'}`} />
                    S{s}
                  </button>
                </Tooltip>
              )
            })}
          </div>
        </div>

        <div className="flex gap-2 pt-1">
          <Tooltip text="Zapisz zmiany grupy" className="flex-1">
            <button
              onClick={onSave}
              disabled={saving}
              className="w-full flex items-center justify-center gap-1.5 bg-green-600 hover:bg-green-700
                text-white py-2.5 rounded-xl text-sm font-medium transition-colors disabled:opacity-50"
            >
              <Save size={15} />
              {saving ? 'Zapisywanie…' : 'Zapisz'}
            </button>
          </Tooltip>
          {!isNew && (
            <Tooltip text="Usuń tę grupę">
              <button
                onClick={onDelete}
                className="flex items-center justify-center gap-1.5 bg-red-50 hover:bg-red-100
                  text-red-500 px-3 py-2.5 rounded-xl text-sm font-medium transition-colors"
              >
                <Trash2 size={15} />
              </button>
            </Tooltip>
          )}
          <Tooltip text="Anuluj bez zapisywania">
            <button
              onClick={onCancel}
              className="flex items-center justify-center gap-1.5 bg-gray-100 hover:bg-gray-200
                text-gray-600 px-3 py-2.5 rounded-xl text-sm font-medium transition-colors"
            >
              <X size={15} />
            </button>
          </Tooltip>
        </div>
      </div>
    </div>
  )
}

// ─── Groups ────────────────────────────────────────────────────────────────────

export default function Groups() {
  const [groups,   setGroups]  = useState<Group[]>([])
  const [editing,  setEditing] = useState<Group | null>(null)
  const [duration, setDur]     = useState(1200)
  const [saving,   setSaving]  = useState(false)

  const load = () => apiGetGroups().then(r => setGroups(r.data))
  useEffect(() => { load() }, [])

  const save = async () => {
    if (!editing) return
    setSaving(true)
    try {
      await apiSetGroup(editing.id, editing)
      toast.success('Grupa zapisana')
      setEditing(null)
      load()
    } catch {
      toast.error('Błąd zapisu')
    }
    setSaving(false)
  }

  const deleteGroup = async () => {
    if (!editing) return
    setSaving(true)
    try {
      await apiDeleteGroup(editing.id)
      toast.success(`Grupa ${editing.id} usunięta`)
      setEditing(null)
      load()
    } catch {
      toast.error('Błąd usuwania grupy')
    }
    setSaving(false)
  }

  const activate = async (id: number) => {
    try {
      await apiActivateGroup(id, duration)
      const g = groups.find(g => g.id === id)
      toast.success(`${g?.name ?? `Grupa ${id}`} uruchomiona (${formatSec(duration)})`)
    } catch {
      toast.error('Błąd uruchamiania grupy')
    }
  }

  const DURATION_OPTS = [60, 120, 300, 600, 900, 1200, 1800, 2700, 3600]

  return (
    <div className="flex flex-col gap-4">

      {/* Duration picker */}
      <div className="flex items-center gap-3 bg-white rounded-2xl shadow-sm border border-gray-100 p-3">
        <Tooltip text="Czas trwania nawadniania przy uruchamianiu grupy">
          <span className="text-sm text-gray-600 shrink-0 cursor-default">Czas aktywacji:</span>
        </Tooltip>
        <div className="flex gap-1.5 flex-wrap">
          {DURATION_OPTS.map(s => (
            <Tooltip key={s} text={`Ustaw czas aktywacji na ${formatSec(s)}`}>
              <button
                onClick={() => setDur(s)}
                className={`px-3 py-1 rounded-full text-xs font-medium transition-colors
                  ${duration === s
                    ? 'bg-green-600 text-white shadow-sm'
                    : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
              >
                {formatSec(s)}
              </button>
            </Tooltip>
          ))}
        </div>
      </div>

      {/* Group grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
        {groups.map(g => (
          <GroupCard
            key={g.id}
            group={g}
            duration={duration}
            onEdit={g => setEditing({ ...g })}
            onActivate={activate}
          />
        ))}
      </div>

      {/* Edit modal */}
      {editing && (
        <EditModal
          group={editing}
          onChange={setEditing}
          onSave={save}
          onCancel={() => setEditing(null)}
          onDelete={deleteGroup}
          saving={saving}
        />
      )}
    </div>
  )
}
