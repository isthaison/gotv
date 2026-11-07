import React from 'react';
import { Code, FileText } from 'lucide-react';

interface FooterProps {
  basePath?: string;
}

const GithubIcon = ({ className }: { className?: string }) => (
  <svg className={className} fill="currentColor" viewBox="0 0 24 24">
    <path fillRule="evenodd" clipRule="evenodd" d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.53 1.032 1.53 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z" />
  </svg>
);

export const Footer: React.FC<FooterProps> = ({ basePath = '/gotv/' }) => {
  return (
    <footer className="border-t border-stone-200 mt-20 bg-white/50 text-stone-600 text-sm py-12">
      <div className="max-w-5xl mx-auto px-4 text-center">
        <div className="flex flex-wrap justify-center gap-6 mb-4 font-medium">
          <a href={basePath} className="hover:text-red-700 transition">Trang chủ</a>
          <a href={`${basePath}faq/`} className="hover:text-red-700 transition">Câu hỏi thường gặp (FAQ)</a>
          <a href={`${basePath}privacy.html`} className="hover:text-red-700 transition">Quyền riêng tư / Privacy</a>
          <a href="https://github.com/isthaison/gotv" target="_blank" rel="noopener noreferrer" className="hover:text-red-700 transition inline-flex items-center gap-1">
            <GithubIcon className="w-3.5 h-3.5" /> Mã nguồn GitHub
          </a>
          <a href="https://github.com/isthaison/gotv/blob/main/docs/config.md" target="_blank" rel="noopener noreferrer" className="hover:text-red-700 transition inline-flex items-center gap-1">
            <FileText className="w-3.5 h-3.5" /> Hướng dẫn cấu hình
          </a>
          <a href="https://github.com/isthaison/gotv/blob/main/docs/dev.md" target="_blank" rel="noopener noreferrer" className="hover:text-red-700 transition inline-flex items-center gap-1">
            <Code className="w-3.5 h-3.5" /> Dành cho nhà phát triển
          </a>
        </div>
        <p className="text-xs text-stone-500 mb-2">
          <strong>GoTV</strong> — Bộ gõ tiếng Việt Telex/VNI mã nguồn mở đa nền tảng (MIT License). Bản quyền &copy; {new Date().getFullYear()} isthaison.
        </p>
        <p className="text-xs text-stone-400">
          bộ gõ tiếng việt &bull; telex &bull; vni &bull; vietnamese input method &bull; ibus gotv &bull; tsf vietnamese &bull; unikey alternative
        </p>
      </div>
    </footer>
  );
};
