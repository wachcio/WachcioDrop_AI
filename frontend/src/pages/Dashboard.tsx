import { useEffect, useRef, useState } from 'react'
import toast from 'react-hot-toast'
import { Wifi, Clock, Thermometer, Power, Droplets, WifiOff, KeyRound, Globe, CloudOff, Layers } from 'lucide-react'
import {
  apiGetStatus, apiSectionOn, apiSectionOff, apiAllOff, apiSetIrrigation, apiActivateGroup,
  SystemStatus, SectionState, GroupStatus
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

// ─── ToggleSwitch ─────────────────────────────────────────────────────────────

function ToggleSwitch({ checked, onChange }: { checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <button
      onClick={() => onChange(!checked)}
      className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
        ${checked ? 'bg-green-500' : 'bg-gray-300'}`}
    >
      <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
        transition-transform ${checked ? 'translate-x-6' : ''}`} />
    </button>
  )
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

// ─── GroupCard ────────────────────────────────────────────────────────────────

function GroupCard({
  group, duration, onActivate,
}: {
  group: GroupStatus
  duration: number
  onActivate: (id: number, active: boolean) => void
}) {
  const { id, name, active, remaining_sec, section_mask } = group
  const empty = section_mask === 0
  const sections = [1,2,3,4,5,6,7,8].filter(s => section_mask & (1 << (s-1)))

  return (
    <button
      onClick={() => !empty && onActivate(id, active)}
      disabled={empty}
      className={`relative flex items-center gap-3 rounded-2xl px-4 py-3 w-full text-left
        transition-all duration-200 active:scale-95 select-none
        ${empty
          ? 'bg-gray-50 text-gray-300 border border-dashed border-gray-200 cursor-not-allowed'
          : active
            ? 'bg-blue-500 text-white shadow-lg shadow-blue-200'
            : 'bg-white text-gray-700 shadow-sm border border-gray-100 hover:border-blue-200'}`}
    >
      {active && (
        <span className="absolute inset-0 rounded-2xl bg-blue-400 opacity-20 animate-pulse" />
      )}

      {/* Ikona + status */}
      <div className={`w-10 h-10 rounded-xl flex items-center justify-center shrink-0 z-10
        ${active ? 'bg-white/20' : 'bg-blue-50'}`}>
        <Layers size={20} className={active ? 'text-white' : 'text-blue-500'} />
      </div>

      {/* Nazwa + sekcje */}
      <div className="flex-1 min-w-0 z-10">
        <div className="flex items-center gap-2">
          <span className="font-bold text-sm">{name}</span>
          {active && (
            <span className="bg-white/30 text-white text-xs font-bold px-2 py-0.5 rounded-full">
              ON
            </span>
          )}
        </div>
        <div className="flex gap-1 mt-1 flex-wrap">
          {sections.map(s => (
            <span key={s} className={`text-xs px-1.5 py-0.5 rounded font-medium
              ${active ? 'bg-white/25 text-white' : 'bg-blue-50 text-blue-600'}`}>
              S{s}
            </span>
          ))}
        </div>
      </div>

      {/* Countdown */}
      <span className={`text-sm font-mono font-bold shrink-0 z-10
        ${active ? 'text-white' : 'text-gray-400'}`}>
        {active ? formatCountdown(remaining_sec) : 'OFF'}
      </span>
    </button>
  )
}

// ─── DurationSlider ───────────────────────────────────────────────────────────

function DurationSlider({ value, onChange }: { value: number; onChange: (v: number) => void }) {
  const MAX = 5400  // 1.5h
  const marks = [
    { v: 60,   l: '1 min' },
    { v: 300,  l: '5 min' },
    { v: 900,  l: '15 min' },
    { v: 1800, l: '30 min' },
    { v: 3600, l: '1h' },
    { v: 5400, l: '1.5h' },
  ]

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
      <div className="flex items-center justify-between mb-3">
        <span className="text-sm font-medium text-gray-700">Czas trwania</span>
        <div className="flex items-center gap-2">
          <button
            onClick={() => onChange(Math.max(60, value - 60))}
            className="w-7 h-7 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
              flex items-center justify-center text-gray-600 font-bold text-lg leading-none
              transition-all select-none"
          >−</button>
          <span className="text-lg font-bold text-green-700 w-20 text-center whitespace-nowrap">
            {formatDuration(value)}
          </span>
          <button
            onClick={() => onChange(Math.min(MAX, value + 60))}
            className="w-7 h-7 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
              flex items-center justify-center text-gray-600 font-bold text-lg leading-none
              transition-all select-none"
          >+</button>
        </div>
      </div>

      <input
        type="range"
        min={60}
        max={MAX}
        step={60}
        value={value}
        onChange={e => onChange(+e.target.value)}
        className="w-full accent-green-600 cursor-pointer"
      />

      <div className="relative mt-1 h-5">
        {marks.map(m => {
          const pct = (m.v - 60) / (MAX - 60) * 100
          const transform = pct < 5 ? 'none' : pct > 95 ? 'translateX(-100%)' : 'translateX(-50%)'
          return (
            <button
              key={m.v}
              onClick={() => onChange(m.v)}
              style={{ left: `${pct}%`, transform }}
              className={`absolute text-xs px-1 py-0.5 rounded transition-colors whitespace-nowrap
                ${value === m.v ? 'text-green-700 font-bold' : 'text-gray-400 hover:text-green-600'}`}
            >
              {m.l}
            </button>
          )
        })}
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
        setErrMsg('Brak połączenia z WachcioDrop')
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
      toast.error('Błąd komunikacji z WachcioDrop')
    }
  }

  const allOff = async () => {
    try {
      await apiAllOff()
      toast.success('Wszystkie sekcje wyłączone')
      load()
    } catch {
      toast.error('Błąd komunikacji z WachcioDrop')
    }
  }

  const activateGroup = async (id: number, active: boolean) => {
    try {
      if (active) {
        await apiAllOff()
        toast.success('Grupa wyłączona')
      } else {
        await apiActivateGroup(id, duration)
        const g = status?.groups.find(g => g.id === id)
        toast.success(`${g?.name ?? `Grupa ${id}`} włączona (${formatDuration(duration)})`)
      }
      load()
    } catch {
      toast.error('Błąd komunikacji z WachcioDrop')
    }
  }

  if (loading) {
    return (
      <div className="flex items-center justify-center h-48 text-gray-400">
        <div className="text-center">
          <Droplets size={40} className="mx-auto mb-2 opacity-30 animate-bounce" />
          <p className="text-sm">Łączenie z WachcioDrop…</p>
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

      {/* Grupy */}
      {status.groups.some(g => g.section_mask !== 0) && (
        <div>
          <h2 className="text-sm font-semibold text-gray-700 uppercase tracking-wide mb-3">Grupy</h2>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
            {status.groups.filter(g => g.section_mask !== 0).map(g => (
              <GroupCard
                key={g.id}
                group={g}
                duration={duration}
                onActivate={activateGroup}
              />
            ))}
          </div>
        </div>
      )}

      {/* Slider czasu */}
      <DurationSlider value={duration} onChange={setDuration} />

      {/* Sterowanie nawadnianiem */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4 flex flex-col gap-3">
        <h2 className="text-sm font-semibold text-gray-700 uppercase tracking-wide">Sterowanie</h2>

        <div className="flex items-center justify-between gap-3">
          <div className="flex items-center gap-3">
            <div className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0
              ${status.irrigation_today ? 'bg-green-100' : 'bg-red-100'}`}>
              <Droplets size={18} className={status.irrigation_today ? 'text-green-600' : 'text-red-500'} />
            </div>
            <div>
              <p className="text-sm font-medium text-gray-800">Nawadnianie dziś</p>
              <p className="text-xs text-gray-400">
                {status.irrigation_today ? 'Aktywne — harmonogram będzie wykonany' : 'Zablokowane — harmonogram pominięty'}
              </p>
            </div>
          </div>
          <ToggleSwitch
            checked={status.irrigation_today}
            onChange={async (v) => {
              try {
                await apiSetIrrigation(v, undefined)
                toast.success(v ? 'Nawadnianie aktywowane' : 'Nawadnianie zablokowane')
                load()
              } catch { toast.error('Błąd') }
            }}
          />
        </div>

        {status.php_url_set && (
          <>
            <div className="border-t border-gray-50" />
            <div className="flex items-center justify-between gap-3">
              <div className="flex items-center gap-3">
                <div className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0
                  ${status.ignore_php ? 'bg-orange-100' : 'bg-blue-100'}`}>
                  {status.ignore_php
                    ? <CloudOff size={18} className="text-orange-500" />
                    : <Globe    size={18} className="text-blue-500" />}
                </div>
                <div>
                  <p className="text-sm font-medium text-gray-800">Skrypt PHP</p>
                  <p className="text-xs text-gray-400">
                    {status.ignore_php ? 'Ignorowany — decyzja manualna' : 'Aktywny — decyzja z internetu'}
                  </p>
                </div>
              </div>
              <ToggleSwitch
                checked={status.ignore_php}
                onChange={async (v) => {
                  try {
                    await apiSetIrrigation(undefined, v)
                    toast.success(v ? 'PHP check wyłączony' : 'PHP check włączony')
                    load()
                  } catch { toast.error('Błąd') }
                }}
              />
            </div>
          </>
        )}
      </div>

    </div>
  )
}
