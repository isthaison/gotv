import React, { useState, useEffect } from 'react';
import { ArrowUp, Download, HelpCircle, Search, Sparkles } from 'lucide-react';

interface FloatingActionButtonProps {
  basePath?: string;
}

export const FloatingActionButton: React.FC<FloatingActionButtonProps> = ({ basePath = '/gotv/' }) => {
  const [isVisible, setIsVisible] = useState(false);
  const [isOpen, setIsOpen] = useState(false);

  useEffect(() => {
    const handleScroll = () => {
      // Hiện FAB khi người dùng cuộn quá 250px
      if (window.scrollY > 250) {
        setIsVisible(true);
      } else {
        setIsVisible(false);
        setIsOpen(false);
      }
    };

    window.addEventListener('scroll', handleScroll, { passive: true });
    return () => window.removeEventListener('scroll', handleScroll);
  }, []);

  const scrollToTop = () => {
    window.scrollTo({ top: 0, behavior: 'smooth' });
    setIsOpen(false);
  };

  if (!isVisible) return null;

  return (
    <aside aria-label="Menu thao tác nhanh" className="fixed bottom-5 right-5 z-40 flex flex-col items-end gap-2 print:hidden">
      {/* Sub-action items when expanded */}
      {isOpen && (
        <div className="flex flex-col items-end gap-2 mb-1 animate-in fade-in slide-in-from-bottom-2 duration-200">
          <a
            href="https://www.google.com/search?q=site%3Aisthaison.github.io%2Fgotv"
            target="_blank"
            rel="noopener noreferrer"
            className="flex items-center gap-2 bg-white hover:bg-stone-50 text-stone-800 text-xs font-bold px-3.5 py-2 rounded-xl border border-stone-200 shadow-lg transition active:scale-95 group"
            title="Tìm kiếm trên Google cho gotv"
          >
            <span>Tìm trên Google</span>
            <span className="w-6 h-6 rounded-lg bg-blue-50 text-blue-600 flex items-center justify-center group-hover:scale-110 transition">
              <Search className="w-3.5 h-3.5" />
            </span>
          </a>

          <a
            href={`${basePath}faq/`}
            className="flex items-center gap-2 bg-white hover:bg-stone-50 text-stone-800 text-xs font-bold px-3.5 py-2 rounded-xl border border-stone-200 shadow-lg transition active:scale-95 group"
            title="Trung tâm Trợ giúp & FAQ"
          >
            <span>Hỏi đáp &amp; Sửa lỗi (FAQ)</span>
            <span className="w-6 h-6 rounded-lg bg-amber-50 text-amber-700 flex items-center justify-center group-hover:scale-110 transition">
              <HelpCircle className="w-3.5 h-3.5" />
            </span>
          </a>

          <a
            href={`${basePath}#tai-ve`}
            className="flex items-center gap-2 bg-white hover:bg-stone-50 text-stone-800 text-xs font-bold px-3.5 py-2 rounded-xl border border-stone-200 shadow-lg transition active:scale-95 group"
            title="Tải về bản mới nhất"
          >
            <span>Tải về GoTV</span>
            <span className="w-6 h-6 rounded-lg bg-red-50 text-red-700 flex items-center justify-center group-hover:scale-110 transition">
              <Download className="w-3.5 h-3.5" />
            </span>
          </a>

          <button
            type="button"
            onClick={scrollToTop}
            className="flex items-center gap-2 bg-white hover:bg-stone-50 text-stone-800 text-xs font-bold px-3.5 py-2 rounded-xl border border-stone-200 shadow-lg transition active:scale-95 group"
            title="Cuộn lên đầu trang"
          >
            <span>Lên đầu trang</span>
            <span className="w-6 h-6 rounded-lg bg-stone-100 text-stone-700 flex items-center justify-center group-hover:scale-110 transition">
              <ArrowUp className="w-3.5 h-3.5" />
            </span>
          </button>
        </div>
      )}

      {/* Main floating trigger button */}
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        aria-label={isOpen ? 'Đóng menu thao tác nhanh' : 'Mở menu thao tác nhanh và tìm kiếm'}
        aria-expanded={isOpen}
        className="w-12 h-12 rounded-2xl bg-stone-900 hover:bg-red-700 text-white flex items-center justify-center shadow-xl transition-all active:scale-95 border-2 border-white focus:outline-none focus:ring-4 focus:ring-red-600/30"
      >
        {isOpen ? (
          <span className="font-bold text-lg leading-none">&times;</span>
        ) : (
          <Sparkles className="w-5 h-5 text-amber-300" />
        )}
      </button>
    </aside>
  );
};
