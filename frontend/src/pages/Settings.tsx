import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { KeyRound, Wifi, Radio, Globe, Clock, Save, RefreshCw } from 'lucide-react'
import { apiGetSettings, apiSaveSettings, apiGetTime, apiSetDateTime, Settings, setToken } from '../api/client'

type Tab = 'token' | 'wifi' | 'mqtt' | 'uslugi' | 'czas'

const TABS: { id: Tab; icon: typeof KeyRound; label: string }[] = [
  { id: 'token',  icon: KeyRound, label: 'Token'  },
  { id: 'wifi',   icon: Wifi,     label: 'WiFi'   },
  { id: 'mqtt',   icon: Radio,    label: 'MQTT'   },
  { id: 'uslugi', icon: Globe,    label: 'Usługi' },
  { id: 'czas',   icon: Clock,    label: 'Czas'   },
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

export default function SettingsPage() {
  const [tab, setTab] = useState<Tab>('token')
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
          <div className="flex flex-col gap-4">
            <Field label="PHP URL (daily check)">
              <input value={cfg.php_url}
                onChange={e => setCfg({ ...cfg, php_url: e.target.value })}
                placeholder="http://example.com/check.php"
                className={inputCls} />
            </Field>
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
