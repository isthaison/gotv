import React, { useState } from 'react';
import { Download, Terminal, CheckCircle2 } from 'lucide-react';
import { WindowsIcon, LinuxIcon, AppleIcon } from './BrandIcons';

export const OsSelector: React.FC = () => {
  const [activeOs, setActiveOs] = useState<'windows' | 'linux' | 'macos'>('windows');

  return (
    <div className="bg-white rounded-2xl border border-stone-200 shadow-sm overflow-hidden my-8">
      {/* OS Tabs */}
      <div className="flex border-b border-stone-200 bg-stone-50/80">
        <button
          onClick={() => setActiveOs('windows')}
          className={`flex-1 py-3 px-2 sm:py-3.5 sm:px-4 text-center font-bold text-xs sm:text-sm transition flex items-center justify-center gap-1.5 sm:gap-2 border-b-2 ${
            activeOs === 'windows'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          <WindowsIcon className="w-4 h-4 sm:w-4.5 sm:h-4.5 text-blue-600 shrink-0" />
          <span>Windows</span>
          <span className="hidden sm:inline">10/11</span>
        </button>
        <button
          onClick={() => setActiveOs('linux')}
          className={`flex-1 py-3 px-2 sm:py-3.5 sm:px-4 text-center font-bold text-xs sm:text-sm transition flex items-center justify-center gap-1.5 sm:gap-2 border-b-2 ${
            activeOs === 'linux'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          <LinuxIcon className="w-4 h-4 sm:w-4.5 sm:h-4.5 text-amber-600 shrink-0" />
          <span>Linux</span>
          <span className="hidden sm:inline">(IBus)</span>
        </button>
        <button
          onClick={() => setActiveOs('macos')}
          className={`flex-1 py-3 px-2 sm:py-3.5 sm:px-4 text-center font-bold text-xs sm:text-sm transition flex items-center justify-center gap-1.5 sm:gap-2 border-b-2 ${
            activeOs === 'macos'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          <AppleIcon className="w-4 h-4 sm:w-4.5 sm:h-4.5 text-stone-800 shrink-0" />
          <span>macOS</span>
          <span className="hidden sm:inline">(Native)</span>
        </button>
      </div>

      {/* Tab Content */}
      <div className="p-6">
        {activeOs === 'windows' && (
          <div className="space-y-4">
            <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 bg-red-50/60 p-4 rounded-xl border border-red-100">
              <div>
                <h4 className="font-bold text-stone-900 text-base">Bộ cài đặt GoTV cho Windows</h4>
                <p className="text-xs text-stone-600">Hỗ trợ Windows 10 &amp; 11 (TSF 64-bit &amp; WOW64 32-bit cho Zalo). Không cần quyền Admin.</p>
              </div>
              <a
                href="https://github.com/isthaison/gotv/releases/latest"
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2 bg-red-700 hover:bg-red-800 text-white font-bold px-5 py-2.5 rounded-xl transition shadow-sm text-sm shrink-0"
              >
                <Download className="w-4 h-4" /> Tải gotv-*-x64-setup.exe
              </a>
            </div>
            <div className="text-sm text-stone-600 space-y-2">
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Chạy file cài đặt &rarr; Đăng xuất / đăng nhập lại Windows.</span>
              </p>
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Chuyển đổi nhanh bàn phím sang <strong>GoTV</strong> bằng phím tắt <code>Win + Space</code>.</span>
              </p>
            </div>
          </div>
        )}

        {activeOs === 'linux' && (
          <div className="space-y-4">
            <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 bg-amber-50/60 p-4 rounded-xl border border-amber-200/70">
              <div>
                <h4 className="font-bold text-stone-900 text-base">Gói cài đặt GoTV cho Linux (.deb / .tar.gz)</h4>
                <p className="text-xs text-stone-600">Hỗ trợ Ubuntu, Debian, Linux Mint, Pop!_OS, Zorin OS và các bản phân phối IBus / Fcitx5.</p>
              </div>
              <a
                href="https://github.com/isthaison/gotv/releases/latest"
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2 bg-stone-900 hover:bg-stone-800 text-white font-bold px-5 py-2.5 rounded-xl transition shadow-sm text-sm shrink-0"
              >
                <Download className="w-4 h-4" /> Tải gotv_*_amd64.deb
              </a>
            </div>

            <div className="text-sm text-stone-600 space-y-2.5">
              <p className="flex items-start gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0 mt-0.5" />
                <span><strong>Cách 1 (Giao diện đồ họa):</strong> Tải file <code>.deb</code> về máy &rarr; Nhấp đúp chuột để mở bằng <em>Software Install</em> &rarr; Nhấn <strong>Install</strong>.</span>
              </p>
              <p className="flex items-start gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0 mt-0.5" />
                <span><strong>Cách 2 (Lệnh nhanh):</strong> Mở thư mục chứa file tải về và chạy <code>sudo apt install ./gotv_*_amd64.deb</code>.</span>
              </p>
              <p className="flex items-start gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0 mt-0.5" />
                <span>Vào <strong>Settings &rarr; Keyboard &rarr; Input Sources</strong> &rarr; Bấm <strong>+</strong> &rarr; Chọn <strong>Vietnamese (GoTV)</strong>. Khởi động lại IBus nếu cần (<code>ibus restart</code>).</span>
              </p>
              <p className="flex items-start gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0 mt-0.5" />
                <span>Chuyển đổi kiểu gõ bằng <code>Super + Space</code>. Gõ tiếng Việt cực mượt, không lỗi gạch chân, không gửi dữ liệu ra ngoài.</span>
              </p>
            </div>

            {/* Developer option: compile from source */}
            <details className="mt-2 group border border-stone-200 rounded-xl overflow-hidden bg-white text-xs">
              <summary className="p-3 bg-stone-50 text-stone-700 font-semibold cursor-pointer list-none flex items-center justify-between hover:bg-stone-100 transition">
                <span className="flex items-center gap-1.5">
                  <Terminal className="w-3.5 h-3.5 text-stone-500" />
                  <span>Dành cho lập trình viên: Biên dịch trực tiếp từ mã nguồn C (Tùy chọn)</span>
                </span>
                <span className="text-stone-400 group-open:rotate-90 transition transform">&rarr;</span>
              </summary>
              <div className="p-3.5 bg-stone-900 text-stone-200 font-mono text-[11px] overflow-x-auto">
                <pre className="text-emerald-400">
{`# Cài thư viện build cần thiết trên Ubuntu/Debian
sudo apt install build-essential pkg-config libibus-1.0-dev libgtk-3-dev

# Tải mã nguồn, biên dịch và cài đặt GoTV
git clone https://github.com/isthaison/gotv.git
cd gotv
./gtv.sh build
sudo ./gtv.sh install`}
                </pre>
              </div>
            </details>
          </div>
        )}

        {activeOs === 'macos' && (
          <div className="space-y-4">
            <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 bg-stone-50 p-4 rounded-xl border border-stone-200">
              <div>
                <h4 className="font-bold text-stone-900 text-base">Bộ cài đặt GoTV cho macOS (.pkg / .dmg)</h4>
                <p className="text-xs text-stone-600">Chuẩn Apple InputMethodKit (IMK), hỗ trợ macOS Monterey, Ventura, Sonoma &amp; Sequoia.</p>
              </div>
              <a
                href="https://github.com/isthaison/gotv/releases/latest"
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2 bg-stone-900 hover:bg-stone-800 text-white font-bold px-5 py-2.5 rounded-xl transition shadow-sm text-sm shrink-0"
              >
                <Download className="w-4 h-4" /> Tải gotv-*-macos.pkg / .dmg
              </a>
            </div>
            <div className="text-sm text-stone-600 space-y-2">
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Chạy gói cài đặt <code>.pkg</code> (hoặc kéo <code>GoTV.app</code> vào <code>/Library/Input Methods/</code>).</span>
              </p>
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Vào <strong>System Settings &rarr; Keyboard &rarr; Input Sources</strong> &rarr; Bấm dấu <strong>+</strong> &rarr; Chọn <strong>Vietnamese &rarr; GoTV</strong>.</span>
              </p>
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Chuẩn native Apple IMK: Gõ mượt mà không gạch chân, hỗ trợ Terminal (Ghostty, iTerm2, Kitty...), có thanh ứng viên gợi ý Candidate List.</span>
              </p>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
