import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'vite';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, '..');
const siteDir = path.resolve(rootDir, '../site');
const distDir = path.resolve(rootDir, 'dist');

async function prerender() {
  console.log('🚀 Bắt đầu quá trình Static Site Generation (SSG)...');

  // 1. Tìm tệp CSS và JS đã build trong dist/assets
  let cssFile = '';
  let jsFile = '';
  if (fs.existsSync(path.join(distDir, 'assets'))) {
    const assetFiles = fs.readdirSync(path.join(distDir, 'assets'));
    const css = assetFiles.find((f) => f.endsWith('.css'));
    const js = assetFiles.find((f) => f.endsWith('.js'));
    if (css) cssFile = `/gotv/assets/${css}`;
    if (js) jsFile = `/gotv/assets/${js}`;
  }

  // Sao chép assets sang site/assets
  if (fs.existsSync(path.join(distDir, 'assets'))) {
    fs.mkdirSync(path.join(siteDir, 'assets'), { recursive: true });
    for (const f of fs.readdirSync(path.join(distDir, 'assets'))) {
      fs.copyFileSync(path.join(distDir, 'assets', f), path.join(siteDir, 'assets', f));
    }
    console.log('✅ Đã copy assets từ web/dist sang site/assets');
  }

  // 2. Khởi tạo Vite SSR loader
  const vite = await createServer({
    root: rootDir,
    server: { middlewareMode: true },
    appType: 'custom',
  });

  const { HomePage } = await vite.ssrLoadModule('/src/pages/HomePage.tsx');
  const { FaqIndexPage } = await vite.ssrLoadModule('/src/pages/FaqIndexPage.tsx');
  const { FaqDetailPage } = await vite.ssrLoadModule('/src/pages/FaqDetailPage.tsx');
  const { FAQ_ITEMS } = await vite.ssrLoadModule('/src/data/faqData.ts');

  await vite.close();

  // Helper tạo khung HTML đầy đủ
  function buildHtmlTemplate({ title, description, canonical, schema, bodyContent, isFaqSubpage = false }) {
    const assetPrefix = isFaqSubpage ? '../' : './';
    return `<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>${title}</title>
  <meta name="description" content="${description}">
  <meta name="keywords" content="bộ gõ tiếng việt, gotv, telex, vni, vietnamese input method, ibus vietnamese, unikey alternative, evkey, gõ tiếng việt linux, gõ tiếng việt windows, gõ tiếng việt macos">
  <meta name="author" content="isthaison">
  <meta name="robots" content="index, follow, max-image-preview:large, max-snippet:-1, max-video-preview:-1">
  <meta name="googlebot" content="index, follow">
  <meta name="theme-color" content="#b22222">
  <link rel="canonical" href="${canonical}">
  <link rel="alternate" hreflang="vi" href="${canonical}">
  <link rel="alternate" hreflang="x-default" href="${canonical}">
  <meta property="og:type" content="${isFaqSubpage ? 'article' : 'website'}">
  <meta property="og:locale" content="vi_VN">
  <meta property="og:site_name" content="GoTV">
  <meta property="og:title" content="${title}">
  <meta property="og:description" content="${description}">
  <meta property="og:url" content="${canonical}">
  <meta property="og:image" content="https://isthaison.github.io/gotv/og-image.png">
  <meta property="og:image:width" content="1200">
  <meta property="og:image:height" content="630">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="${title}">
  <meta name="twitter:description" content="${description}">
  <meta name="twitter:image" content="https://isthaison.github.io/gotv/og-image.png">
  <link rel="icon" href="${assetPrefix}favicon.svg" type="image/svg+xml" sizes="any">
  <link rel="apple-touch-icon" href="${assetPrefix}og-image.png">
  ${cssFile ? `<link rel="stylesheet" href="${cssFile}">` : `<link rel="stylesheet" href="${assetPrefix}styles.css">`}
  <script type="application/ld+json">
${JSON.stringify(schema, null, 2)}
  </script>
</head>
<body class="bg-[#faf7f2] text-[#2b2320]">
  <div id="root">${bodyContent}</div>
  ${jsFile ? `<script type="module" src="${jsFile}"></script>` : ''}
</body>
</html>`;
  }

  // 3. Render Trang Chủ
  console.log('📄 Đang render Trang chủ (index.html)...');
  const homeContent = renderToStaticMarkup(React.createElement(HomePage, { basePath: '/gotv/' }));
  const homeSchema = {
    "@context": "https://schema.org",
    "@graph": [
      {
        "@type": "WebSite",
        "@id": "https://isthaison.github.io/gotv/#website",
        "url": "https://isthaison.github.io/gotv/",
        "name": "GoTV",
        "alternateName": ["Bộ gõ tiếng Việt GoTV", "GoTV Vietnamese Input", "GoTiengViet (isthaison)"],
        "description": "Trang chủ chính thức của bộ gõ tiếng Việt GoTV (Telex/VNI) cho Windows, Linux và macOS.",
        "inLanguage": "vi"
      },
      {
        "@type": "SoftwareApplication",
        "@id": "https://isthaison.github.io/gotv/#software",
        "name": "GoTV",
        "alternateName": ["Bộ gõ tiếng Việt GoTV", "ibus-gotv"],
        "url": "https://isthaison.github.io/gotv/",
        "image": "https://isthaison.github.io/gotv/og-image.png",
        "applicationCategory": "UtilitiesApplication",
        "applicationSubCategory": "Vietnamese input method",
        "operatingSystem": ["Linux", "Ubuntu", "Windows 10", "Windows 11", "macOS"],
        "softwareVersion": "0.9.6",
        "datePublished": "2026-09-05",
        "dateModified": new Date().toISOString().split('T')[0],
        "license": "https://opensource.org/licenses/MIT",
        "inLanguage": "vi",
        "description": "Bộ gõ tiếng Việt Telex/VNI mã nguồn mở cho Windows, Linux và macOS với kiểm tra chính tả, macro/emoji và gợi ý từ điển ngoại tuyến.",
        "downloadUrl": "https://github.com/isthaison/gotv/releases/latest",
        "codeRepository": "https://github.com/isthaison/gotv",
        "sameAs": "https://github.com/isthaison/gotv",
        "author": {
          "@type": "Person",
          "name": "isthaison",
          "url": "https://github.com/isthaison"
        }
      }
    ]
  };

  const homeHtml = buildHtmlTemplate({
    title: 'GoTV — Bộ gõ tiếng Việt Telex/VNI cho Windows, Linux, macOS',
    description: 'GoTV là bộ gõ tiếng Việt Telex/VNI mã nguồn mở cho Windows, Linux (IBus) và macOS. Nhẹ, riêng tư, có kiểm tra chính tả, macro/emoji và gợi ý từ điển ngoại tuyến.',
    canonical: 'https://isthaison.github.io/gotv/',
    schema: homeSchema,
    bodyContent: homeContent,
    isFaqSubpage: false,
  });

  fs.writeFileSync(path.join(siteDir, 'index.html'), homeHtml, 'utf-8');

  // 4. Render Trang FAQ Index
  console.log('📄 Đang render Trang Trung tâm FAQ (faq/index.html)...');
  fs.mkdirSync(path.join(siteDir, 'faq'), { recursive: true });
  const faqIndexContent = renderToStaticMarkup(React.createElement(FaqIndexPage, { basePath: '/gotv/' }));
  const faqIndexSchema = {
    "@context": "https://schema.org",
    "@graph": [
      {
        "@type": "BreadcrumbList",
        "itemListElement": [
          { "@type": "ListItem", "position": 1, "name": "Trang chủ", "item": "https://isthaison.github.io/gotv/" },
          { "@type": "ListItem", "position": 2, "name": "Câu hỏi thường gặp", "item": "https://isthaison.github.io/gotv/faq/" }
        ]
      },
      {
        "@type": "CollectionPage",
        "url": "https://isthaison.github.io/gotv/faq/",
        "name": "Trung tâm trợ giúp & Câu hỏi thường gặp — GoTV",
        "description": "Danh sách các bài viết giải đáp thắc mắc, hướng dẫn xử lý lỗi khi sử dụng bộ gõ tiếng Việt GoTV.",
        "inLanguage": "vi"
      }
    ]
  };

  const faqIndexHtml = buildHtmlTemplate({
    title: 'Câu hỏi thường gặp (FAQ) — Bộ gõ tiếng Việt GoTV',
    description: 'Tổng hợp toàn bộ câu hỏi thường gặp, hướng dẫn cài đặt, giải đáp thắc mắc và sửa lỗi cho bộ gõ tiếng Việt GoTV.',
    canonical: 'https://isthaison.github.io/gotv/faq/',
    schema: faqIndexSchema,
    bodyContent: faqIndexContent,
    isFaqSubpage: true,
  });

  fs.writeFileSync(path.join(siteDir, 'faq/index.html'), faqIndexHtml, 'utf-8');

  // 5. Render từng trang chi tiết FAQ
  for (const faq of FAQ_ITEMS) {
    console.log(`📄 Đang render bài viết FAQ: ${faq.slug}.html...`);
    const detailContent = renderToStaticMarkup(React.createElement(FaqDetailPage, { faq, basePath: '/gotv/' }));
    const detailSchema = {
      "@context": "https://schema.org",
      "@graph": [
        {
          "@type": "BreadcrumbList",
          "itemListElement": [
            { "@type": "ListItem", "position": 1, "name": "Trang chủ", "item": "https://isthaison.github.io/gotv/" },
            { "@type": "ListItem", "position": 2, "name": "FAQ", "item": "https://isthaison.github.io/gotv/faq/" },
            { "@type": "ListItem", "position": 3, "name": faq.question, "item": `https://isthaison.github.io/gotv/faq/${faq.slug}.html` }
          ]
        },
        {
          "@type": "QAPage",
          "mainEntity": {
            "@type": "Question",
            "name": faq.question,
            "text": faq.shortAnswer,
            "answerCount": 1,
            "acceptedAnswer": {
              "@type": "Answer",
              "text": faq.shortAnswer,
              "url": `https://isthaison.github.io/gotv/faq/${faq.slug}.html#answer`
            }
          }
        }
      ]
    };

    const detailHtml = buildHtmlTemplate({
      title: `${faq.question} — FAQ GoTV`,
      description: faq.shortAnswer,
      canonical: `https://isthaison.github.io/gotv/faq/${faq.slug}.html`,
      schema: detailSchema,
      bodyContent: detailContent,
      isFaqSubpage: true,
    });

    fs.writeFileSync(path.join(siteDir, 'faq', `${faq.slug}.html`), detailHtml, 'utf-8');
  }

  // 6. Cập nhật sitemap.xml
  console.log('🗺️ Đang cập nhật sitemap.xml...');
  const today = new Date().toISOString().split('T')[0];
  const sitemapXml = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"
        xmlns:image="http://www.google.com/schemas/sitemap-image/1.1">
  <url>
    <loc>https://isthaison.github.io/gotv/</loc>
    <lastmod>${today}</lastmod>
    <changefreq>weekly</changefreq>
    <priority>1.0</priority>
    <image:image>
      <image:loc>https://isthaison.github.io/gotv/og-image.png</image:loc>
      <image:title>GoTV — Bộ gõ tiếng Việt Telex/VNI cho Windows, Linux, macOS</image:title>
    </image:image>
  </url>
  <url>
    <loc>https://isthaison.github.io/gotv/privacy.html</loc>
    <lastmod>${today}</lastmod>
    <changefreq>yearly</changefreq>
    <priority>0.5</priority>
  </url>
  <url>
    <loc>https://isthaison.github.io/gotv/faq/</loc>
    <lastmod>${today}</lastmod>
    <changefreq>weekly</changefreq>
    <priority>0.8</priority>
  </url>
${FAQ_ITEMS.map((faq) => `  <url>
    <loc>https://isthaison.github.io/gotv/faq/${faq.slug}.html</loc>
    <lastmod>${today}</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.7</priority>
  </url>`).join('\n')}
</urlset>`;

  fs.writeFileSync(path.join(siteDir, 'sitemap.xml'), sitemapXml, 'utf-8');

  console.log('🎉 Quá trình Static Site Generation (SSG) hoàn tất thành công!');
}

prerender().catch((err) => {
  console.error('❌ Lỗi khi prerender:', err);
  process.exit(1);
});
