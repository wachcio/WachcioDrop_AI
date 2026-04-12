import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { KeyRound, Wifi, Radio, Globe, Clock, Save, RefreshCw, SlidersHorizontal, Droplets, CloudOff } from 'lucide-react'
import { apiGetSettings, apiSaveSettings, apiGetTime, apiSetDateTime, apiGetStatus, apiSetIrrigation, api, Settings, setToken, SystemStatus } from '../api/client'

export type ScheduleMode = 'sections' | 'groups'
export const SCHEDULE_MODE_KEY = 'schedule_mode'
export function getScheduleMode(): ScheduleMode {
  return (localStorage.getItem(SCHEDULE_MODE_KEY) as ScheduleMode) ?? 'sections'
}

type Tab = 'glowne' | 'token' | 'wifi' | 'mqtt' | 'uslugi' | 'czas'

const TABS: { id: Tab; icon: typeof KeyRound; label: string }[] = [
  { id: 'glowne', icon: SlidersHorizontal, label: 'Główne' },
  { id: 'token',  icon: KeyRound,          label: 'Token'  },
  { id: 'wifi',   icon: Wifi,              label: 'WiFi'   },
  { id: 'mqtt',   icon: Radio,             label: 'MQTT'   },
  { id: 'uslugi', icon: Globe,             label: 'Usługi' },
  { id: 'czas',   icon: Clock,             label: 'Czas'   },
]

function Field({
  label, children,
}: {
  label: string
  children: React.ReactNode
}) {
  return (
    <div className="flex flex-col gap-1">
      <label className="text-xs font-medium text-gray-500">{label}</label>
      {children}
    </div>
  )
}

const inputCls = `w-full border border-gray-200 rounded-xl px-3 py-2.5 text-sm
  focus:outline-none focus:ring-2 focus:ring-green-400 bg-white`

function ScheduleModeConfirm({
  current, onConfirm, onCancel,
}: {
  current: ScheduleMode
  onConfirm: () => void
  onCancel: () => void
}) {
  const next = current === 'sections' ? 'grupy' : 'sekcje'
  const prev = current === 'sections' ? 'sekcje' : 'grupy'
  return (
    <div className="fixed inset-0 bg-black/40 flex items-center justify-center z-50 p-4">
      <div className="bg-white rounded-2xl shadow-xl w-full max-w-sm p-5 flex flex-col gap-4">
        <div className="flex items-start gap-3">
          <div className="w-10 h-10 rounded-xl bg-yellow-100 flex items-center justify-center shrink-0">
            <span className="text-xl">⚠️</span>
          </div>
          <div>
            <h3 className="font-bold text-gray-800">Zmiana trybu harmonogramu</h3>
            <p className="text-sm text-gray-600 mt-1">
              Przełączasz harmonogram z <strong>{prev}</strong> na <strong>{next}</strong>.
            </p>
            <p className="text-sm text-gray-500 mt-2">
              Sprawdź wpisy w harmonogramie — ustawione {prev} mogą nie pokrywać się
              z {next === 'grupy' ? 'skonfigurowanymi grupami' : 'wybranymi sekcjami'}.
            </p>
          </div>
        </div>
        <div className="flex gap-2">
          <button
            onClick={onConfirm}
            className="flex-1 bg-green-600 hover:bg-green-700 text-white py-2.5
              rounded-xl text-sm font-medium transition-colors"
          >
            Zmień tryb
          </button>
          <button
            onClick={onCancel}
            className="flex-1 bg-gray-100 hover:bg-gray-200 text-gray-700 py-2.5
              rounded-xl text-sm font-medium transition-colors"
          >
            Anuluj
          </button>
        </div>
      </div>
    </div>
  )
}

