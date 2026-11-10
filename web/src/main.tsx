import React from 'react';
import ReactDOM from 'react-dom/client';
import { HomePage } from './pages/HomePage';
import { FaqIndexPage } from './pages/FaqIndexPage';
import { FaqDetailPage } from './pages/FaqDetailPage';
import { FAQ_ITEMS } from './data/faqData';
import './index.css';

// Prevent pinch-to-zoom gestures on iOS Safari
if (typeof document !== 'undefined') {
  document.addEventListener('gesturestart', (e) => e.preventDefault(), { passive: false });
  document.addEventListener('gesturechange', (e) => e.preventDefault(), { passive: false });
  document.addEventListener('gestureend', (e) => e.preventDefault(), { passive: false });
}

const rootElement = document.getElementById('root');
if (rootElement) {
  const basePath = import.meta.env.BASE_URL || '/gotv/';
  const pathname = window.location.pathname;

  let page = <HomePage basePath={basePath} />;

  if (pathname.includes('/faq/')) {
    const matchedFaq = FAQ_ITEMS.find((f) => pathname.includes(f.slug));
    if (matchedFaq) {
      page = <FaqDetailPage faq={matchedFaq} basePath={basePath} />;
    } else {
      page = <FaqIndexPage basePath={basePath} />;
    }
  }

  ReactDOM.createRoot(rootElement).render(
    <React.StrictMode>
      {page}
    </React.StrictMode>
  );
}
