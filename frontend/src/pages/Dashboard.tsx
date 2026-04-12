import { useEffect, useRef, useState } from 'react'
import toast from 'react-hot-toast'
import { Wifi, Clock, Thermometer, Power, Droplets, WifiOff, KeyRound } from 'lucide-react'
import {
  apiGetStatus, apiSectionOn, apiSectionOff, apiAllOff,
  SystemStatus, SectionState
} from '../api/client'

// ─── helpers ─────────────────────────────────────────────────────────────────

function formatUptime(sec: number): string {
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return h > 0 ? `${h}h ${m}m` : `${m}m`
}

function formatCountdown(sec: number | null): string {
  if (sec === null || sec <= 0) return '∞'
  const m = Math.floor(sec / 60)
  const s = sec % 60
  return `${m}:${String(s).padStart(2, '0')}`
}

function formatDuration(sec: number): string {
  if (sec < 60)   return `${sec}s`
  if (sec < 3600) return `${sec / 60} min`
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  return m ? `${h}h ${m}m` : `${h}h`
}

function rssiStrength(rssi: number): { bars: number; label: string; color: string } {
  if (rssi >= -50) return { bars: 4, label: 'Doskonały', color: 'text-green-500' }
  if (rssi >= -65) return { bars: 3, label: 'Dobry',     color: 'text-green-400' }
  if (rssi >= -75) return { bars: 2, label: 'Słaby',     color: 'text-yellow-400' }
  return             { bars: 1, label: 'Bardzo słaby', color: 'text-red-400' }
}

// ─── RssiIcon ─────────────────────────────────────────────────────────────────

function RssiIcon({ rssi }: { rssi: number }) {
  const { bars, color } = rssiStrength(rssi)
  return (
    <span className={`flex items-end gap-0.5 h-4 ${color}`}>
      {[1, 2, 3, 4].map(b => (
        <span
          key={b}
          className={`w-1 rounded-sm ${b <= bars ? 'bg-current' : 'bg-gray-300'}`}
          style={{ height: `${b * 25}%` }}
        />
      ))}
    </span>
  )
}

// ─── SectionCard ──────────────────────────────────────────────────────────────

function SectionCard({
  section, duration, onToggle,
}: {
  section: SectionState
  duration: number
  onToggle: (id: number, active: boolean) => void
}) {
  const { id, active, remaining_sec } = section

  return (
    <button
      onClick={() => onToggle(id, active)}
      className={`relative flex flex-col items-center gap-2 rounded-2xl p-4 w-full
        transition-all duration-200 active:scale-95 select-none
        ${active
          ? 'bg-green-500 text-white shadow-lg shadow-green-200'
          : 'bg-white text-gray-600 shadow-sm border border-gray-100 hover:border-green-200'}`}
    >
      {/* Pulsujący ring gdy aktywna */}
      {active && (
        <span className="absolute inset-0 rounded-2xl bg-green-400 opacity-30 animate-pulse" />
      )}

      <Droplets
        size={28}
        className={active ? 'text-white' : 'text-green-600'}
      />

      <span className="font-bold text-base z-10">Sekcja {id}</span>

      <span className={`text-xs font-mono z-10 ${active ? 'text-green-100' : 'text-gray-400'}`}>
        {active ? formatCountdown(remaining_sec) : 'OFF'}
      </span>
    </button>
  )
}

// ─── DurationSlider ───────────────────────────────────────────────────────────

function DurationSlider({ value, onChange }: { value: number; onChange: (v: number) => void }) {
  const marks = [
    { v: 60,   l: '1 min' },
    { v: 300,  l: '5 min' },
    { v: 900,  l: '15 min' },
    { v: 1800, l: '30 min' },
    { v: 3600, l: '1h' },
    { v: 7200, l: '2h' },
  ]

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
      <div className="flex items-center justify-between mb-3">
        <span className="text-sm font-medium text-gray-700">Czas trwania</span>
        <span className="text-lg font-bold text-green-700">{formatDuration(value)}</span>
      </div>

      <input
        type="range"
        min={60}
        max={7200}
        step={60}
        value={value}
        onChange={e => onChange(+e.target.value)}
        className="w-full accent-green-600 cursor-pointer"
      />

      <div className="flex justify-between mt-1">
        {marks.map(m => (
          <button
            key={m.v}
            onClick={() => onChange(m.v)}
            className={`text-xs px-1 py-0.5 rounded transition-colors
              ${value === m.v ? 'text-green-700 font-bold' : 'text-gray-400 hover:text-green-600'}`}
          >
            {m.l}
          </button>
        ))}
      </div>
    </div>
  )
}

// ─── Dashboard ────────────────────────────────────────────────────────────────