export default function SettingsPage() {
  const [tab, setTab] = useState<Tab>('glowne')
  const [scheduleMode, setScheduleMode] = useState<ScheduleMode>(getScheduleMode())
  const [pendingMode, setPendingMode]   = useState<ScheduleMode | null>(null)
  const [devStatus, setDevStatus]       = useState<SystemStatus | null>(null)
  const [checkTime, setCheckTime]       = useState<string>('')
  const [cfg, setCfg] = useState<Settings>({
    wifi_ssid: '', mqtt_uri: '', mqtt_user: '', php_url: '',
    ntp_server: 'pool.ntp.org', tz_offset: 2,
  })
  const [pass, setPass]             = useState({ wifi: '', mqtt: '', token: '' })
  const [rtcTime, setRtcTime]       = useState('')
  const [saving, setSaving]         = useState(false)
  const [localToken, setLocalToken] = useState(
    localStorage.getItem('api_token') ?? ''
  )

  useEffect(() => {
    apiGetSettings().then(r => setCfg(r.data)).catch(() => {})
    apiGetTime().then(r => setRtcTime(r.data.time.replace('T', ' '))).catch(() => {})
    apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
    api.get<{ daily_check_hour: number; daily_check_minute: number }>('/api/info')
      .then(r => {
        const h = String(r.data.daily_check_hour).padStart(2, '0')
        const m = String(r.data.daily_check_minute).padStart(2, '0')
        setCheckTime(`${h}:${m}`)
      }).catch(() => {})
  }, [])

  // Lokalny tick co 1s — inkrementuje czas bez odpytywania API
  useEffect(() => {
    const t = setInterval(() => {
      setRtcTime(prev => {
        if (!prev) return prev
        const d = new Date(prev.replace(' ', 'T'))
        if (isNaN(d.getTime())) return prev
        d.setSeconds(d.getSeconds() + 1)
        const pad = (n: number) => String(n).padStart(2, '0')
        return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ` +
               `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
      })
    }, 1000)
    return () => clearInterval(t)
  }, [])

  const save = async () => {
    setSaving(true)
    try {
      await apiSaveSettings({
        ...cfg,
        wifi_pass:  pass.wifi  || undefined,
        mqtt_pass:  pass.mqtt  || undefined,
        api_token:  pass.token || undefined,
      } as any)
      toast.success('Ustawienia zapisane')
      apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
    } catch {
      toast.error('Błąd zapisu ustawień')
    }
    setSaving(false)
  }

  const syncTime = async () => {
    try {
      // Wyślij czas lokalny przeglądarki jako datetime string (nie UTC)
      const now = new Date()
      const pad = (n: number) => String(n).padStart(2, '0')
      const datetime = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}` +
                       `T${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`
      await apiSetDateTime(datetime)
      toast.success('Czas zsynchronizowany z przeglądarką')
      apiGetTime().then(r => setRtcTime(r.data.time.replace('T', ' '))).catch(() => {})
    } catch {
      toast.error('Błąd synchronizacji czasu')
    }
  }

  const saveToken = () => {
    setToken(localToken)
    toast.success('Token zapisany w przeglądarce')
  }

  return (
    <div className="flex flex-col gap-4">

      {/* Tab bar */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-1 flex gap-1">
        {TABS.map(({ id, icon: Icon, label }) => (
          <button
            key={id}
            onClick={() => setTab(id)}
            className={`flex-1 flex flex-col sm:flex-row items-center justify-center gap-1
              py-2 px-1 rounded-xl text-xs font-medium transition-colors
              ${tab === id
                ? 'bg-green-600 text-white shadow-sm'
                : 'text-gray-500 hover:bg-gray-50'}`}
          >
            <Icon size={15} />
            <span className="hidden sm:block">{label}</span>
          </button>
        ))}
      </div>

      {/* Content */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-5">

        {tab === 'glowne' && (
          <div className="flex flex-col gap-5">

            {/* Nawadnianie dziś */}
            {devStatus && (
              <div className="flex items-center justify-between gap-3">
                <div className="flex items-center gap-3">
                  <div className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0
                    ${devStatus.irrigation_today ? 'bg-green-100' : 'bg-red-100'}`}>
                    <Droplets size={18} className={devStatus.irrigation_today ? 'text-green-600' : 'text-red-500'} />
                  </div>
                  <div>
                    <p className="text-sm font-medium text-gray-800">Nawadnianie dziś</p>
                    <p className="text-xs text-gray-400">
                      {devStatus.irrigation_today ? 'Aktywne — harmonogram będzie wykonany' : 'Zablokowane — harmonogram pominięty'}
                    </p>
                  </div>
                </div>
                <button
                  onClick={async () => {
                    try {
                      await apiSetIrrigation(!devStatus.irrigation_today, undefined)
                      toast.success(devStatus.irrigation_today ? 'Nawadnianie zablokowane' : 'Nawadnianie aktywowane')
                      apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
                    } catch { toast.error('Błąd') }
                  }}
                  className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
                    ${devStatus.irrigation_today ? 'bg-green-500' : 'bg-gray-300'}`}
                >
                  <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
                    transition-transform ${devStatus.irrigation_today ? 'translate-x-6' : ''}`} />
                </button>
              </div>
            )}

            <div className="border-t border-gray-100" />

            <div className="flex items-center justify-between gap-4">
              <div>
                <p className="text-sm font-medium text-gray-800">Tryb harmonogramu</p>
                <p className="text-xs text-gray-400 mt-0.5">
                  {scheduleMode === 'sections'
                    ? 'Harmonogram pozwala wybierać sekcje'
                    : 'Harmonogram pozwala wybierać grupy'}
                </p>
              </div>
              <div className="flex items-center gap-2 shrink-0">
                <span className={`text-xs font-medium ${scheduleMode === 'sections' ? 'text-green-700' : 'text-gray-400'}`}>
                  Sekcje
                </span>
                <button
                  onClick={() => {
                    const next: ScheduleMode = scheduleMode === 'sections' ? 'groups' : 'sections'
                    setPendingMode(next)
                  }}
                  className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
                    ${scheduleMode === 'groups' ? 'bg-blue-500' : 'bg-green-500'}`}
                >
                  <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
                    transition-transform ${scheduleMode === 'groups' ? 'translate-x-6' : ''}`} />
                </button>
                <span className={`text-xs font-medium ${scheduleMode === 'groups' ? 'text-blue-600' : 'text-gray-400'}`}>
                  Grupy
                </span>
              </div>
            </div>
          </div>
        )}

        {tab === 'token' && (
          <div className="flex flex-col gap-4">
            <p className="text-sm text-gray-600">
              Token API jest przechowywany tylko w tej przeglądarce.
              Możesz go odczytać na wyświetlaczu OLED urządzenia.
            </p>
            <Field label="Token Bearer">
              <input
                value={localToken}
                onChange={e => setLocalToken(e.target.value)}
                placeholder="wklej token z OLED…"
                className={inputCls}
              />
            </Field>
            <button
              onClick={saveToken}
              className="flex items-center gap-2 bg-green-600 hover:bg-green-700 text-white
                px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
            >
              <Save size={15} />
              Zapisz token lokalnie
            </button>
          </div>
        )}

        {tab === 'wifi' && (
          <div className="flex flex-col gap-4">
            <Field label="Nazwa sieci (SSID)">
              <input value={cfg.wifi_ssid}
                onChange={e => setCfg({ ...cfg, wifi_ssid: e.target.value })}
                className={inputCls} />
            </Field>
            <Field label="Hasło (puste = bez zmian)">
              <input type="password" value={pass.wifi}
                onChange={e => setPass({ ...pass, wifi: e.target.value })}
                className={inputCls} placeholder="••••••••" />
            </Field>
            <SaveButton saving={saving} onClick={save} />
          </div>
        )}

        {tab === 'mqtt' && (
          <div className="flex flex-col gap-4">
            <Field label="Broker URI">
              <input value={cfg.mqtt_uri}
                onChange={e => setCfg({ ...cfg, mqtt_uri: e.target.value })}
                placeholder="mqtt://192.168.1.10 lub mqtts://host:8883"
                className={inputCls} />
            </Field>
            <Field label="Użytkownik">
              <input value={cfg.mqtt_user}
                onChange={e => setCfg({ ...cfg, mqtt_user: e.target.value })}
                className={inputCls} />
            </Field>
            <Field label="Hasło (puste = bez zmian)">
              <input type="password" value={pass.mqtt}
                onChange={e => setPass({ ...pass, mqtt: e.target.value })}
                className={inputCls} placeholder="••••••••" />
            </Field>
            <SaveButton saving={saving} onClick={save} />
          </div>
        )}

        {tab === 'uslugi' && (
          <div className="flex flex-col gap-5">

            {/* Skrypt zewnętrzny */}
            <div>
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-3">
                Skrypt zewnętrzny
              </p>
              <div className="flex flex-col gap-3">
                <Field label={`URL skryptu (sprawdzanie codziennie o ${checkTime || '…'})`}>
                  {(() => {
                    const url = cfg.php_url
                    const invalid = url.length > 0 && !/^https?:\/\/.+\..+/.test(url)
                    return (
                      <>
                        <input value={url}
                          onChange={e => setCfg({ ...cfg, php_url: e.target.value })}
                          placeholder="http://example.com/check.php"
                          className={`${inputCls} ${invalid ? 'border-red-400 focus:ring-red-400' : ''}`} />
                        {invalid && (
                          <p className="text-xs text-red-500 mt-1">Nieprawidłowy URL — powinien zaczynać się od http:// lub https://</p>
                        )}
                      </>
                    )
                  })()}
                </Field>
                {devStatus && (devStatus.php_url_set || cfg.php_url.length > 0) && (
                  <div className="flex items-center justify-between gap-3 py-1">
                    <div className="flex items-center gap-3">
                      <div className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0
                        ${devStatus.ignore_php ? 'bg-orange-100' : 'bg-blue-100'}`}>
                        {devStatus.ignore_php
                          ? <CloudOff size={18} className="text-orange-500" />
                          : <Globe    size={18} className="text-blue-500" />}
                      </div>
                      <div>
                        <p className="text-sm font-medium text-gray-800">Ignoruj skrypt</p>
                        <p className="text-xs text-gray-400">
                          {devStatus.ignore_php ? 'Wyłączony — decyzja manualna' : 'Aktywny — decyzja z internetu'}
                        </p>
                      </div>
                    </div>
                    <button
                      onClick={async () => {
                        try {
                          await apiSetIrrigation(undefined, !devStatus.ignore_php)
                          toast.success(devStatus.ignore_php ? 'Skrypt włączony' : 'Skrypt wyłączony')
                          apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
                        } catch { toast.error('Błąd') }
                      }}
                      className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
                        ${devStatus.ignore_php ? 'bg-orange-400' : 'bg-gray-300'}`}
                    >
                      <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
                        transition-transform ${devStatus.ignore_php ? 'translate-x-6' : ''}`} />
                    </button>
                  </div>
                )}
              </div>
            </div>

            <div className="border-t border-gray-100" />

            {/* Czas / NTP */}
            <div>
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-3">
                Synchronizacja czasu (NTP)
              </p>
              <div className="flex flex-col gap-3">
                <Field label="Serwer NTP">
                  <input value={cfg.ntp_server}
                    onChange={e => setCfg({ ...cfg, ntp_server: e.target.value })}
                    className={inputCls} />
                </Field>
                <Field label="Strefa czasowa (offset UTC, np. 2 dla CEST)">
                  <input type="number" min={-12} max={14} value={cfg.tz_offset}
                    onChange={e => setCfg({ ...cfg, tz_offset: +e.target.value })}
                    className={`${inputCls} w-24`} />
                </Field>
              </div>
            </div>

            <SaveButton saving={saving} onClick={save} />
          </div>
        )}

        {tab === 'czas' && (
          <div className="flex flex-col gap-4">
            <div className="bg-gray-50 rounded-xl p-4 text-center">
              <p className="text-xs text-gray-400 mb-1">Aktualny czas ESP32 (lokalny)</p>
              <p className="text-2xl font-mono font-bold text-gray-800">
                {rtcTime ? rtcTime.replace('T', ' ') : '—'}
              </p>
            </div>
            <p className="text-sm text-gray-500">
              Synchronizacja ustawia czas RTC na podstawie czasu lokalnego przeglądarki.
            </p>
            <button
              onClick={syncTime}
              className="flex items-center gap-2 bg-gray-700 hover:bg-gray-800 text-white
                px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
            >
              <RefreshCw size={15} />
              Synchronizuj z przeglądarką
            </button>
          </div>
        )}

      </div>

      {pendingMode && (
        <ScheduleModeConfirm
          current={scheduleMode}
          onConfirm={() => {
            setScheduleMode(pendingMode)
            localStorage.setItem(SCHEDULE_MODE_KEY, pendingMode)
            setPendingMode(null)
            toast.success(`Tryb harmonogramu: ${pendingMode === 'groups' ? 'grupy' : 'sekcje'}`)
          }}
          onCancel={() => setPendingMode(null)}
        />
      )}
    </div>
  )
}

function SaveButton({ saving, onClick }: { saving: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      disabled={saving}
      className="flex items-center gap-2 bg-green-600 hover:bg-green-700 text-white
        px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit disabled:opacity-50"
    >
      <Save size={15} />
      {saving ? 'Zapisywanie…' : 'Zapisz ustawienia'}
    </button>
  )
}
