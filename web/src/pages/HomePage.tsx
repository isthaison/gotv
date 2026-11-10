import React, { useEffect } from 'react';
import { Navbar } from '../components/Navbar';
import { Footer } from '../components/Footer';
import { VietnamesePlayground } from '../components/VietnamesePlayground';
import { OsSelector } from '../components/OsSelector';
import { FloatingActionButton } from '../components/FloatingActionButton';
import { FAQ_ITEMS } from '../data/faqData';
import { Download, ShieldCheck, Cpu, BookMarked, Sparkles, RefreshCw, ChevronRight, CheckCircle2, Star, Smile } from 'lucide-react';
import { WindowsIcon, LinuxIcon, AppleIcon } from '../components/BrandIcons';
import { APP_VERSION } from '../version';

interface HomePageProps {
  basePath?: string;
}

export const HomePage: React.FC<HomePageProps> = ({ basePath = '/gotv/' }) => {
  useEffect(() => {
    if (typeof window !== 'undefined' && !window.location.hash) {
      window.scrollTo(0, 0);
    }
  }, []);

  return (
    <div className="min-h-screen flex flex-col bg-[#faf7f2] text-[#2b2320]">
      <Navbar basePath={basePath} active="home" />

      <main className="flex-1 max-w-5xl mx-auto px-4 py-4 sm:py-8 w-full">
        {/* Hero Section */}
        <section className="text-center py-4 sm:py-10 md:py-16">
          <div className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full bg-red-100 text-red-800 text-xs font-bold mb-3 sm:mb-6">
            <Sparkles className="w-4 h-4 text-red-600" />
            <span>Mã nguồn mở 100% · Lõi C thuần · Hoạt động ngoại tuyến</span>
          </div>

          <h1 className="text-2xl sm:text-4xl md:text-5xl font-black tracking-tight text-stone-900 leading-tight mb-3 sm:mb-4">
            Bộ gõ tiếng Việt <span className="text-red-700">Telex/VNI</span><br className="hidden sm:inline" /> cho Windows, Linux &amp; macOS
          </h1>

          <p className="text-stone-600 max-w-2xl mx-auto text-sm sm:text-base md:text-lg mb-5 sm:mb-8 leading-relaxed">
            Một lõi C duy nhất — gõ <code>duocjwd</code> ra <strong>được</strong> ở mọi nơi. 
            Kiểm tra chính tả ngoại tuyến, gợi ý từ điển offline, macro gõ tắt &amp; emoji.
          </p>

          <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-center gap-2.5 sm:gap-3 max-w-xs sm:max-w-none mx-auto">
            <a
              href="https://github.com/isthaison/gotv/releases/latest"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center justify-center gap-2 bg-red-700 hover:bg-red-800 text-white font-bold px-5 py-3 rounded-xl transition shadow-md hover:shadow-lg text-sm sm:text-base active:scale-95"
            >
              <Download className="w-5 h-5 shrink-0" />
              <span>Tải bản mới nhất (v{APP_VERSION})</span>
            </a>
            <a
              href="https://github.com/isthaison/gotv"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center justify-center gap-2 bg-white hover:bg-stone-100 text-stone-800 border border-stone-300 font-bold px-5 py-3 rounded-xl transition text-sm sm:text-base active:scale-95 shadow-sm"
            >
              <Star className="w-4 h-4 fill-amber-400 text-amber-500 shrink-0" />
              <span>Star trên GitHub</span>
            </a>
          </div>

          {/* Interactive Playground */}
          <VietnamesePlayground />
        </section>

        {/* Features Grid */}
        <section id="tinh-nang" className="py-12 border-t border-stone-200">
          <h2 className="text-2xl sm:text-3xl font-extrabold text-stone-900 mb-8 inline-block border-b-2 border-red-700 pb-1">
            Tính năng nổi bật
          </h2>

          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-5">
            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-red-50 text-red-700 flex items-center justify-center mb-4">
                <Cpu className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Telex + VNI một lõi C</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Một thuật toán biến đổi duy nhất, chỉ khác bảng phím. Tùy chọn đặt dấu tự do kiểu mới (<em>hòa</em>) hoặc truyền thống (<em>hoà</em>).
              </p>
            </article>

            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-amber-50 text-amber-700 flex items-center justify-center mb-4">
                <ShieldCheck className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Chính tả &amp; Gợi ý thông minh</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Không vẽ gạch đỏ phiền toái, gõ thoải mái tiếng Việt, tiếng Anh và mã code. Gợi ý từ đúng qua balloon trên Windows và Candidate List trên macOS.
              </p>
            </article>

            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-emerald-50 text-emerald-700 flex items-center justify-center mb-4">
                <BookMarked className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Gợi ý từ điển &amp; Tự học</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Tự động hoàn thành từ và sửa lỗi chính tả từ kho từ điển ~7.000 từ. Tự động ghi nhớ thói quen gõ cá nhân trên ổ cứng máy tính.
              </p>
            </article>

            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-purple-50 text-purple-700 flex items-center justify-center mb-4">
                <Sparkles className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Macro gõ tắt &amp; Emoji</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Gõ <code>cty</code> bấm Tab để bung thành <em>Công ty TNHH</em>, gõ <code>:smile:</code> &rarr; <Smile className="w-4 h-4 inline-block text-amber-500 align-text-bottom" />. Quản lý trực tiếp qua file text đơn giản.
              </p>
            </article>

            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-blue-50 text-blue-700 flex items-center justify-center mb-4">
                <ShieldCheck className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Nhẹ &amp; Bảo mật riêng tư</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Viết bằng C thuần và thư viện hệ thống glib, không runtime nặng nề, không gửi dữ liệu phím đi đâu. Cài đặt per-user không cần quyền Admin.
              </p>
            </article>

            <article className="bg-white p-6 rounded-2xl border border-stone-200 shadow-sm hover:border-red-300 transition">
              <div className="w-10 h-10 rounded-xl bg-stone-100 text-stone-700 flex items-center justify-center mb-4">
                <RefreshCw className="w-5 h-5" />
              </div>
              <h3 className="font-bold text-lg text-stone-900 mb-2">Tự cập nhật thông minh</h3>
              <p className="text-sm text-stone-600 leading-relaxed">
                Tự động kiểm tra bản phát hành mới từ GitHub, nâng cấp đè giữ nguyên toàn bộ cấu hình cá nhân trên cả Windows, Linux và macOS.
              </p>
            </article>
          </div>
        </section>

        {/* Installation Section */}
        <section id="cai-dat" className="py-12 border-t border-stone-200">
          <h2 className="text-2xl sm:text-3xl font-extrabold text-stone-900 mb-2 inline-block border-b-2 border-red-700 pb-1">
            Cài đặt &amp; Hướng dẫn
          </h2>
          <p className="text-stone-600 text-sm mb-6">Chọn hệ điều hành bạn đang sử dụng để xem hướng dẫn chi tiết:</p>

          <OsSelector />
        </section>

        {/* Releases Section */}
        <section id="tai-ve" className="py-8 sm:py-12 border-t border-stone-200">
          <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 mb-6">
            <div>
              <h2 className="text-2xl sm:text-3xl font-extrabold text-stone-900 inline-block border-b-2 border-red-700 pb-1">
                Tải về các bản phát hành
              </h2>
              <p className="text-stone-600 text-xs sm:text-sm mt-1.5">
                Các gói cài đặt chính thức cho phiên bản mới nhất (v{APP_VERSION})
              </p>
            </div>
            <a
              href="https://github.com/isthaison/gotv/releases/latest"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-1 text-xs sm:text-sm font-bold text-red-700 hover:text-red-800 transition py-1"
            >
              <span>Xem nhật ký thay đổi</span>
              <ChevronRight className="w-4 h-4" />
            </a>
          </div>

          {/* Mobile Card List (< md) */}
          <div className="md:hidden space-y-3">
            {/* Windows Card */}
            <div className="bg-white rounded-2xl border border-stone-200 p-4 shadow-sm hover:border-stone-300 transition">
              <div className="flex items-center justify-between gap-2 mb-2.5">
                <div className="flex items-center gap-2.5">
                  <div className="w-9 h-9 rounded-xl bg-blue-50 flex items-center justify-center shrink-0">
                    <WindowsIcon className="w-5 h-5 text-blue-600" />
                  </div>
                  <div>
                    <h3 className="font-bold text-stone-900 text-sm leading-tight">Windows 10 / 11</h3>
                    <p className="text-[11px] text-stone-500">64-bit &amp; 32-bit (TSF)</p>
                  </div>
                </div>
                <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800 whitespace-nowrap shrink-0">
                  <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                  <span>Ổn định</span>
                </span>
              </div>
              <div className="flex items-center justify-between gap-2 pt-2.5 border-t border-stone-100">
                <code className="bg-stone-100 text-stone-700 px-2 py-1 rounded-lg font-mono text-[11px] truncate flex-1 min-w-0">
                  gotv-*-x64-setup.exe
                </code>
                <a
                  href="https://github.com/isthaison/gotv/releases/latest"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm shrink-0"
                >
                  <Download className="w-3.5 h-3.5" />
                  <span>Tải</span>
                </a>
              </div>
            </div>

            {/* Linux Card */}
            <div className="bg-white rounded-2xl border border-stone-200 p-4 shadow-sm hover:border-stone-300 transition">
              <div className="flex items-center justify-between gap-2 mb-2.5">
                <div className="flex items-center gap-2.5">
                  <div className="w-9 h-9 rounded-xl bg-amber-50 flex items-center justify-center shrink-0">
                    <LinuxIcon className="w-5 h-5 text-amber-600" />
                  </div>
                  <div>
                    <h3 className="font-bold text-stone-900 text-sm leading-tight">Linux</h3>
                    <p className="text-[11px] text-stone-500">Ubuntu, Debian, Fedora, Arch</p>
                  </div>
                </div>
                <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800 whitespace-nowrap shrink-0">
                  <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                  <span>Ổn định</span>
                </span>
              </div>
              <div className="flex items-center justify-between gap-2 pt-2.5 border-t border-stone-100">
                <code className="bg-stone-100 text-stone-700 px-2 py-1 rounded-lg font-mono text-[11px] truncate flex-1 min-w-0">
                  gotv_*_amd64.deb / tar.gz
                </code>
                <a
                  href="https://github.com/isthaison/gotv/releases/latest"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm shrink-0"
                >
                  <Download className="w-3.5 h-3.5" />
                  <span>Tải</span>
                </a>
              </div>
            </div>

            {/* macOS Card */}
            <div className="bg-white rounded-2xl border border-stone-200 p-4 shadow-sm hover:border-stone-300 transition">
              <div className="flex items-center justify-between gap-2 mb-2.5">
                <div className="flex items-center gap-2.5">
                  <div className="w-9 h-9 rounded-xl bg-stone-100 flex items-center justify-center shrink-0">
                    <AppleIcon className="w-5 h-5 text-stone-800" />
                  </div>
                  <div>
                    <h3 className="font-bold text-stone-900 text-sm leading-tight">macOS</h3>
                    <p className="text-[11px] text-stone-500">Apple Silicon &amp; Intel</p>
                  </div>
                </div>
                <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800 whitespace-nowrap shrink-0">
                  <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                  <span>Ổn định (Native)</span>
                </span>
              </div>
              <div className="flex items-center justify-between gap-2 pt-2.5 border-t border-stone-100">
                <code className="bg-stone-100 text-stone-700 px-2 py-1 rounded-lg font-mono text-[11px] truncate flex-1 min-w-0">
                  gotv-*-macos.pkg / .dmg
                </code>
                <a
                  href="https://github.com/isthaison/gotv/releases/latest"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm shrink-0"
                >
                  <Download className="w-3.5 h-3.5" />
                  <span>Tải</span>
                </a>
              </div>
            </div>
          </div>

          {/* Desktop Table (>= md) */}
          <div className="hidden md:block bg-white rounded-2xl border border-stone-200 overflow-hidden shadow-sm">
            <table className="w-full text-left text-sm">
              <thead className="bg-stone-50 border-b border-stone-200 text-stone-700">
                <tr>
                  <th className="px-6 py-4 font-bold">Hệ điều hành</th>
                  <th className="px-6 py-4 font-bold">Tên tệp</th>
                  <th className="px-6 py-4 font-bold">Trạng thái</th>
                  <th className="px-6 py-4 font-bold text-right">Tải về</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-stone-200">
                <tr className="hover:bg-stone-50/60 transition">
                  <td className="px-6 py-4 font-semibold text-stone-900 whitespace-nowrap">
                    <span className="inline-flex items-center gap-2">
                      <WindowsIcon className="w-4 h-4 text-blue-600" />
                      <span>Windows 10/11 (x64)</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 font-mono text-xs text-stone-600 whitespace-nowrap">
                    <code className="bg-stone-100 px-2.5 py-1 rounded-md text-stone-700 font-mono text-xs">
                      gotv-*-x64-setup.exe
                    </code>
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap">
                    <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800">
                      <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                      <span>Ổn định</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 text-right whitespace-nowrap">
                    <a
                      href="https://github.com/isthaison/gotv/releases/latest"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3.5 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm"
                    >
                      <Download className="w-3.5 h-3.5" />
                      <span>Tải</span>
                    </a>
                  </td>
                </tr>
                <tr className="hover:bg-stone-50/60 transition">
                  <td className="px-6 py-4 font-semibold text-stone-900 whitespace-nowrap">
                    <span className="inline-flex items-center gap-2">
                      <LinuxIcon className="w-4 h-4 text-amber-600" />
                      <span>Linux (.deb / tar)</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 font-mono text-xs text-stone-600 whitespace-nowrap">
                    <code className="bg-stone-100 px-2.5 py-1 rounded-md text-stone-700 font-mono text-xs">
                      gotv_*_amd64.deb
                    </code>
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap">
                    <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800">
                      <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                      <span>Ổn định</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 text-right whitespace-nowrap">
                    <a
                      href="https://github.com/isthaison/gotv/releases/latest"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3.5 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm"
                    >
                      <Download className="w-3.5 h-3.5" />
                      <span>Tải</span>
                    </a>
                  </td>
                </tr>
                <tr className="hover:bg-stone-50/60 transition">
                  <td className="px-6 py-4 font-semibold text-stone-900 whitespace-nowrap">
                    <span className="inline-flex items-center gap-2">
                      <AppleIcon className="w-4 h-4 text-stone-800" />
                      <span>macOS (Apple Silicon &amp; Intel)</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 font-mono text-xs text-stone-600 whitespace-nowrap">
                    <code className="bg-stone-100 px-2.5 py-1 rounded-md text-stone-700 font-mono text-xs">
                      gotv-*-macos.pkg / .dmg
                    </code>
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap">
                    <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800">
                      <CheckCircle2 className="w-3.5 h-3.5 text-emerald-600 shrink-0" />
                      <span>Ổn định (Native IMK)</span>
                    </span>
                  </td>
                  <td className="px-6 py-4 text-right whitespace-nowrap">
                    <a
                      href="https://github.com/isthaison/gotv/releases/latest"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="inline-flex items-center gap-1 bg-red-700 hover:bg-red-800 text-white font-bold px-3.5 py-1.5 rounded-xl text-xs transition active:scale-95 shadow-sm"
                    >
                      <Download className="w-3.5 h-3.5" />
                      <span>Tải</span>
                    </a>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <div className="mt-6 text-center">
            <a
              href="https://github.com/isthaison/gotv/releases/latest"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center justify-center gap-2 bg-red-700 hover:bg-red-800 text-white font-bold px-6 py-3 rounded-xl transition text-sm shadow-sm w-full sm:w-auto active:scale-95"
            >
              <span>Xem tất cả bản tải về trên GitHub Releases</span>
              <ChevronRight className="w-4 h-4" />
            </a>
          </div>
        </section>

        {/* FAQ Preview Section */}
        <section id="faq" className="py-12 border-t border-stone-200">
          <div className="flex flex-col sm:flex-row sm:items-end justify-between mb-8 gap-4">
            <div>
              <h2 className="text-2xl sm:text-3xl font-extrabold text-stone-900 mb-2 inline-block border-b-2 border-red-700 pb-1">
                Câu hỏi thường gặp (FAQ)
              </h2>
              <p className="text-stone-600 text-sm">
                Giải đáp nhanh các thắc mắc phổ biến về bản quyền, tính năng và cách xử lý sự cố.
              </p>
            </div>
            <a
              href={`${basePath}faq/`}
              className="inline-flex items-center gap-1 text-sm font-bold text-red-700 hover:underline"
            >
              <span>Xem toàn bộ trung tâm FAQ</span>
              <ChevronRight className="w-4 h-4" />
            </a>
          </div>

          <div className="space-y-4">
            {FAQ_ITEMS.slice(0, 5).map((faq) => (
              <details
                key={faq.id}
                id={`faq-${faq.id}`}
                className="group bg-white rounded-xl border border-stone-200 p-4 transition shadow-sm open:border-red-300"
              >
                <summary className="font-bold text-stone-900 cursor-pointer list-none flex items-center justify-between">
                  <span>{faq.question}</span>
                  <span className="text-stone-400 group-open:rotate-90 transition transform">&rarr;</span>
                </summary>
                <div className="mt-3 text-sm text-stone-600 pt-3 border-t border-stone-100">
                  <p>{faq.shortAnswer}</p>
                  <a
                    href={`${basePath}faq/${faq.slug}.html`}
                    className="inline-block mt-2 font-semibold text-red-700 hover:underline text-xs"
                  >
                    Xem chi tiết bài viết &rarr;
                  </a>
                </div>
              </details>
            ))}
          </div>

          <div className="mt-6 text-center">
            <a
              href={`${basePath}faq/`}
              className="inline-flex items-center gap-2 bg-stone-900 hover:bg-stone-800 text-white font-bold px-6 py-2.5 rounded-xl transition text-sm shadow-sm"
            >
              <span>Vào Trung tâm Trợ giúp &amp; Hỏi đáp (FAQ)</span>
              <ChevronRight className="w-4 h-4" />
            </a>
          </div>
        </section>
      </main>

      <Footer basePath={basePath} />
      <FloatingActionButton basePath={basePath} />
    </div>
  );
};
