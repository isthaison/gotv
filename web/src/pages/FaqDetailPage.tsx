import React from 'react';
import { Navbar } from '../components/Navbar';
import { Footer } from '../components/Footer';
import { FloatingActionButton } from '../components/FloatingActionButton';
import { FaqItem, FAQ_ITEMS } from '../data/faqData';
import { Calendar, User, ArrowLeft, ChevronRight } from 'lucide-react';

interface FaqDetailPageProps {
  faq: FaqItem;
  basePath?: string;
}

export const FaqDetailPage: React.FC<FaqDetailPageProps> = ({ faq, basePath = '/gotv/' }) => {
  const relatedFaqs = FAQ_ITEMS.filter((item) => item.id !== faq.id).slice(0, 3);

  return (
    <div className="min-h-screen flex flex-col bg-[#faf7f2] text-[#2b2320]">
      <Navbar basePath={basePath} active="faq" />

      <main className="flex-1 max-w-3xl mx-auto px-4 py-8 w-full">
        {/* Breadcrumb */}
        <nav className="text-xs text-stone-500 mb-6 flex items-center gap-1.5 font-medium flex-wrap">
          <a href={basePath} className="hover:text-red-700">Trang chủ</a>
          <span>&rsaquo;</span>
          <a href={`${basePath}faq/`} className="hover:text-red-700">FAQ</a>
          <span>&rsaquo;</span>
          <span className="text-stone-900 font-bold truncate max-w-60 sm:max-w-none">{faq.question}</span>
        </nav>

        {/* Article Box */}
        <article className="bg-white rounded-3xl border border-stone-200 p-6 sm:p-10 shadow-sm" id="answer">
          <h1 className="text-2xl sm:text-3xl font-black text-stone-900 leading-snug mb-4">
            {faq.question}
          </h1>

          <div className="flex flex-wrap items-center gap-4 text-xs text-stone-500 border-b border-stone-100 pb-5 mb-6">
            <span className="inline-flex items-center gap-1.5">
              <Calendar className="w-3.5 h-3.5" />
              <span>Cập nhật: {faq.dateModified}</span>
            </span>
            <span className="inline-flex items-center gap-1.5">
              <User className="w-3.5 h-3.5" />
              <span>Dự án GoTV (isthaison)</span>
            </span>
          </div>

          {/* Answer HTML */}
          <div
            className="text-stone-700 leading-relaxed text-sm sm:text-base space-y-4"
            dangerouslySetInnerHTML={{ __html: faq.detailHtml }}
          />

          <div className="border-t border-stone-100 pt-6 mt-8 flex justify-between items-center">
            <a
              href={`${basePath}faq/`}
              className="inline-flex items-center gap-1.5 text-xs sm:text-sm font-bold text-red-700 hover:underline"
            >
              <ArrowLeft className="w-4 h-4" />
              <span>Xem tất cả câu hỏi thường gặp (FAQ)</span>
            </a>
          </div>
        </article>

        {/* Related Questions */}
        <section className="mt-12">
          <h3 className="text-lg font-bold text-stone-900 mb-4">Các câu hỏi liên quan khác</h3>
          <div className="space-y-3">
            {relatedFaqs.map((item) => (
              <a
                key={item.id}
                href={`${basePath}faq/${item.slug}.html`}
                className="block bg-white p-4 rounded-xl border border-stone-200 hover:border-red-300 transition shadow-sm group"
              >
                <div className="flex items-center justify-between text-sm font-bold text-stone-800 group-hover:text-red-700">
                  <span>{item.question}</span>
                  <ChevronRight className="w-4 h-4 text-stone-400 group-hover:text-red-700 shrink-0" />
                </div>
              </a>
            ))}
          </div>
        </section>
      </main>

      <Footer basePath={basePath} />
      <FloatingActionButton basePath={basePath} />
    </div>
  );
};
