import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom'
import { Toaster } from 'react-hot-toast'
import { LayoutDashboard, CalendarDays, Layers, Settings, Info } from 'lucide-react'
import Dashboard from './pages/Dashboard'
import Schedule  from './pages/Schedule'
import Groups    from './pages/Groups'
import SettingsPage from './pages/Settings'
import About    from './pages/About'

const NAV_ITEMS = [
  { to: '/',         icon: LayoutDashboard, label: 'Dashboard' },
  { to: '/schedule', icon: CalendarDays,    label: 'Harmonogram' },
  { to: '/groups',   icon: Layers,          label: 'Grupy' },
  { to: '/settings', icon: Settings,        label: 'Ustawienia' },
  { to: '/about',    icon: Info,            label: 'O aplikacji' },
]

export default function App() {
  return (
    <BrowserRouter>
      <div className="min-h-screen bg-gray-50 flex flex-col">

        {/* Header */}
        <header className="bg-green-800 text-white px-4 py-3 flex items-center gap-3 shadow-md">
          <span className="text-2xl">💧</span>
          <div>
            <h1 className="text-lg font-bold leading-none">WachcioDrop</h1>
            <p className="text-green-300 text-xs">Sterownik nawadniania</p>
          </div>
        </header>

        {/* Tab navigation */}
        <nav className="bg-white border-b border-gray-200 shadow-sm">
          <div className="flex max-w-3xl mx-auto">
            {NAV_ITEMS.map(({ to, icon: Icon, label }) => (
              <NavLink
                key={to}
                to={to}
                end={to === '/'}
                className={({ isActive }) =>
                  `flex-1 flex flex-col items-center gap-0.5 py-2.5 text-xs font-medium transition-colors ` +
                  (isActive
                    ? 'text-green-700 border-b-2 border-green-700'
                    : 'text-gray-500 hover:text-green-600')
                }
              >
                <Icon size={20} />
                <span className="hidden sm:block">{label}</span>
              </NavLink>
            ))}
          </div>
        </nav>

        {/* Content */}
        <main className="flex-1 max-w-3xl mx-auto w-full px-4 py-5">
          <Routes>
            <Route path="/"         element={<Dashboard />} />
            <Route path="/schedule" element={<Schedule />} />
            <Route path="/groups"   element={<Groups />} />
            <Route path="/settings" element={<SettingsPage />} />
            <Route path="/about"    element={<About />} />
          </Routes>
        </main>

      </div>
      <Toaster
        position="bottom-center"
        toastOptions={{
          style: { borderRadius: '10px', background: '#1a1a1a', color: '#fff' },
          success: { iconTheme: { primary: '#22c55e', secondary: '#fff' } },
          error:   { iconTheme: { primary: '#ef4444', secondary: '#fff' } },
        }}
      />
    </BrowserRouter>
  )
}