export default function Dashboard() {
  const [status,   setStatus]   = useState<SystemStatus | null>(null)
  const [duration, setDuration] = useState(300)
  const [loading,  setLoading]  = useState(true)
  const [errMsg,   setErrMsg]   = useState('')
  const countdownRef = useRef<ReturnType<typeof setInterval> | null>(null)

  const load = async () => {
    try {
      const r = await apiGetStatus()
      setStatus(r.data)
      setErrMsg('')
      setLoading(false)
    } catch (e: any) {
      setLoading(false)
      if (e?.response?.status === 401) {
        setErrMsg('Brak autoryzacji — ustaw token w Ustawieniach → Token')
      } else {
        setErrMsg('Brak połączenia z ESP32')
      }
    }
  }

  // Odświeżanie co 3s
  useEffect(() => {
    load()
    const t = setInterval(load, 3000)
    return () => clearInterval(t)
  }, [])

  // Lokalny countdown (tick co 1s bez API)
  useEffect(() => {
    if (countdownRef.current) clearInterval(countdownRef.current)
    countdownRef.current = setInterval(() => {
      setStatus(prev => {
        if (!prev) return prev
        const sections = prev.sections.map(s => ({
          ...s,
          remaining_sec:
            s.active && s.remaining_sec !== null && s.remaining_sec > 0
              ? s.remaining_sec - 1
              : s.remaining_sec,
        }))
        return { ...prev, sections }
      })
    }, 1000)
    return () => { if (countdownRef.current) clearInterval(countdownRef.current) }
  }, [])

  const toggleSection = async (id: number, active: boolean) => {
    try {
      if (active) {
        await apiSectionOff(id)
        toast.success(`Sekcja ${id} wyłączona`)
      } else {
        await apiSectionOn(id, duration)
        toast.success(`Sekcja ${id} włączona (${formatDuration(duration)})`)
      }
      load()
    } catch {
      toast.error('Błąd komunikacji z ESP32')
    }
  }

  const allOff = async () => {
    try {
      await apiAllOff()
      toast.success('Wszystkie sekcje wyłączone')
      load()
    } catch {
      toast.error('Błąd komunikacji z ESP32')
    }
  }

  if (loading) {
    return (
      <div className="flex items-center justify-center h-48 text-gray-400">
        <div className="text-center">
          <Droplets size={40} className="mx-auto mb-2 opacity-30 animate-bounce" />
          <p className="text-sm">Łączenie z ESP32…</p>
        </div>
      </div>
    )
  }

  if (!status) {
    const isAuth = errMsg.includes('autoryzacji')
    return (
      <div className="flex items-center justify-center h-48">
        <div className="text-center">
          {isAuth
            ? <KeyRound size={40} className="mx-auto mb-2 text-yellow-400" />
            : <WifiOff size={40} className="mx-auto mb-2 text-red-400" />}
          <p className="text-sm font-medium text-gray-600">{errMsg}</p>
          {isAuth && (
            <a href="/settings" className="mt-2 inline-block text-xs text-green-600 underline">
              Przejdź do Ustawień
            </a>
          )}
        </div>
      </div>
    )
  }

  const anyActive = status.sections_active > 0

  return (
    <div className="flex flex-col gap-4">

      {/* Status bar */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">

          <div className="flex items-center gap-2">
            <Clock size={16} className="text-gray-400 shrink-0" />
            <div>
              <p className="text-xs text-gray-400">Czas</p>
              <p className="text-sm font-semibold text-gray-800 leading-tight">
                {status.time.replace('T', ' ').substring(0, 16)}
              </p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Wifi size={16} className="text-gray-400 shrink-0" />
            <div>
              <p className="text-xs text-gray-400">WiFi</p>
              <div className="flex items-center gap-1.5">
                <RssiIcon rssi={status.rssi} />
                <p className="text-sm font-semibold text-gray-800">{status.rssi} dBm</p>
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Thermometer size={16} className="text-gray-400 shrink-0" />
            <div>
              <p className="text-xs text-gray-400">Uptime</p>
              <p className="text-sm font-semibold text-gray-800">{formatUptime(status.uptime_sec)}</p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Droplets size={16} className="text-gray-400 shrink-0" />
            <div>
              <p className="text-xs text-gray-400">Nawadnianie</p>
              <span className={`inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium
                ${status.irrigation_today
                  ? 'bg-green-100 text-green-700'
                  : 'bg-red-100 text-red-600'}`}>
                {status.irrigation_today ? 'Aktywne' : 'Zablokowane'}
              </span>
            </div>
          </div>

        </div>
      </div>

      {/* Sekcje */}
      <div>
        <div className="flex items-center justify-between mb-3">
          <h2 className="text-sm font-semibold text-gray-700 uppercase tracking-wide">Sekcje</h2>
          {anyActive && (
            <button
              onClick={allOff}
              className="flex items-center gap-1.5 bg-red-500 hover:bg-red-600 text-white
                text-xs font-medium px-3 py-1.5 rounded-full transition-colors shadow-sm"
            >
              <Power size={13} />
              Wyłącz wszystko
            </button>
          )}
        </div>

        <div className="grid grid-cols-4 gap-3">
          {status.sections.map(sec => (
            <SectionCard
              key={sec.id}
              section={sec}
              duration={duration}
              onToggle={toggleSection}
            />
          ))}
        </div>
      </div>

      {/* Slider czasu */}
      <DurationSlider value={duration} onChange={setDuration} />

    </div>
  )
}
