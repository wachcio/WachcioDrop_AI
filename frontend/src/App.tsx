import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom'
import Dashboard  from './pages/Dashboard'
import Schedule   from './pages/Schedule'
import Groups     from './pages/Groups'
import Settings   from './pages/Settings'

const NAV_STYLE: React.CSSProperties = {
  display: 'flex', gap: '2px', background: '#1f5e1f', padding: '8px 12px',
}
const LINK_STYLE: React.CSSProperties = {
  color: '#cce5cc', textDecoration: 'none', padding: '6px 14px',
  borderRadius: '4px', fontSize: '14px',
}
const ACTIVE_STYLE: React.CSSProperties = {
  ...LINK_STYLE, background: '#2a7c2a', color: '#fff', fontWeight: 'bold',
}

export default function App() {
  return (
    <BrowserRouter>
      <nav style={NAV_STYLE}>
        <NavLink to="/"         style={({ isActive }) => isActive ? ACTIVE_STYLE : LINK_STYLE}>Dashboard</NavLink>
        <NavLink to="/schedule" style={({ isActive }) => isActive ? ACTIVE_STYLE : LINK_STYLE}>Harmonogram</NavLink>
        <NavLink to="/groups"   style={({ isActive }) => isActive ? ACTIVE_STYLE : LINK_STYLE}>Grupy</NavLink>
        <NavLink to="/settings" style={({ isActive }) => isActive ? ACTIVE_STYLE : LINK_STYLE}>Ustawienia</NavLink>
      </nav>
      <main style={{ maxWidth: '900px', margin: '0 auto', padding: '16px' }}>
        <Routes>
          <Route path="/"         element={<Dashboard />} />
          <Route path="/schedule" element={<Schedule />} />
          <Route path="/groups"   element={<Groups />} />
          <Route path="/settings" element={<Settings />} />
        </Routes>
      </main>
    </BrowserRouter>
  )
}
