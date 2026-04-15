import { useEffect, useState } from 'react'
import { Cpu, Layers, CalendarDays, Clock, MemoryStick, Droplets, Info, Code2, User } from 'lucide-react'
import { api } from '../api/client'

interface DeviceInfo {
  fw_version: string
  author: string
  idf_version: string
  app_name: string
  build_date: string
  build_time: string
  flash_mb: number
  psram_kb: number
  heap_free: number
  sections: number
  groups_max: number
  schedule_max: number
  daily_check_hour: number
  daily_check_minute: number
}

const MONTHS: Record<string, string> = {
  Jan: 'sty', Feb: 'lut', Mar: 'mar', Apr: 'kwi',
  May: 'maj', Jun: 'cze', Jul: 'lip', Aug: 'sie',
  Sep: 'wrz', Oct: 'paź', Nov: 'lis', Dec: 'gru',
}

function formatBuildDate(date: string, time: string): string {
  // date = "Apr 14 2026", time = "15:04:32"
  const [mon, day, year] = date.split(' ')
  const pl = MONTHS[mon] ?? mon.toLowerCase()
  return `${day} ${pl} ${year}, ${time}`
}

function Row({ icon: Icon, label, value }: { icon: React.ElementType; label: string; value: string | number }) {
  return (
    <div className="flex items-center gap-3 py-3 border-b border-gray-50 last:border-0">
      <div className="w-8 h-8 rounded-xl bg-green-50 flex items-center justify-center shrink-0">
        <Icon size={16} className="text-green-600" />
      </div>
      <span className="text-sm text-gray-500 flex-1">{label}</span>
      <span className="text-sm font-semibold text-gray-800">{value}</span>
    </div>
  )
}

export default function About() {
  const [info, setInfo] = useState<DeviceInfo | null>(null)
  const [error, setError] = useState(false)

  useEffect(() => {
    api.get<DeviceInfo>('/api/info')
      .then(r => setInfo(r.data))
      .catch(() => setError(true))
  }, [])

  return (
    <div className="flex flex-col gap-4 max-w-lg mx-auto">

      {/* Hero */}
      <div className="bg-green-800 rounded-2xl p-6 text-white flex flex-col items-center gap-2 text-center">
        <img src="/logo.png" alt="WachcioDrop" className="w-20 h-20 object-contain" />
        <h1 className="text-2xl font-bold">WachcioDrop</h1>
        <p className="text-green-300 text-sm">Zaawansowany sterownik nawadniania ogrodu</p>
        {info && (
          <span className="mt-1 px-3 py-1 bg-white/15 rounded-full text-xs font-medium">
            v{info.fw_version}
          </span>
        )}
      </div>

      {/* Autor */}
      <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
        <h2 className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-1">Autor</h2>
        <div className="flex items-center gap-3 mt-2">
          <div className="w-10 h-10 rounded-xl bg-green-100 flex items-center justify-center shrink-0">
            <User size={20} className="text-green-700" />
          </div>
          <div>
            <p className="font-semibold text-gray-800">Wachcio</p>
            <p className="text-xs text-gray-400">wspomagany przez Claude (Anthropic)</p>
            <a href="mailto:wachciodrop@wachcio.pl" className="text-xs text-green-600 hover:underline">wachciodrop@wachcio.pl</a>
          </div>
        </div>
      </div>

      {/* Firmware */}
      {error ? (
        <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4 text-center text-gray-400 text-sm">
          Brak połączenia z WachcioDrop
        </div>
      ) : info ? (
        <>
          <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
            <h2 className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-1">Oprogramowanie</h2>
            <Row icon={Info}         label="Wersja firmware"  value={`v${info.fw_version}`} />
            <Row icon={Code2}        label="ESP-IDF"          value={info.idf_version} />
            <Row icon={CalendarDays} label="Data kompilacji"  value={formatBuildDate(info.build_date, info.build_time)} />
          </div>

          <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
            <h2 className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-1">Sprzęt</h2>
            <Row icon={Cpu}         label="Procesor"         value="ESP32-S3 N16R8" />
            <Row icon={MemoryStick} label="Flash"            value={`${info.flash_mb} MB`} />
            <Row icon={MemoryStick} label="PSRAM"            value={`${info.psram_kb} KB`} />
            <Row icon={Clock}       label="Wolna sterta"     value={`${(info.heap_free / 1024).toFixed(0)} KB`} />
          </div>

          <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-4">
            <h2 className="text-xs font-semibold text-gray-400 uppercase tracking-wide mb-1">Konfiguracja</h2>
            <Row icon={Droplets}    label="Liczba sekcji"    value={info.sections} />
            <Row icon={Layers}      label="Maks. grup"       value={info.groups_max} />
            <Row icon={CalendarDays}label="Wpisów harmonogramu" value={info.schedule_max} />
          </div>
        </>
      ) : (
        <div className="bg-white rounded-2xl shadow-sm border border-gray-100 p-6 text-center text-gray-400 text-sm">
          Ładowanie…
        </div>
      )}
    </div>
  )
}
