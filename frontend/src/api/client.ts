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
export interface SystemStatus {
  uptime_sec: number
  ip: string
  rssi: number
  sections_active: number
  master_active: boolean
  irrigation_today: boolean
  time: string
}

export interface SectionState {
  id: number
  active: boolean
}

export interface SectionsResponse {
  master: boolean
  sections: SectionState[]
}

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
export const apiActivateGroup = (id: number, duration?: number) =>
  api.post(`/api/groups/${id}/activate`, { duration: duration ?? 0 })

export const apiGetSettings  = ()                => api.get<Settings>('/api/settings')
export const apiSaveSettings = (s: Partial<Settings> & { wifi_pass?: string; mqtt_pass?: string; api_token?: string }) =>
  api.post('/api/settings', s)

export const apiGetTime      = ()                => api.get('/api/time')
export const apiSetTime      = (unix: number)    => api.post('/api/time', { unix })
