import { useEffect, useState } from 'react'
import { apiGetSettings, apiSaveSettings, apiGetTime, apiSetTime, Settings, setToken } from '../api/client'

const CARD: React.CSSProperties = {
  background: '#fff', borderRadius: '8px', padding: '20px',
  boxShadow: '0 1px 4px rgba(0,0,0,.12)', marginBottom: '16px',
}
const LBL: React.CSSProperties = {
  display: 'block', marginBottom: '12px', fontSize: '13px', color: '#444',
}
const INP: React.CSSProperties = {
  display: 'block', width: '100%', padding: '8px', marginTop: '4px',
  border: '1px solid #ccc', borderRadius: '4px', fontSize: '14px',
}
const BTN: React.CSSProperties = {
  padding: '10px 24px', background: '#2a7c2a', color: '#fff',
  border: 'none', borderRadius: '4px', cursor: 'pointer', fontSize: '14px',
}

export default function Settings() {
  const [cfg,     setCfg]     = useState<Settings>({
    wifi_ssid: '', mqtt_uri: '', mqtt_user: '', php_url: '',
    ntp_server: 'pool.ntp.org', tz_offset: 1,
  })
  const [pass,    setPass]    = useState({ wifi: '', mqtt: '', token: '' })
  const [time,    setTime]    = useState('')
  const [saving,  setSaving]  = useState(false)
  const [msg,     setMsg]     = useState('')
  const [localToken, setLocalToken] = useState(
    localStorage.getItem('api_token') ?? ''
  )

  useEffect(() => {
    apiGetSettings().then(r => setCfg(r.data)).catch(() => {})
    apiGetTime().then(r => setTime(r.data.time)).catch(() => {})
  }, [])

  const save = async () => {
    setSaving(true)
    setMsg('')
    try {
      await apiSaveSettings({
        ...cfg,
        wifi_pass:  pass.wifi  || undefined,
        mqtt_pass:  pass.mqtt  || undefined,
        api_token:  pass.token || undefined,
      } as any)
      setMsg('Ustawienia zapisane.')
    } catch {
      setMsg('Błąd zapisu.')
    }
    setSaving(false)
  }

  const syncTime = async () => {
    await apiSetTime(Math.floor(Date.now() / 1000))
    setMsg('Czas zsynchronizowany z przeglądarką.')
  }

  const saveToken = () => {
    setToken(localToken)
    setMsg('Token zapisany lokalnie.')
  }

  return (
    <div>
      <h2 style={{ margin: '0 0 16px', color: '#1f5e1f' }}>Ustawienia</h2>

      {msg && (
        <div style={{ background: '#e8f4e8', padding: '10px', borderRadius: '6px',
                      color: '#1f5e1f', marginBottom: '16px' }}>{msg}</div>
      )}

      {/* Token API (lokalny) */}
      <div style={CARD}>
        <h3 style={{ margin: '0 0 12px', fontSize: '15px' }}>Token API (lokalny)</h3>
        <label style={LBL}>
          Token (przechowywany w przeglądarce)
          <input style={INP} value={localToken}
                 onChange={e => setLocalToken(e.target.value)}
                 placeholder="wklej token z OLED" />
        </label>
        <button style={BTN} onClick={saveToken}>Zapisz token</button>
      </div>

      {/* WiFi */}
      <div style={CARD}>
        <h3 style={{ margin: '0 0 12px', fontSize: '15px' }}>WiFi</h3>
        <label style={LBL}>
          SSID
          <input style={INP} value={cfg.wifi_ssid}
                 onChange={e => setCfg({ ...cfg, wifi_ssid: e.target.value })} />
        </label>
        <label style={LBL}>
          Hasło (pozostaw puste aby nie zmieniać)
          <input style={INP} type="password" value={pass.wifi}
                 onChange={e => setPass({ ...pass, wifi: e.target.value })} />
        </label>
      </div>

      {/* MQTT */}
      <div style={CARD}>
        <h3 style={{ margin: '0 0 12px', fontSize: '15px' }}>MQTT</h3>
        <label style={LBL}>
          URI (np. mqtt://192.168.1.10 lub mqtts://host:8883)
          <input style={INP} value={cfg.mqtt_uri}
                 onChange={e => setCfg({ ...cfg, mqtt_uri: e.target.value })} />
        </label>
        <label style={LBL}>
          Użytkownik
          <input style={INP} value={cfg.mqtt_user}
                 onChange={e => setCfg({ ...cfg, mqtt_user: e.target.value })} />
        </label>
        <label style={LBL}>
          Hasło
          <input style={INP} type="password" value={pass.mqtt}
                 onChange={e => setPass({ ...pass, mqtt: e.target.value })} />
        </label>
      </div>

      {/* PHP + NTP */}
      <div style={CARD}>
        <h3 style={{ margin: '0 0 12px', fontSize: '15px' }}>Usługi zewnętrzne</h3>
        <label style={LBL}>
          PHP URL (daily check)
          <input style={INP} value={cfg.php_url}
                 onChange={e => setCfg({ ...cfg, php_url: e.target.value })}
                 placeholder="http://example.com/irrigation_check.php" />
        </label>
        <label style={LBL}>
          Serwer NTP
          <input style={INP} value={cfg.ntp_server}
                 onChange={e => setCfg({ ...cfg, ntp_server: e.target.value })} />
        </label>
        <label style={LBL}>
          Strefa czasowa (offset UTC)
          <input style={{ ...INP, width: '80px' }} type="number" min={-12} max={14}
                 value={cfg.tz_offset}
                 onChange={e => setCfg({ ...cfg, tz_offset: +e.target.value })} />
        </label>
      </div>

      {/* Czas RTC */}
      <div style={CARD}>
        <h3 style={{ margin: '0 0 12px', fontSize: '15px' }}>Czas RTC</h3>
        <p style={{ color: '#555', fontSize: '13px', marginBottom: '8px' }}>
          Aktualny czas ESP32: <strong>{time}</strong>
        </p>
        <button style={{ ...BTN, background: '#555' }} onClick={syncTime}>
          Synchronizuj z przeglądarką
        </button>
      </div>

      <button style={BTN} onClick={save} disabled={saving}>
        {saving ? 'Zapisywanie...' : 'Zapisz ustawienia'}
      </button>
    </div>
  )
}
