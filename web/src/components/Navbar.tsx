import React, { useState, useEffect } from 'react';
import { BookOpen, ChevronRight, Download, HelpCircle, Menu, Terminal, X } from 'lucide-react';

interface NavbarProps {
  basePath?: string;
  active?: 'home' | 'faq';
}

const GithubIcon = ({ className }: { className?: string }) => (
  <svg className={className} fill="currentColor" viewBox="0 0 24 24">
    <path fillRule="evenodd" clipRule="evenodd" d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.53 1.032 1.53 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z" />
  </svg>
);

export const Navbar: React.FC<NavbarProps> = ({ basePath = '/gotv/', active = 'home' }) => {
  const [isMenuOpen, setIsMenuOpen] = useState(false);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setIsMenuOpen(false);
    };
    const handleResize = () => {
      if (window.innerWidth >= 768) {
        setIsMenuOpen(false);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('resize', handleResize);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('resize', handleResize);
    };
  }, []);

  return (
    <header className="sticky top-0 z-50 bg-white/95 backdrop-blur-md border-b border-stone-200 transition-colors">
      <div className="max-w-5xl mx-auto px-4 h-16 flex items-center justify-between gap-3">
        {/* Brand Logo & Version */}
        <a href={basePath} className="flex items-center gap-2 text-stone-900 font-extrabold text-lg sm:text-xl group shrink-0 whitespace-nowrap">
          <span className="w-8 h-8 rounded-lg bg-red-700 text-white flex items-center justify-center font-black text-sm group-hover:scale-105 transition shadow-sm shrink-0">
            T
          </span>
          <span>GoTV</span>
          <span className="text-[11px] sm:text-xs px-1.5 sm:px-2 py-0.5 rounded-full bg-red-100 text-red-800 font-semibold shrink-0">
            v0.9.9
          </span>
        </a>

        {/* Desktop Navigation (>= md) */}
        <nav className="hidden md:flex items-center gap-6 text-sm font-medium text-stone-600">
          <a href={`${basePath}#tinh-nang`} className="hover:text-red-700 transition inline-flex items-center gap-1.5 whitespace-nowrap">
            <BookOpen className="w-4 h-4" /> Tính năng
          </a>
          <a href={`${basePath}#cai-dat`} className="hover:text-red-700 transition inline-flex items-center gap-1.5 whitespace-nowrap">
            <Terminal className="w-4 h-4" /> Cài đặt
          </a>
          <a href={`${basePath}#tai-ve`} className="hover:text-red-700 transition inline-flex items-center gap-1.5 whitespace-nowrap">
            <Download className="w-4 h-4" /> Tải về
          </a>
          <a 
            href={`${basePath}faq/`} 
            className={`inline-flex items-center gap-1.5 transition whitespace-nowrap ${active === 'faq' ? 'text-red-700 font-bold' : 'hover:text-red-700'}`}
          >
            <HelpCircle className="w-4 h-4" /> FAQ
          </a>
          <a 
            href="https://github.com/isthaison/gotv" 
            target="_blank" 
            rel="noopener noreferrer"
            className="flex items-center gap-1.5 bg-stone-900 text-white px-3.5 py-1.5 rounded-xl hover:bg-stone-800 transition shadow-sm text-xs font-semibold whitespace-nowrap shrink-0"
          >
            <GithubIcon className="w-3.5 h-3.5" />
            <span>GitHub</span>
          </a>
        </nav>

        {/* Mobile Quick Action & Hamburger Toggle (< md) */}
        <div className="flex md:hidden items-center gap-2 shrink-0">
          <a
            href={active === 'home' ? '#tai-ve' : `${basePath}#tai-ve`}
            className="inline-flex items-center gap-1.5 bg-red-700 hover:bg-red-800 text-white px-3 py-1.5 rounded-xl text-xs font-bold transition shadow-sm whitespace-nowrap active:scale-95 shrink-0"
          >
            <Download className="w-3.5 h-3.5" />
            <span>Tải về</span>
          </a>

          <button
            type="button"
            onClick={() => setIsMenuOpen(!isMenuOpen)}
            aria-label={isMenuOpen ? 'Đóng menu điều hướng' : 'Mở menu điều hướng'}
            aria-expanded={isMenuOpen}
            className="w-9 h-9 rounded-xl bg-stone-100 hover:bg-stone-200 active:scale-95 text-stone-700 flex items-center justify-center transition focus:outline-none focus:ring-2 focus:ring-red-600/30 shrink-0"
          >
            {isMenuOpen ? <X className="w-5 h-5 text-stone-900" /> : <Menu className="w-5 h-5 text-stone-700" />}
          </button>
        </div>
      </div>

      {/* Mobile Drawer Navigation Menu */}
      {isMenuOpen && (
        <div className="md:hidden border-t border-stone-200 bg-white/98 backdrop-blur-lg px-4 py-3 shadow-xl animate-in slide-in-from-top-2 duration-200">
          <nav className="flex flex-col space-y-1">
            <a
              href={`${basePath}#tinh-nang`}
              onClick={() => setIsMenuOpen(false)}
              className="flex items-center justify-between px-3 py-2.5 rounded-xl text-stone-700 hover:text-red-700 hover:bg-red-50/70 active:bg-red-100 transition font-medium text-sm"
            >
              <span className="flex items-center gap-2.5">
                <BookOpen className="w-4 h-4 text-stone-500" />
                <span>Tính năng nổi bật</span>
              </span>
              <ChevronRight className="w-4 h-4 text-stone-400" />
            </a>

            <a
              href={`${basePath}#cai-dat`}
              onClick={() => setIsMenuOpen(false)}
              className="flex items-center justify-between px-3 py-2.5 rounded-xl text-stone-700 hover:text-red-700 hover:bg-red-50/70 active:bg-red-100 transition font-medium text-sm"
            >
              <span className="flex items-center gap-2.5">
                <Terminal className="w-4 h-4 text-stone-500" />
                <span>Cài đặt & Hướng dẫn</span>
              </span>
              <ChevronRight className="w-4 h-4 text-stone-400" />
            </a>

            <a
              href={`${basePath}#tai-ve`}
              onClick={() => setIsMenuOpen(false)}
              className="flex items-center justify-between px-3 py-2.5 rounded-xl text-stone-700 hover:text-red-700 hover:bg-red-50/70 active:bg-red-100 transition font-medium text-sm"
            >
              <span className="flex items-center gap-2.5">
                <Download className="w-4 h-4 text-red-600" />
                <span className="font-semibold text-stone-900">Tải về các bản phát hành</span>
              </span>
              <span className="text-xs px-2 py-0.5 rounded-md bg-red-100 text-red-800 font-bold">Mới</span>
            </a>

            <a
              href={`${basePath}faq/`}
              onClick={() => setIsMenuOpen(false)}
              className={`flex items-center justify-between px-3 py-2.5 rounded-xl transition font-medium text-sm ${
                active === 'faq'
                  ? 'text-red-700 bg-red-50/80 font-bold'
                  : 'text-stone-700 hover:text-red-700 hover:bg-red-50/70 active:bg-red-100'
              }`}
            >
              <span className="flex items-center gap-2.5">
                <HelpCircle className="w-4 h-4 text-stone-500" />
                <span>Câu hỏi thường gặp (FAQ)</span>
              </span>
              <ChevronRight className="w-4 h-4 text-stone-400" />
            </a>

            <div className="pt-2 mt-1 border-t border-stone-100 flex items-center justify-between gap-2">
              <a
                href="https://github.com/isthaison/gotv"
                target="_blank"
                rel="noopener noreferrer"
                className="flex-1 flex items-center justify-center gap-2 bg-stone-900 hover:bg-stone-800 text-white py-2.5 px-3 rounded-xl text-xs font-semibold shadow-sm transition active:scale-98"
              >
                <GithubIcon className="w-4 h-4" />
                <span>Star GitHub</span>
              </a>
              <a
                href="https://github.com/isthaison/gotv/releases/latest"
                target="_blank"
                rel="noopener noreferrer"
                className="flex-1 flex items-center justify-center gap-2 bg-red-700 hover:bg-red-800 text-white py-2.5 px-3 rounded-xl text-xs font-semibold shadow-sm transition active:scale-98"
              >
                <Download className="w-4 h-4" />
                <span>Tải v0.9.9</span>
              </a>
            </div>
          </nav>
        </div>
      )}
    </header>
  );
};
