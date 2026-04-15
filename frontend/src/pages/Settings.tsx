import { useEffect, useState } from 'react'
import toast from 'react-hot-toast'
import { KeyRound, Wifi, Radio, Globe, Clock, Save, RefreshCw, SlidersHorizontal, Droplets, CloudOff, Antenna, Upload, AlertTriangle, CheckCircle, XCircle, Download, ArchiveRestore, Thermometer } from 'lucide-react'
import { apiGetSettings, apiSaveSettings, apiGetTime, apiSetDateTime, apiGetStatus, apiSetIrrigation, apiSyncNtp, apiOtaUpload, apiSpiffsUpload, apiRestart, apiExportSettings, apiImportSettings, api, Settings, setToken, SystemStatus } from '../api/client'
import { Tooltip } from '../components/Tooltip'

export type ScheduleMode = 'sections' | 'groups'
export const SCHEDULE_MODE_KEY = 'schedule_mode'
export function getScheduleMode(): ScheduleMode {
  return (localStorage.getItem(SCHEDULE_MODE_KEY) as ScheduleMode) ?? 'sections'
}

type Tab = 'glowne' | 'token' | 'wifi' | 'mqtt' | 'uslugi' | 'czas' | 'ota' | 'backup'

const TABS: { id: Tab; icon: typeof KeyRound; label: string; tip: string }[] = [
  { id: 'glowne', icon: SlidersHorizontal, label: 'Główne',  tip: 'Nawadnianie, ochrona przed mrozem, tryb harmonogramu' },
  { id: 'token',  icon: KeyRound,          label: 'Token',   tip: 'Token autoryzacji API' },
  { id: 'wifi',   icon: Wifi,              label: 'WiFi',    tip: 'Połączenie z siecią WiFi' },
  { id: 'mqtt',   icon: Radio,             label: 'MQTT',    tip: 'Broker MQTT do integracji zewnętrznych' },
  { id: 'uslugi', icon: Globe,             label: 'Usługi',  tip: 'Skrypt zewnętrzny, NTP, Graylog' },
  { id: 'czas',   icon: Clock,             label: 'Czas',    tip: 'Synchronizacja czasu RTC' },
  { id: 'ota',    icon: Upload,            label: 'OTA',     tip: 'Aktualizacja firmware i interfejsu przez WiFi' },
  { id: 'backup', icon: Download,          label: 'Backup',  tip: 'Eksport i import ustawień urządzenia' },
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

function OtaProgress({ label, pct }: { label: string; pct: number }) {
  return (
    <div className="flex flex-col gap-2">
      <div className="flex justify-between text-xs text-gray-500">
        <span>{label}</span>
        <span>{pct}%</span>
      </div>
      <div className="w-full bg-gray-200 rounded-full h-2.5">
        <div
          className="bg-orange-500 h-2.5 rounded-full transition-all duration-300"
          style={{ width: `${pct}%` }}
        />
      </div>
      <p className="text-xs text-gray-400">Nie zamykaj strony podczas aktualizacji</p>
    </div>
  )
}

function OtaSuccess({ msg }: { msg: string }) {
  return (
    <div className="flex items-center gap-3 p-3 bg-green-50 border border-green-100 rounded-xl">
      <CheckCircle size={18} className="text-green-600 shrink-0" />
      <p className="text-sm font-medium text-green-800">{msg}</p>
    </div>
  )
}

function OtaError({ msg }: { msg: string }) {
  return (
    <div className="flex items-center gap-3 p-3 bg-red-50 border border-red-100 rounded-xl">
      <XCircle size={18} className="text-red-500 shrink-0" />
      <div>
        <p className="text-sm font-medium text-red-800">Błąd aktualizacji</p>
        <p className="text-xs text-red-600 mt-0.5">{msg}</p>
      </div>
    </div>
  )
}

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
  const [deviceBuildHash, setDeviceBuildHash] = useState<string>('')
  const [cfg, setCfg] = useState<Settings>({
    wifi_ssid: '', mqtt_uri: '', mqtt_user: '', php_url: '',
    ntp_server: 'pool.ntp.org', tz_offset: 2,
    graylog_host: '', graylog_port: 12201, graylog_enabled: false, graylog_level: 6,
    frost_protection_enabled: false, frost_temp_threshold: 3, frost_recovery_delay_min: 60,
  })
  const [pass, setPass]             = useState({ wifi: '', mqtt: '', token: '' })
  const [rtcTime, setRtcTime]       = useState('')
  const [saving, setSaving]         = useState(false)
  const [localToken, setLocalToken] = useState(
    localStorage.getItem('api_token') ?? ''
  )
  type OtaStatus = 'idle' | 'uploading' | 'done' | 'rebooting' | 'error'
  const [otaFile, setOtaFile]         = useState<File | null>(null)
  const [otaStatus, setOtaStatus]     = useState<OtaStatus>('idle')
  const [otaProgress, setOtaProgress] = useState(0)
  const [otaError, setOtaError]       = useState('')
  const [otaFileSha, setOtaFileSha]   = useState('')

  const [spiffsFile, setSpiffsFile]         = useState<File | null>(null)
  const [spiffsStatus, setSpiffsStatus]     = useState<OtaStatus>('idle')
  const [spiffsProgress, setSpiffsProgress] = useState(0)
  const [spiffsError, setSpiffsError]       = useState('')

  useEffect(() => {
    apiGetSettings().then(r => setCfg(r.data)).catch(() => {})
    apiGetTime().then(r => setRtcTime(r.data.time.replace('T', ' '))).catch(() => {})
    apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
    api.get<{ daily_check_hour: number; daily_check_minute: number; build_hash: string }>('/api/info')
      .then(r => {
        const h = String(r.data.daily_check_hour).padStart(2, '0')
        const m = String(r.data.daily_check_minute).padStart(2, '0')
        setCheckTime(`${h}:${m}`)
        if (r.data.build_hash) setDeviceBuildHash(r.data.build_hash)
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

  const startOta = async () => {
    if (!otaFile) return
    setOtaStatus('uploading')
    setOtaProgress(0)
    setOtaError('')
    try {
      await apiOtaUpload(otaFile, setOtaProgress)
      setOtaStatus('rebooting')
      toast.success('Firmware wgrany — urządzenie restartuje się')
    } catch (e: any) {
      const msg = e?.response?.data?.error ?? 'Błąd wgrywania firmware'
      setOtaError(msg)
      setOtaStatus('error')
      toast.error(msg)
    }
  }

  const startSpiffsOta = async () => {
    if (!spiffsFile) return
    setSpiffsStatus('uploading')
    setSpiffsProgress(0)
    setSpiffsError('')
    try {
      await apiSpiffsUpload(spiffsFile, setSpiffsProgress)
      setSpiffsStatus('done')
      toast.success('Interfejs wgrany — urządzenie wymaga restartu')
    } catch (e: any) {
      const msg = e?.response?.data?.error ?? 'Błąd wgrywania interfejsu'
      setSpiffsError(msg)
      setSpiffsStatus('error')
      toast.error(msg)
    }
  }

  const startBothOta = async () => {
    if (!spiffsFile || !otaFile) return
    // Kolejność: najpierw SPIFFS (bez restartu), potem firmware (restart)
    setSpiffsStatus('uploading')
    setSpiffsProgress(0)
    setSpiffsError('')
    try {
      await apiSpiffsUpload(spiffsFile, setSpiffsProgress)
      setSpiffsStatus('done')
    } catch (e: any) {
      const msg = e?.response?.data?.error ?? 'Błąd wgrywania interfejsu'
      setSpiffsError(msg)
      setSpiffsStatus('error')
      toast.error(`Interfejs: ${msg}`)
      return
    }
    setOtaStatus('uploading')
    setOtaProgress(0)
    setOtaError('')
    try {
      await apiOtaUpload(otaFile, setOtaProgress)
      setOtaStatus('rebooting')
      toast.success('Aktualizacja zakończona — urządzenie restartuje się')
    } catch (e: any) {
      const msg = e?.response?.data?.error ?? 'Błąd wgrywania firmware'
      setOtaError(msg)
      setOtaStatus('error')
      toast.error(`Firmware: ${msg}`)
    }
  }

  const doRestart = async () => {
    try {
      await apiRestart()
      setSpiffsStatus('rebooting')
      toast.success('Urządzenie restartuje się')
    } catch {
      setSpiffsStatus('rebooting')
    }
  }

  // --- Backup / Restore ---
  const [importFile, setImportFile]     = useState<File | null>(null)
  const [importPreview, setImportPreview] = useState<any | null>(null)
  const [importError, setImportError]   = useState('')
  const [importing, setImporting]       = useState(false)
  const [exporting, setExporting]       = useState(false)

  const doExport = async () => {
    setExporting(true)
    try {
      const resp = await apiExportSettings()
      const text = await (resp.data as Blob).text()
      const blob = new Blob([text], { type: 'application/json' })
      const url  = URL.createObjectURL(blob)
      const a    = document.createElement('a')
      const date = new Date().toISOString().slice(0, 10)
      a.href     = url
      a.download = `wachciodrop_backup_${date}.json`
      a.click()
      URL.revokeObjectURL(url)
      toast.success('Backup pobrany')
    } catch {
      toast.error('Błąd eksportu')
    }
    setExporting(false)
  }

  const onImportFileChange = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0] ?? null
    setImportFile(file)
    setImportPreview(null)
    setImportError('')
    if (!file) return
    try {
      const text = await file.text()
      const data = JSON.parse(text)
      if (!data.config && !data.schedule && !data.groups) {
        setImportError('Nieprawidłowy format pliku — brak sekcji config/schedule/groups')
        return
      }
      setImportPreview(data)
    } catch {
      setImportError('Błąd parsowania JSON — uszkodzony plik')
    }
  }

  const doImport = async () => {
    if (!importPreview) return
    setImporting(true)
    try {
      await apiImportSettings(importPreview)
      toast.success('Ustawienia przywrócone')
      setImportFile(null)
      setImportPreview(null)
      // Odśwież dane
      apiGetSettings().then(r => setCfg(r.data)).catch(() => {})
      apiGetStatus().then(r => setDevStatus(r.data)).catch(() => {})
    } catch {
      toast.error('Błąd importu')
    }
    setImporting(false)
  }

  return (
    <div className="flex flex-col gap-4">

      {/* Tab bar */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-1 flex gap-1">
        {TABS.map(({ id, icon: Icon, label, tip }) => (
          <Tooltip key={id} text={tip} pos="bottom">
            <button
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
          </Tooltip>
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
                <Tooltip text={devStatus.irrigation_today ? 'Kliknij aby zablokować nawadnianie dziś' : 'Kliknij aby aktywować nawadnianie dziś'}>
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
                </Tooltip>
              </div>
            )}

            <div className="border-t border-gray-100" />

            {/* Ochrona przed mrozem */}
            <div className="flex flex-col gap-4">
              <div className="flex items-center justify-between gap-3">
                <div className="flex items-center gap-3">
                  <div className={`w-9 h-9 rounded-xl flex items-center justify-center shrink-0
                    ${cfg.frost_protection_enabled ? 'bg-blue-100' : 'bg-gray-100'}`}>
                    <Thermometer size={18} className={cfg.frost_protection_enabled ? 'text-blue-600' : 'text-gray-400'} />
                  </div>
                  <div>
                    <p className="text-sm font-medium text-gray-800">Ochrona przed mrozem</p>
                    <p className="text-xs text-gray-400">
                      Wyłącz nawadnianie gdy temperatura DS18B20 spadnie poniżej progu
                    </p>
                  </div>
                </div>
                <Tooltip text={cfg.frost_protection_enabled ? 'Wyłącz ochronę przed mrozem' : 'Włącz ochronę przed mrozem'}>
                  <button
                    onClick={() => setCfg(c => ({ ...c, frost_protection_enabled: !c.frost_protection_enabled }))}
                    className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
                      ${cfg.frost_protection_enabled ? 'bg-blue-500' : 'bg-gray-300'}`}
                  >
                    <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
                      transition-transform ${cfg.frost_protection_enabled ? 'translate-x-6' : ''}`} />
                  </button>
                </Tooltip>
              </div>

              {cfg.frost_protection_enabled && (
                <div className="flex flex-col gap-3 pl-12">
                  <div className="flex items-center gap-3">
                    <label className="text-sm text-gray-600 whitespace-nowrap w-44">
                      Próg temperatury:
                    </label>
                    <div className="flex items-center gap-2">
                      <Tooltip text="Obniż próg o 1°C">
                        <button
                          onClick={() => setCfg(c => ({ ...c, frost_temp_threshold: c.frost_temp_threshold - 1 }))}
                          className="w-8 h-8 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
                            flex items-center justify-center text-gray-600 font-bold text-lg leading-none
                            transition-all select-none"
                        >−</button>
                      </Tooltip>
                      <Tooltip text={`Nawadnianie wyłączy się gdy temp. spadnie poniżej ${cfg.frost_temp_threshold}°C`}>
                        <span className="text-base font-bold text-blue-700 w-16 text-center block cursor-default">
                          {cfg.frost_temp_threshold} °C
                        </span>
                      </Tooltip>
                      <Tooltip text="Podnieś próg o 1°C">
                        <button
                          onClick={() => setCfg(c => ({ ...c, frost_temp_threshold: c.frost_temp_threshold + 1 }))}
                          className="w-8 h-8 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
                            flex items-center justify-center text-gray-600 font-bold text-lg leading-none
                            transition-all select-none"
                        >+</button>
                      </Tooltip>
                    </div>
                  </div>
                  <div className="flex items-center gap-3">
                    <label className="text-sm text-gray-600 whitespace-nowrap w-44">
                      Opóźnienie reaktywacji:
                    </label>
                    <div className="flex items-center gap-2">
                      <Tooltip text="Skróć opóźnienie o 5 minut">
                        <button
                          onClick={() => setCfg(c => ({ ...c, frost_recovery_delay_min: Math.max(1, c.frost_recovery_delay_min - 5) }))}
                          className="w-8 h-8 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
                            flex items-center justify-center text-gray-600 font-bold text-lg leading-none
                            transition-all select-none"
                        >−</button>
                      </Tooltip>
                      <Tooltip text={`Nawadnianie reaktywuje się ${cfg.frost_recovery_delay_min} min po wzroście temp. powyżej progu`}>
                        <span className="text-base font-bold text-blue-700 w-16 text-center block cursor-default">
                          {cfg.frost_recovery_delay_min} min
                        </span>
                      </Tooltip>
                      <Tooltip text="Wydłuż opóźnienie o 5 minut">
                        <button
                          onClick={() => setCfg(c => ({ ...c, frost_recovery_delay_min: c.frost_recovery_delay_min + 5 }))}
                          className="w-8 h-8 rounded-lg bg-gray-100 hover:bg-gray-200 active:scale-90
                            flex items-center justify-center text-gray-600 font-bold text-lg leading-none
                            transition-all select-none"
                        >+</button>
                      </Tooltip>
                    </div>
                    <span className="text-xs text-gray-400">po wzroście powyżej progu</span>
                  </div>
                </div>
              )}
            </div>

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
                <Tooltip text={scheduleMode === 'sections' ? 'Przełącz na tryb grup' : 'Przełącz na tryb sekcji'}>
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
                </Tooltip>
                <span className={`text-xs font-medium ${scheduleMode === 'groups' ? 'text-blue-600' : 'text-gray-400'}`}>
                  Grupy
                </span>
              </div>
            </div>

            <div className="border-t border-gray-100" />

            <Tooltip text="Zapisz wszystkie ustawienia z tej zakładki na urządzeniu">
              <button
                onClick={save}
                disabled={saving}
                className="flex items-center gap-2 bg-green-600 hover:bg-green-700 disabled:opacity-50
                  text-white px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
              >
                <Save size={15} />
                {saving ? 'Zapisywanie…' : 'Zapisz ustawienia'}
              </button>
            </Tooltip>

          </div>
        )}

        {tab === 'token' && (
          <div className="flex flex-col gap-5">

            {/* Połącz przeglądarkę z urządzeniem */}
            <div className="flex flex-col gap-3">
              <p className="text-sm text-gray-600">
                Odczytaj token z wyświetlacza OLED urządzenia i wpisz go poniżej.
                Token zostanie zapisany tylko w tej przeglądarce — nie wymaga połączenia z urządzeniem.
              </p>
              <Field label="Token z OLED">
                <input
                  value={localToken}
                  onChange={e => setLocalToken(e.target.value)}
                  placeholder="wklej token z OLED…"
                  className={inputCls}
                />
              </Field>
              <Tooltip text="Zapisz token w przeglądarce i połącz z urządzeniem">
                <button
                  onClick={() => {
                    if (!localToken.trim()) { toast.error('Token nie może być pusty'); return }
                    setToken(localToken.trim())
                    toast.success('Token zapisany — przeglądarka połączona z urządzeniem')
                  }}
                  className="flex items-center gap-2 bg-green-600 hover:bg-green-700 text-white
                    px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
                >
                  <Save size={15} />
                  Połącz z urządzeniem
                </button>
              </Tooltip>
            </div>

            <div className="border-t border-gray-100" />

            {/* Zmień token na urządzeniu (wymaga autoryzacji) */}
            {(() => {
              const TOKEN_LEN = 10
              const TOKEN_RE = /^[0-9a-f]*$/
              const tokenValid = pass.token.length === TOKEN_LEN && TOKEN_RE.test(pass.token)
              const tokenBad   = pass.token.length > 0 && !tokenValid
              return (
                <div className="flex flex-col gap-3">
                  <p className="text-sm text-gray-600">
                    Aby zmienić token zapisany na urządzeniu, musisz być już połączony (powyżej).
                    Po zmianie tokenu przeglądarka zostanie automatycznie zaktualizowana.
                  </p>
                  <div className="flex items-start gap-2 text-xs text-gray-400 bg-gray-50
                    border border-gray-100 rounded-xl px-3 py-2">
                    <KeyRound size={13} className="shrink-0 mt-0.5" />
                    <span>Dokładnie <strong>10 znaków</strong>, tylko cyfry i małe litery a–f
                      &nbsp;(np.&nbsp;<code className="font-mono bg-gray-100 px-1 rounded">3f8a12c04b</code>)
                    </span>
                  </div>
                  <Field label="Nowy token (pozostaw puste = bez zmian)">
                    <input
                      value={pass.token}
                      maxLength={TOKEN_LEN}
                      onChange={e => {
                        const v = e.target.value.toLowerCase().replace(/[^0-9a-f]/g, '')
                        setPass(p => ({ ...p, token: v }))
                      }}
                      placeholder="np. 3f8a12c04b"
                      className={`${inputCls} font-mono ${tokenBad ? 'border-red-400 focus:ring-red-400' : ''}`}
                    />
                    {tokenBad && (
                      <p className="text-xs text-red-500 mt-1">
                        {pass.token.length < TOKEN_LEN
                          ? `Za krótki — ${TOKEN_LEN - pass.token.length} znaków brakuje`
                          : 'Niedozwolone znaki (tylko 0-9, a-f)'}
                      </p>
                    )}
                    {pass.token.length > 0 && (
                      <p className="text-xs text-gray-400 mt-1">{pass.token.length} / {TOKEN_LEN}</p>
                    )}
                  </Field>
                  <Tooltip text={tokenValid ? 'Wyślij nowy token do urządzenia i zaktualizuj w przeglądarce' : 'Wpisz prawidłowy token (10 znaków hex) aby odblokować'}>
                    <button
                      onClick={async () => {
                        if (!tokenValid) { toast.error('Token musi mieć 10 znaków hex (0-9, a-f)'); return }
                        setSaving(true)
                        try {
                          await apiSaveSettings({ api_token: pass.token } as any)
                          setToken(pass.token)
                          setLocalToken(pass.token)
                          setPass(p => ({ ...p, token: '' }))
                          toast.success('Token zmieniony na urządzeniu i zaktualizowany w przeglądarce')
                        } catch {
                          toast.error('Błąd — sprawdź czy przeglądarka jest połączona z urządzeniem')
                        }
                        setSaving(false)
                      }}
                      disabled={saving || !tokenValid}
                      className="flex items-center gap-2 bg-gray-700 hover:bg-gray-800 text-white
                        px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit
                        disabled:opacity-50 disabled:cursor-not-allowed"
                    >
                      <KeyRound size={15} />
                      {saving ? 'Zapisywanie…' : 'Zmień token na urządzeniu'}
                    </button>
                  </Tooltip>
                </div>
              )
            })()}

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
            <SaveButton saving={saving} onClick={save} tip="Zapisz dane sieci WiFi na urządzeniu" />
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
            <SaveButton saving={saving} onClick={save} tip="Zapisz konfigurację brokera MQTT na urządzeniu" />
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
                    <Tooltip text={devStatus.ignore_php ? 'Włącz skrypt — decyzja z internetu' : 'Wyłącz skrypt — decyzja manualna'}>
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
                    </Tooltip>
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

            <div className="border-t border-gray-100" />

            {/* Graylog */}
            <div>
              <p className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-3">
                Logowanie (Syslog UDP / RFC 3164)
              </p>
              <div className="flex flex-col gap-3">
                <div className="flex items-center justify-between gap-3 py-1">
                  <div>
                    <p className="text-sm font-medium text-gray-800">Wysyłka logów</p>
                    <p className="text-xs text-gray-400">Wysyłaj logi do serwera Graylog przez UDP</p>
                  </div>
                  <Tooltip text={cfg.graylog_enabled ? 'Wyłącz wysyłkę logów do Graylog' : 'Włącz wysyłkę logów do Graylog przez UDP'}>
                    <button
                      onClick={() => setCfg(c => ({ ...c, graylog_enabled: !c.graylog_enabled }))}
                      className={`relative inline-flex w-12 h-6 rounded-full transition-colors shrink-0
                        ${cfg.graylog_enabled ? 'bg-green-500' : 'bg-gray-300'}`}
                    >
                      <span className={`absolute top-1 left-1 w-4 h-4 bg-white rounded-full shadow
                        transition-transform ${cfg.graylog_enabled ? 'translate-x-6' : ''}`} />
                    </button>
                  </Tooltip>
                </div>
                {cfg.graylog_enabled && (
                  <>
                    <Field label="Host Graylog (IP lub nazwa hosta)">
                      <input value={cfg.graylog_host}
                        onChange={e => setCfg({ ...cfg, graylog_host: e.target.value })}
                        placeholder="192.168.1.100"
                        className={inputCls} />
                    </Field>
                    <Field label="Port UDP (domyślnie 12201)">
                      <input type="number" min={1} max={65535} value={cfg.graylog_port}
                        onChange={e => setCfg({ ...cfg, graylog_port: +e.target.value })}
                        className={`${inputCls} w-28`} />
                    </Field>
                    <Field label="Minimalny poziom logów">
                      <select value={cfg.graylog_level}
                        onChange={e => setCfg({ ...cfg, graylog_level: +e.target.value })}
                        className={inputCls}
                      >
                        <option value={3}>ERROR (tylko błędy)</option>
                        <option value={4}>WARN (błędy + ostrzeżenia)</option>
                        <option value={6}>INFO (zalecane)</option>
                        <option value={7}>DEBUG (wszystko)</option>
                      </select>
                    </Field>
                  </>
                )}
              </div>
            </div>

            <SaveButton saving={saving} onClick={save} tip="Zapisz ustawienia usług (NTP, Graylog, PHP) na urządzeniu" />
          </div>
        )}

        {tab === 'ota' && (
          <div className="flex flex-col gap-5">

            {/* Aktualna wersja */}
            {devStatus && (
              <div className="bg-gray-50 rounded-xl p-4">
                <p className="text-xs text-gray-400 mb-1">Aktualna wersja firmware</p>
                <p className="text-lg font-mono font-bold text-gray-800">
                  {devStatus.fw_version ?? '—'}
                </p>
                <p className="text-xs text-gray-400 mt-1">ESP-IDF {devStatus.idf_version ?? '—'}</p>
              </div>
            )}

            {/* Ostrzeżenie */}
            <div className="flex items-start gap-3 p-3 bg-orange-50 border border-orange-100 rounded-xl">
              <AlertTriangle size={18} className="text-orange-500 shrink-0 mt-0.5" />
              <p className="text-sm text-orange-700">
                Upewnij się, że żadne sekcje nie są aktywne przed aktualizacją.
                Wgranie firmware powoduje automatyczny restart.
              </p>
            </div>

            {/* ---- SEKCJA: Firmware ---- */}
            <div className="bg-white rounded-2xl border border-gray-100 shadow-sm p-4 flex flex-col gap-3">
              <h3 className="text-sm font-semibold text-gray-700">Firmware (.bin)</h3>

              {otaStatus !== 'rebooting' && (
                <input
                  type="file"
                  accept=".bin"
                  disabled={otaStatus === 'uploading'}
                  onChange={async e => {
                    const file = e.target.files?.[0] ?? null
                    setOtaFile(file)
                    setOtaStatus('idle')
                    setOtaError('')
                    setOtaFileSha('')
                    if (file) {
                      const buf = new Uint8Array(await file.arrayBuffer())
                      const limit = Math.min(buf.length - 176, 8192)
                      let found = ''
                      for (let i = 0; i < limit; i++) {
                        if (buf[i]===0xA5 && buf[i+1]===0x5A && buf[i+2]===0xCD && buf[i+3]===0xAB) {
                          found = Array.from(buf.slice(i + 144, i + 176))
                            .map(b => b.toString(16).padStart(2, '0')).join('')
                          break
                        }
                      }
                      setOtaFileSha(found)
                    }
                  }}
                  className={inputCls}
                />
              )}

              {otaFileSha && otaStatus !== 'rebooting' && (
                <div>
                  <p className="font-mono text-xs text-gray-500 break-all bg-gray-50 rounded-lg px-2 py-1.5">
                    {otaFileSha}
                  </p>
                  {deviceBuildHash && (
                    <p className={`text-xs mt-1 font-medium ${otaFileSha === deviceBuildHash ? 'text-orange-500' : 'text-green-600'}`}>
                      {otaFileSha === deviceBuildHash
                        ? '⚠ Identyczny z aktualnym firmware'
                        : '✓ Nowa wersja'}
                    </p>
                  )}
                </div>
              )}

              {otaFile && otaStatus === 'idle' && (
                <Tooltip text="Wyślij plik firmware na urządzenie — spowoduje automatyczny restart">
                  <button
                    onClick={startOta}
                    className="flex items-center gap-2 bg-orange-600 hover:bg-orange-700 text-white
                      px-4 py-2 rounded-xl text-sm font-medium transition-colors w-fit"
                  >
                    <Upload size={14} />
                    Wgraj firmware ({(otaFile.size / 1024 / 1024).toFixed(2)} MB)
                  </button>
                </Tooltip>
              )}

              {otaStatus === 'uploading' && (
                <OtaProgress label="Przesyłanie firmware…" pct={otaProgress} />
              )}

              {otaStatus === 'rebooting' && (
                <OtaSuccess msg="Firmware wgrany — urządzenie restartuje się" />
              )}

              {otaStatus === 'error' && <OtaError msg={otaError} />}
            </div>

            {/* ---- SEKCJA: SPIFFS / Interfejs ---- */}
            <div className="bg-white rounded-2xl border border-gray-100 shadow-sm p-4 flex flex-col gap-3">
              <h3 className="text-sm font-semibold text-gray-700">Interfejs (SPIFFS .bin)</h3>

              {spiffsStatus !== 'rebooting' && (
                <input
                  type="file"
                  accept=".bin"
                  disabled={spiffsStatus === 'uploading'}
                  onChange={e => {
                    const file = e.target.files?.[0] ?? null
                    setSpiffsFile(file)
                    setSpiffsStatus('idle')
                    setSpiffsError('')
                  }}
                  className={inputCls}
                />
              )}

              {spiffsFile && spiffsStatus === 'idle' && (
                <Tooltip text="Wyślij plik SPIFFS (interfejs webowy) na urządzenie — wymaga restartu">
                  <button
                    onClick={startSpiffsOta}
                    className="flex items-center gap-2 bg-blue-600 hover:bg-blue-700 text-white
                      px-4 py-2 rounded-xl text-sm font-medium transition-colors w-fit"
                  >
                    <Upload size={14} />
                    Wgraj interfejs ({(spiffsFile.size / 1024 / 1024).toFixed(2)} MB)
                  </button>
                </Tooltip>
              )}

              {spiffsStatus === 'uploading' && (
                <OtaProgress label="Przesyłanie interfejsu…" pct={spiffsProgress} />
              )}

              {spiffsStatus === 'done' && (
                <div className="flex items-center justify-between p-3 bg-green-50 border border-green-100 rounded-xl">
                  <div className="flex items-center gap-2">
                    <CheckCircle size={18} className="text-green-600 shrink-0" />
                    <p className="text-sm font-medium text-green-800">Interfejs wgrany — wymagany restart</p>
                  </div>
                  <Tooltip text="Zrestartuj urządzenie aby zastosować nowy interfejs">
                    <button
                      onClick={doRestart}
                      className="flex items-center gap-1.5 bg-green-600 hover:bg-green-700 text-white
                        px-3 py-1.5 rounded-lg text-xs font-medium transition-colors shrink-0"
                    >
                      <RefreshCw size={13} />
                      Restartuj
                    </button>
                  </Tooltip>
                </div>
              )}

              {spiffsStatus === 'rebooting' && (
                <OtaSuccess msg="Urządzenie restartuje się — odśwież stronę za kilkanaście sekund" />
              )}

              {spiffsStatus === 'error' && <OtaError msg={spiffsError} />}
            </div>

            {/* ---- Wgraj oba jednocześnie ---- */}
            {otaFile && spiffsFile &&
              otaStatus === 'idle' && spiffsStatus === 'idle' && (
              <Tooltip text="Wgraj interfejs (SPIFFS) a następnie firmware — jedno kliknięcie, jeden restart">
                <button
                  onClick={startBothOta}
                  className="flex items-center justify-center gap-2 bg-green-700 hover:bg-green-800
                    text-white px-4 py-3 rounded-xl text-sm font-semibold transition-colors"
                >
                  <Upload size={16} />
                  Wgraj oba (SPIFFS → firmware + restart)
                </button>
              </Tooltip>
            )}

          </div>
        )}

        {tab === 'backup' && (
          <div className="flex flex-col gap-5">

            {/* Export */}
            <div className="bg-white rounded-2xl border border-gray-100 shadow-sm p-4 flex flex-col gap-3">
              <h3 className="text-sm font-semibold text-gray-700">Eksport ustawień</h3>
              <p className="text-xs text-gray-500">
                Pobiera plik JSON zawierający wszystkie ustawienia urządzenia:
                konfigurację WiFi, MQTT, PHP, harmonogram i grupy.
                Hasła są zapisane w pliku — przechowuj go bezpiecznie.
              </p>
              <Tooltip text="Pobierz plik JSON ze wszystkimi ustawieniami urządzenia (przechowuj bezpiecznie)">
                <button
                  onClick={doExport}
                  disabled={exporting}
                  className="flex items-center gap-2 bg-green-700 hover:bg-green-800 disabled:opacity-50
                    text-white px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
                >
                  <Download size={15} />
                  {exporting ? 'Pobieranie…' : 'Pobierz backup (.json)'}
                </button>
              </Tooltip>
            </div>

            {/* Import */}
            <div className="bg-white rounded-2xl border border-gray-100 shadow-sm p-4 flex flex-col gap-3">
              <h3 className="text-sm font-semibold text-gray-700">Import ustawień</h3>
              <p className="text-xs text-gray-500">
                Wczytaj wcześniej pobrany plik backup. Zastąpi wszystkie ustawienia urządzenia.
              </p>

              <input
                type="file"
                accept=".json"
                onChange={onImportFileChange}
                className={inputCls}
              />

              {importError && (
                <div className="flex items-center gap-2 p-3 bg-red-50 border border-red-100 rounded-xl">
                  <XCircle size={16} className="text-red-500 shrink-0" />
                  <p className="text-xs text-red-700">{importError}</p>
                </div>
              )}

              {importPreview && !importError && (
                <div className="flex flex-col gap-2 p-3 bg-blue-50 border border-blue-100 rounded-xl text-xs text-blue-800">
                  <p className="font-semibold">Zawartość pliku:</p>
                  {importPreview.config && (
                    <p>WiFi: <span className="font-mono">{importPreview.config.wifi_ssid || '(brak)'}</span>
                      {importPreview.config.mqtt_uri ? ` · MQTT: ${importPreview.config.mqtt_uri}` : ''}</p>
                  )}
                  {importPreview.schedule && (
                    <p>Harmonogram: {importPreview.schedule.filter((e: any) => e.enabled).length} aktywnych wpisów
                      z {importPreview.schedule.length}</p>
                  )}
                  {importPreview.groups && (
                    <p>Grupy: {importPreview.groups.filter((g: any) => g.section_mask > 0).length} skonfigurowanych
                      z {importPreview.groups.length}</p>
                  )}
                </div>
              )}

              {importPreview && !importError && (
                <div className="flex items-start gap-3 p-3 bg-orange-50 border border-orange-100 rounded-xl">
                  <AlertTriangle size={16} className="text-orange-500 shrink-0 mt-0.5" />
                  <p className="text-xs text-orange-700">
                    Import nadpisze wszystkie aktualne ustawienia urządzenia. Tej operacji nie można cofnąć.
                  </p>
                </div>
              )}

              {importPreview && !importError && (
                <Tooltip text="Nadpisz wszystkie ustawienia urządzenia danymi z pliku (nieodwracalne)">
                  <button
                    onClick={doImport}
                    disabled={importing}
                    className="flex items-center gap-2 bg-orange-600 hover:bg-orange-700 disabled:opacity-50
                      text-white px-4 py-2.5 rounded-xl text-sm font-medium transition-colors w-fit"
                  >
                    <ArchiveRestore size={15} />
                    {importing ? 'Przywracanie…' : 'Przywróć ustawienia'}
                  </button>
                </Tooltip>
              )}
            </div>

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
              Synchronizacja ustawia czas RTC na podstawie czasu lokalnego przeglądarki lub serwera NTP.
            </p>
            <div className="flex gap-2 flex-wrap">
              <Tooltip text="Ustaw czas RTC na podstawie aktualnego czasu tej przeglądarki">
                <button
                  onClick={syncTime}
                  className="flex items-center gap-2 bg-gray-700 hover:bg-gray-800 text-white
                    px-4 py-2.5 rounded-xl text-sm font-medium transition-colors"
                >
                  <RefreshCw size={15} />
                  Synchronizuj z przeglądarką
                </button>
              </Tooltip>
              <Tooltip text="Pobierz czas z serwera NTP przez WiFi i ustaw RTC">
                <button
                  onClick={async () => {
                    try {
                      await apiSyncNtp()
                      toast.success('Synchronizacja NTP zakończona')
                      apiGetTime().then(r => setRtcTime(r.data.time.replace('T', ' '))).catch(() => {})
                    } catch (e: any) {
                      const msg = e?.response?.data?.error
                      toast.error(msg === 'no WiFi connection' ? 'Brak połączenia WiFi' : 'Błąd synchronizacji NTP')
                    }
                  }}
                  className="flex items-center gap-2 bg-blue-600 hover:bg-blue-700 text-white
                    px-4 py-2.5 rounded-xl text-sm font-medium transition-colors"
                >
                  <Antenna size={15} />
                  Synchronizuj z NTP
                </button>
              </Tooltip>
            </div>
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

function SaveButton({ saving, onClick, tip }: { saving: boolean; onClick: () => void; tip?: string }) {
  const btn = (
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
  if (!tip) return btn
  return <Tooltip text={tip}>{btn}</Tooltip>
}
