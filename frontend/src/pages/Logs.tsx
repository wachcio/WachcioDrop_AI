import { useEffect, useRef, useState } from 'react'
import toast from 'react-hot-toast'
import { RefreshCw, Trash2, Pause, Play } from 'lucide-react'
import { apiGetLogs, apiClearLogs, LogEntry } from '../api/client'
import { Tooltip } from '../components/Tooltip'

const LEVEL_META: Record<number, { label: string; cls: string; desc: string }> = {
  3: { label: 'ERROR', cls: 'bg-red-100 text-red-700',       desc: 'Błąd krytyczny — wymaga uwagi' },
  4: { label: 'WARN',  cls: 'bg-yellow-100 text-yellow-700', desc: 'Ostrzeżenie — coś może być nie tak' },
  6: { label: 'INFO',  cls: 'bg-blue-100 text-blue-600',     desc: 'Informacja diagnostyczna' },
  7: { label: 'DEBUG', cls: 'bg-gray-100 text-gray-500',     desc: 'Szczegóły debugowania' },
}

function formatTs(unix: number): string {
  if (!unix) return '—'
  const d = new Date(unix * 1000)
  const pad = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ` +
         `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

export default function Logs() {
  const [entries,    setEntries]    = useState<LogEntry[]>([])
  const [total,      setTotal]      = useState(0)
  const [filter,     setFilter]     = useState<number | null>(null)
  const [autoScroll, setAutoScroll] = useState(true)
  const [paused,     setPaused]     = useState(false)
  const bottomRef = useRef<HTMLDivElement>(null)

  const load = async () => {
    try {
      const r = await apiGetLogs(0, 200)
      setEntries(r.data.entries)
      setTotal(r.data.total)
    } catch { /* silent */ }
  }

  useEffect(() => { load() }, [])

  useEffect(() => {
    if (paused) return
    const t = setInterval(load, 3000)
    return () => clearInterval(t)
  }, [paused])

  useEffect(() => {
    if (autoScroll && !paused) {
      bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
    }
  }, [entries, autoScroll, paused])

  const clear = async () => {
    if (!confirm('Wyczyścić bufor logów?')) return
    try {
      await apiClearLogs()
      setEntries([])
      setTotal(0)
      toast.success('Logi wyczyszczone')
    } catch {
      toast.error('Błąd czyszczenia logów')
    }
  }

  const filtered = filter !== null ? entries.filter(e => e.level === filter) : entries

  return (
    <div className="flex flex-col gap-3">

      {/* Toolbar */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-3 flex items-center gap-2 flex-wrap">

        {/* Filtry poziomów */}
        <div className="flex gap-1.5 flex-wrap flex-1">
          <Tooltip text="Pokaż wszystkie wpisy logów">
            <button
              onClick={() => setFilter(null)}
              className={`px-2.5 py-1 rounded-full text-xs font-medium transition-colors
                ${filter === null ? 'bg-gray-800 text-white' : 'bg-gray-100 text-gray-600 hover:bg-gray-200'}`}
            >
              Wszystkie ({total})
            </button>
          </Tooltip>
          {([3, 4, 6, 7] as number[]).map(lvl => {
            const m = LEVEL_META[lvl]
            const cnt = entries.filter(e => e.level === lvl).length
            return (
              <Tooltip key={lvl} text={`${m.desc} — pokaż tylko ${m.label}`}>
                <button
                  onClick={() => setFilter(filter === lvl ? null : lvl)}
                  className={`px-2.5 py-1 rounded-full text-xs font-medium transition-colors
                    ${filter === lvl ? m.cls + ' ring-2 ring-offset-1 ring-current' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
                >
                  {m.label} ({cnt})
                </button>
              </Tooltip>
            )
          })}
        </div>

        {/* Akcje */}
        <div className="flex gap-1.5 shrink-0">
          <Tooltip text={paused ? 'Wznów automatyczne odświeżanie co 3s' : 'Wstrzymaj automatyczne odświeżanie'}>
            <button
              onClick={() => setPaused(p => !p)}
              className={`w-8 h-8 rounded-xl flex items-center justify-center transition-colors
                ${paused ? 'bg-yellow-100 text-yellow-700' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'}`}
            >
              {paused ? <Play size={14} /> : <Pause size={14} />}
            </button>
          </Tooltip>
          <Tooltip text="Pobierz najnowsze logi z urządzenia">
            <button
              onClick={load}
              className="w-8 h-8 rounded-xl bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 transition-colors"
            >
              <RefreshCw size={14} />
            </button>
          </Tooltip>
          <Tooltip text="Wyczyść bufor logów (nieodwracalne)">
            <button
              onClick={clear}
              className="w-8 h-8 rounded-xl bg-red-50 hover:bg-red-100 flex items-center justify-center text-red-500 transition-colors"
            >
              <Trash2 size={14} />
            </button>
          </Tooltip>
        </div>
      </div>

      {/* Log list */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 overflow-hidden">
        {filtered.length === 0 ? (
          <div className="p-8 text-center text-gray-400 text-sm">Brak wpisów</div>
        ) : (
          <div
            className="overflow-y-auto max-h-[75vh]"
            onScroll={e => {
              const el = e.currentTarget
              const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40
              setAutoScroll(atBottom)
            }}
          >
            <table className="w-full text-xs">
              <thead className="bg-gray-50 sticky top-0 z-10">
                <tr>
                  <th className="text-left px-3 py-2 text-gray-400 font-medium w-40">Czas</th>
                  <th className="text-left px-2 py-2 text-gray-400 font-medium w-16">Poziom</th>
                  <th className="text-left px-2 py-2 text-gray-400 font-medium w-20">Tag</th>
                  <th className="text-left px-2 py-2 text-gray-400 font-medium">Wiadomość</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-gray-50">
                {filtered.map((e, i) => {
                  const m = LEVEL_META[e.level] ?? { label: String(e.level), cls: 'bg-gray-100 text-gray-500', desc: '' }
                  return (
                    <tr key={i} className="hover:bg-gray-50 transition-colors">
                      <td className="px-3 py-1.5 font-mono text-gray-500 whitespace-nowrap">{formatTs(e.ts)}</td>
                      <td className="px-2 py-1.5">
                        <Tooltip text={m.desc || m.label} pos="bottom">
                          <span className={`px-1.5 py-0.5 rounded text-xs font-bold cursor-default ${m.cls}`}>
                            {m.label}
                          </span>
                        </Tooltip>
                      </td>
                      <td className="px-2 py-1.5 font-mono text-gray-500">{e.tag}</td>
                      <td className="px-2 py-1.5 text-gray-700 break-all">{e.msg}</td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
            <div ref={bottomRef} />
          </div>
        )}
      </div>

      {paused && (
        <p className="text-xs text-center text-yellow-600 font-medium">
          Odświeżanie wstrzymane — kliknij ▶ aby wznowić
        </p>
      )}
    </div>
  )
}
