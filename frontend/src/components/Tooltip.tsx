export function Tooltip({
  text, children, className = '', pos = 'top',
}: {
  text: string
  children: React.ReactNode
  className?: string
  pos?: 'top' | 'bottom'
}) {
  const isTop = pos !== 'bottom'
  return (
    <div className={`relative group/tip ${className}`}>
      {children}
      <div className={`pointer-events-none absolute left-1/2 -translate-x-1/2
        px-2.5 py-1.5 bg-green-700 text-white text-xs rounded-lg whitespace-nowrap z-50
        shadow-lg shadow-green-900/25 font-medium tracking-wide
        opacity-0 scale-95 group-hover/tip:opacity-100 group-hover/tip:scale-100
        transition-all duration-200 ease-out
        ${isTop
          ? 'bottom-full mb-2 origin-bottom'
          : 'top-full mt-2 origin-top'
        }`}>
        {text}
        {isTop
          ? <div className="absolute top-full left-1/2 -translate-x-1/2 border-[5px] border-transparent border-t-green-700" />
          : <div className="absolute bottom-full left-1/2 -translate-x-1/2 border-[5px] border-transparent border-b-green-700" />
        }
      </div>
    </div>
  )
}
