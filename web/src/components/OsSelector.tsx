import React, { useState } from 'react';
import { Download, Terminal, CheckCircle2 } from 'lucide-react';

export const OsSelector: React.FC = () => {
  const [activeOs, setActiveOs] = useState<'windows' | 'linux' | 'macos'>('windows');

  return (
    <div className="bg-white rounded-2xl border border-stone-200 shadow-sm overflow-hidden my-8">
      {/* OS Tabs */}
      <div className="flex border-b border-stone-200 bg-stone-50/80">
        <button
          onClick={() => setActiveOs('windows')}
          className={`flex-1 py-3.5 px-4 text-center font-bold text-sm transition flex items-center justify-center gap-2 border-b-2 ${
            activeOs === 'windows'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          🪟 Windows 10/11
        </button>
        <button
          onClick={() => setActiveOs('linux')}
          className={`flex-1 py-3.5 px-4 text-center font-bold text-sm transition flex items-center justify-center gap-2 border-b-2 ${
            activeOs === 'linux'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          🐧 Linux (Ubuntu / IBus)
        </button>
        <button
          onClick={() => setActiveOs('macos')}
          className={`flex-1 py-3.5 px-4 text-center font-bold text-sm transition flex items-center justify-center gap-2 border-b-2 ${
            activeOs === 'macos'
              ? 'border-red-700 text-red-700 bg-white'
              : 'border-transparent text-stone-600 hover:text-stone-900'
          }`}
        >
          🍎 macOS (Native IMK)
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
            <div className="bg-stone-900 text-stone-200 p-4 rounded-xl font-mono text-xs overflow-x-auto">
              <div className="flex items-center gap-2 text-stone-400 mb-2 border-b border-stone-800 pb-1">
                <Terminal className="w-4 h-4" />
                <span>Cài đặt từ mã nguồn hoặc gói .deb</span>
              </div>
              <pre className="text-emerald-400">
{`# Cài thư viện build cần thiết
sudo apt install build-essential pkg-config libibus-1.0-dev libgtk-3-dev
# Build và cài đặt GoTV
./gtv.sh build
sudo ./gtv.sh install`}
              </pre>
            </div>
            <div className="text-sm text-stone-600 space-y-2">
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Thêm <strong>GoTV</strong> trong Settings &rarr; Keyboard &rarr; Input Sources.</span>
              </p>
              <p className="flex items-center gap-2">
                <CheckCircle2 className="w-4 h-4 text-green-600 shrink-0" />
                <span>Chuyển đổi gõ bằng <code>Super + Space</code>. Tự động kiểm tra chính tả gạch đỏ ngoại tuyến.</span>
              </p>
            </div>
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
