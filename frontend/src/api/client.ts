import axios from 'axios'

const BASE_URL = window.location.origin

// Token przechowywany w localStorage
export function getToken(): string {
  return localStorage.getItem('api_token') ?? ''
}
export function setToken(token: string): void {
  localStorage.setItem('api_token', token)
}

export const api = axios.create({
  baseURL: BASE_URL,
  timeout: 10000,
})

// Dołącz Bearer token do każdego żądania
api.interceptors.request.use((config) => {
  const token = getToken()
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

// Typy danych
export interface SectionState {
  id: number
  active: boolean
  remaining_sec: number | null
  started_at: string | null
}

export interface SectionsResponse {
  master: boolean
  sections: SectionState[]
}

export interface GroupStatus {
  id: number
  name: string
  section_mask: number
  active: boolean
  remaining_sec: number | null
}

export interface SystemStatus {
  uptime_sec: number
  ip: string
  rssi: number
  sections_active: number
  master_active: boolean
  irrigation_today: boolean
  ignore_php: boolean
  php_url_set: boolean
  time: string
  fw_version: string
  idf_version: string
  temperature: number | null
  sections: SectionState[]
  groups: GroupStatus[]
}

export const apiSetIrrigation = (irrigation_today?: boolean, ignore_php?: boolean) =>
  api.post('/api/irrigation', {
    ...(irrigation_today !== undefined && { irrigation_today }),
    ...(ignore_php      !== undefined && { ignore_php }),
  })

export interface ScheduleEntry {
  id: number
  enabled: boolean
  days_mask: number   // bit0=Pon...bit6=Nie
  hour: number
  minute: number
  duration_sec: number
  section_mask: number
  group_mask: number
}

export interface Group {
  id: number
  name: string
  section_mask: number
}

export interface Settings {
  wifi_ssid: string
  mqtt_uri: string
  mqtt_user: string
  php_url: string
  ntp_server: string
  tz_offset: number
  graylog_host: string
  graylog_port: number
  graylog_enabled: boolean
  graylog_level: number
}

// API calls
export const apiGetStatus    = ()            => api.get<SystemStatus>('/api/status')
export const apiGetSections  = ()            => api.get<SectionsResponse>('/api/sections')
export const apiSectionOn    = (id: number, duration?: number) =>
  api.post(`/api/sections/${id}/on`, { duration: duration ?? 0 })
export const apiSectionOff   = (id: number) => api.post(`/api/sections/${id}/off`)
export const apiAllOff       = ()            => api.post('/api/sections/all/off')

export const apiGetSchedule  = ()                          => api.get<ScheduleEntry[]>('/api/schedule')
export const apiSetSchedule  = (id: number, e: Partial<ScheduleEntry>) =>
  api.put(`/api/schedule/${id}`, e)
export const apiDelSchedule  = (id: number)                => api.delete(`/api/schedule/${id}`)

export const apiGetGroups    = ()                          => api.get<Group[]>('/api/groups')
export const apiSetGroup     = (id: number, g: Partial<Group>) =>
  api.put(`/api/groups/${id}`, g)
export const apiDeleteGroup  = (id: number)                => api.delete(`/api/groups/${id}`)
export const apiActivateGroup = (id: number, duration?: number) =>
  api.post(`/api/groups/${id}/activate`, { duration: duration ?? 0 })

export const apiGetSettings  = ()                => api.get<Settings>('/api/settings')
export const apiSaveSettings = (s: Partial<Settings> & { wifi_pass?: string; mqtt_pass?: string; api_token?: string }) =>
  api.post('/api/settings', s)

export const apiGetTime      = ()                => api.get('/api/time')
export const apiSetTime      = (unix: number)    => api.post('/api/time', { unix })
export const apiSetDateTime  = (datetime: string) => api.post('/api/time', { datetime })
export const apiSyncNtp      = ()                => api.post('/api/time/sntp')

export interface LogEntry {
  ts: number
  level: number  // 3=ERROR 4=WARN 6=INFO 7=DEBUG
  tag: string
  msg: string
}

export interface LogsResponse {
  total: number
  offset: number
  count: number
  entries: LogEntry[]
}

export const apiGetLogs   = (offset = 0, limit = 100) =>
  api.get<LogsResponse>(`/api/logs?offset=${offset}&limit=${limit}`)
export const apiClearLogs = () => api.delete('/api/logs')

export const apiOtaUpload = (
  file: File,
  onProgress: (pct: number) => void
) => api.post('/api/ota', file, {
  headers: { 'Content-Type': 'application/octet-stream' },
  timeout: 120000,
  onUploadProgress: (e) => {
    if (e.total) onProgress(Math.round((e.loaded / e.total) * 100))
  },
})

export const apiSpiffsUpload = (
  file: File,
  onProgress: (pct: number) => void
) => api.post('/api/ota/spiffs', file, {
  headers: { 'Content-Type': 'application/octet-stream' },
  timeout: 120000,
  onUploadProgress: (e) => {
    if (e.total) onProgress(Math.round((e.loaded / e.total) * 100))
  },
})

export const apiRestart = () => api.post('/api/restart')

export const apiExportSettings = () =>
  api.get('/api/settings/export', { responseType: 'blob' })

export const apiImportSettings = (data: object) =>
  api.put('/api/settings/import', data)
