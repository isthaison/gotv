import React, { useState } from 'react';
import { Navbar } from '../components/Navbar';
import { Footer } from '../components/Footer';
import { FloatingActionButton } from '../components/FloatingActionButton';
import { FAQ_ITEMS } from '../data/faqData';
import { Search, ChevronRight, HelpCircle, ExternalLink } from 'lucide-react';

interface FaqIndexPageProps {
  basePath?: string;
}

export const FaqIndexPage: React.FC<FaqIndexPageProps> = ({ basePath = '/gotv/' }) => {
  const [searchTerm, setSearchTerm] = useState('');

  const filteredFaqs = FAQ_ITEMS.filter(
    (faq) =>
      faq.question.toLowerCase().includes(searchTerm.toLowerCase()) ||
      faq.shortAnswer.toLowerCase().includes(searchTerm.toLowerCase())
  );

  return (
    <div className="min-h-screen flex flex-col bg-[#faf7f2] text-[#2b2320]">
      <Navbar basePath={basePath} active="faq" />

      <main className="flex-1 max-w-4xl mx-auto px-4 py-8 w-full">
        {/* Breadcrumb */}
        <nav className="text-xs text-stone-500 mb-6 flex items-center gap-1.5 font-medium">
          <a href={basePath} className="hover:text-red-700">Trang chủ</a>
          <span>&rsaquo;</span>
          <span className="text-stone-900 font-bold">Câu hỏi thường gặp (FAQ)</span>
        </nav>

        {/* Header */}
        <div className="text-center mb-10">
          <div className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full bg-red-100 text-red-800 text-xs font-bold mb-3">
            <HelpCircle className="w-3.5 h-3.5" />
            <span>Trung tâm trợ giúp chính thức</span>
          </div>
          <h1 className="text-3xl sm:text-4xl font-black text-stone-900 mb-3">
            Câu hỏi thường gặp
          </h1>
          <p className="text-stone-600 text-sm sm:text-base max-w-xl mx-auto">
            Tổng hợp câu hỏi, hướng dẫn cài đặt, mẹo sử dụng và cách khắc phục sự cố bộ gõ tiếng Việt GoTV.
          </p>

          {/* Search bar with Google Search integration */}
          <div className="max-w-md mx-auto mt-6">
            <div className="relative">
              <Search className="w-4 h-4 text-stone-400 absolute left-3.5 top-1/2 -translate-y-1/2" />
              <input
                type="text"
                value={searchTerm}
                onChange={(e) => setSearchTerm(e.target.value)}
                placeholder="Tìm kiếm câu hỏi hoặc từ khóa..."
                className="w-full bg-white border border-stone-300 rounded-xl pl-10 pr-4 py-2.5 text-sm placeholder-stone-400 focus:outline-none focus:border-red-600 shadow-sm"
              />
            </div>
            {searchTerm.trim().length > 0 && (
              <div className="mt-2 text-xs text-stone-500 flex items-center justify-center gap-1.5">
                <span>Không thấy câu hỏi?</span>
                <a
                  href={`https://www.google.com/search?q=site%3Aisthaison.github.io%2Fgotv+${encodeURIComponent(searchTerm)}`}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="font-bold text-red-700 hover:underline inline-flex items-center gap-1"
                >
                  <Search className="w-3 h-3" />
                  <span>Tìm kiếm &ldquo;{searchTerm}&rdquo; trên Google</span>
                  <ExternalLink className="w-2.5 h-2.5" />
                </a>
              </div>
            )}
          </div>
        </div>

        {/* FAQ List */}
        <div className="space-y-4">
          {filteredFaqs.length > 0 ? (
            filteredFaqs.map((faq) => (
              <article
                key={faq.id}
                className="bg-white rounded-2xl border border-stone-200 p-6 shadow-sm hover:border-red-300 transition group"
              >
                <div className="flex items-start justify-between gap-4">
                  <div>
                    <h2 className="text-lg font-bold text-stone-900 mb-2 group-hover:text-red-700 transition">
                      <a href={`${basePath}faq/${faq.slug}.html`} className="hover:underline">
                        {faq.question}
                      </a>
                    </h2>
                    <p className="text-sm text-stone-600 leading-relaxed mb-4">
                      {faq.shortAnswer}
                    </p>
                    <a
                      href={`${basePath}faq/${faq.slug}.html`}
                      className="inline-flex items-center gap-1 text-xs font-bold text-red-700 hover:underline"
                    >
                      <span>Đọc bài viết chi tiết</span>
                      <ChevronRight className="w-3.5 h-3.5" />
                    </a>
                  </div>
                </div>
              </article>
            ))
          ) : (
            <div className="text-center py-12 bg-white rounded-2xl border border-stone-200 text-stone-600 p-6">
              <p className="mb-4">Không tìm thấy câu hỏi phù hợp với từ khóa &ldquo;{searchTerm}&rdquo; trong các mục FAQ có sẵn.</p>
              <a
                href={`https://www.google.com/search?q=site%3Aisthaison.github.io%2Fgotv+${encodeURIComponent(searchTerm)}`}
                target="_blank"
                rel="noopener noreferrer"
                className="inline-flex items-center gap-2 bg-stone-900 hover:bg-stone-800 text-white text-xs font-bold px-4 py-2.5 rounded-xl transition shadow-sm"
              >
                <Search className="w-3.5 h-3.5" />
                <span>Tìm kiếm &ldquo;{searchTerm}&rdquo; bằng Google Search</span>
                <ExternalLink className="w-3 h-3" />
              </a>
            </div>
          )}
        </div>

        <div className="mt-10 text-center">
          <a
            href={basePath}
            className="inline-flex items-center gap-1.5 text-sm font-semibold text-stone-600 hover:text-red-700"
          >
            &larr; Quay lại trang chủ GoTV
          </a>
        </div>
      </main>

      <Footer basePath={basePath} />
      <FloatingActionButton basePath={basePath} />
    </div>
  );
};
