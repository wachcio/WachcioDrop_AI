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
        px-2.5 py-1.5 bg-gray-800/90 text-white text-xs rounded-lg whitespace-nowrap
        opacity-0 group-hover/tip:opacity-100 transition-opacity duration-150 z-50 shadow-lg
        ${isTop ? 'bottom-full mb-2' : 'top-full mt-2'}`}>
        {text}
        {isTop
          ? <div className="absolute top-full left-1/2 -translate-x-1/2 border-[5px] border-transparent border-t-gray-800/90" />
          : <div className="absolute bottom-full left-1/2 -translate-x-1/2 border-[5px] border-transparent border-b-gray-800/90" />
        }
      </div>
    </div>
  )
}
