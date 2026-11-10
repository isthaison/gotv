import React from 'react';
import { Navbar } from '../components/Navbar';
import { Footer } from '../components/Footer';
import { VietnamesePlayground } from '../components/VietnamesePlayground';
import { OsSelector } from '../components/OsSelector';
import { FloatingActionButton } from '../components/FloatingActionButton';
import { FAQ_ITEMS } from '../data/faqData';
import { Download, ShieldCheck, Cpu, BookMarked, Sparkles, RefreshCw, ChevronRight, CheckCircle2 } from 'lucide-react';

interface HomePageProps {
  basePath?: string;
}

export const HomePage: React.FC<HomePageProps> = ({ basePath = '/gotv/' }) => {
  return (
    <div className="min-h-screen flex flex-col bg-[#faf7f2] text-[#2b2320]">
      <Navbar basePath={basePath} active="home" />

      <main className="flex-1 max-w-5xl mx-auto px-4 py-8 w-full">
        {/* Hero Section */}
        <section className="text-center py-12 md:py-16">
          <div className="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-full bg-red-100 text-red-800 text-xs font-bold mb-6">
            <Sparkles className="w-4 h-4 text-red-600" />
            <span>Mã nguồn mở 100% · Lõi C thuần · Hoạt động ngoại tuyến</span>
          </div>

          <h1 className="text-3xl sm:text-5xl font-black tracking-tight text-stone-900 leading-tight mb-4">
            Bộ gõ tiếng Việt <span className="text-red-700">Telex/VNI</span><br className="hidden sm:inline" /> cho Windows, Linux &amp; macOS
          </h1>

          <p className="text-stone-600 max-w-2xl mx-auto text-base sm:text-lg mb-8 leading-relaxed">
            Một lõi C duy nhất — gõ <code>duocjwd</code> ra <strong>được</strong> ở mọi nơi. 
            Kiểm tra chính tả ngoại tuyến, gợi ý từ điển offline, macro gõ tắt &amp; emoji.
          </p>

          <div className="flex flex-wrap items-center justify-center gap-3">
            <a
              href="https://github.com/isthaison/gotv/releases/latest"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 bg-red-700 hover:bg-red-800 text-white font-bold px-6 py-3 rounded-xl transition shadow-md hover:shadow-lg text-sm sm:text-base"
            >
              <Download className="w-5 h-5" />
              <span>Tải bản mới nhất (v0.9.9)</span>
            </a>
            <a
              href="https://github.com/isthaison/gotv"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 bg-white hover:bg-stone-100 text-stone-800 border border-stone-300 font-bold px-6 py-3 rounded-xl transition text-sm sm:text-base"
            >
              <span>★ Star trên GitHub</span>
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
                Gõ <code>cty</code> bấm Tab để bung thành <em>Công ty TNHH</em>, gõ <code>:smile:</code> &rarr; 😊. Quản lý trực tiếp qua file text đơn giản.
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

        {/* Releases Table */}
        <section id="tai-ve" className="py-12 border-t border-stone-200">
          <h2 className="text-2xl sm:text-3xl font-extrabold text-stone-900 mb-6 inline-block border-b-2 border-red-700 pb-1">
            Tải về các bản phát hành
          </h2>

          <div className="bg-white rounded-2xl border border-stone-200 overflow-hidden shadow-sm">
            <table className="w-full text-left text-sm">
              <thead className="bg-stone-50 border-b border-stone-200 text-stone-700">
                <tr>
                  <th className="p-4 font-bold">Hệ điều hành</th>
                  <th className="p-4 font-bold">Tên tệp</th>
                  <th className="p-4 font-bold">Trạng thái</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-stone-200">
                <tr>
                  <td className="p-4 font-semibold text-stone-900">🪟 Windows 10/11 (x64)</td>
                  <td className="p-4 font-mono text-xs text-stone-600">gotv-*-x64-setup.exe</td>
                  <td className="p-4">
                    <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-green-100 text-green-800">
                      <CheckCircle2 className="w-3.5 h-3.5" /> Ổn định
                    </span>
                  </td>
                </tr>
                <tr>
                  <td className="p-4 font-semibold text-stone-900">🐧 Linux (.deb / tar)</td>
                  <td className="p-4 font-mono text-xs text-stone-600">gotv_*_amd64.deb</td>
                  <td className="p-4">
                    <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-green-100 text-green-800">
                      <CheckCircle2 className="w-3.5 h-3.5" /> Ổn định
                    </span>
                  </td>
                </tr>
                <tr>
                  <td className="p-4 font-semibold text-stone-900">🍎 macOS (Apple Silicon &amp; Intel)</td>
                  <td className="p-4 font-mono text-xs text-stone-600">gotv-*-macos.pkg / .dmg</td>
                  <td className="p-4">
                    <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-green-100 text-green-800">
                      <CheckCircle2 className="w-3.5 h-3.5" /> Ổn định (Native IMK)
                    </span>
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
              className="inline-flex items-center gap-2 bg-red-700 hover:bg-red-800 text-white font-bold px-6 py-2.5 rounded-xl transition text-sm shadow-sm"
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
