import React, { useState } from 'react';
import { Sparkles, RotateCcw } from 'lucide-react';

export const VietnamesePlayground: React.FC = () => {
  const [text, setText] = useState('Gõ tiếng Việt cực nhanh với GoTV!');
  const [mode, setMode] = useState<'telex' | 'vni'>('telex');

  const examples = [
    { telex: 'duocjwd', vni: 'du7o7c5', result: 'được' },
    { telex: 'tieengs', vni: 'tie6ng1', result: 'tiếng' },
    { telex: 'vieetj', vni: 'vie6t5', result: 'việt' },
    { telex: 'nguyeenv', vni: 'nguye6n4', result: 'nguyễn' },
  ];

  return (
    <div className="bg-stone-900 text-stone-100 rounded-2xl p-6 shadow-xl border border-stone-800 max-w-2xl mx-auto my-6 text-left">
      <div className="flex items-center justify-between border-b border-stone-800 pb-3 mb-4">
        <div className="flex items-center gap-2">
          <Sparkles className="w-5 h-5 text-amber-400" />
          <span className="font-bold text-sm tracking-wide uppercase text-stone-300">Gõ thử trực tuyến (Live Playground)</span>
        </div>
        <div className="flex gap-2">
          <button
            onClick={() => setMode('telex')}
            className={`px-3 py-1 rounded-lg text-xs font-semibold transition ${
              mode === 'telex' ? 'bg-red-700 text-white' : 'bg-stone-800 text-stone-400 hover:text-white'
            }`}
          >
            Telex
          </button>
          <button
            onClick={() => setMode('vni')}
            className={`px-3 py-1 rounded-lg text-xs font-semibold transition ${
              mode === 'vni' ? 'bg-red-700 text-white' : 'bg-stone-800 text-stone-400 hover:text-white'
            }`}
          >
            VNI
          </button>
        </div>
      </div>

      <div className="mb-4">
        <label className="text-xs text-stone-400 mb-1 block">Khung soạn thảo thử nghiệm:</label>
        <textarea
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder="Nhập thử văn bản tiếng Việt tại đây..."
          rows={3}
          className="w-full bg-stone-950 border border-stone-800 rounded-xl p-3 text-stone-100 placeholder-stone-600 focus:outline-none focus:border-red-600 font-mono text-sm resize-none"
        />
      </div>

      <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-stone-400 pt-1">
        <div className="flex flex-wrap items-center gap-2">
          <span className="text-stone-500">Ví dụ:</span>
          {examples.map((ex, i) => (
            <button
              key={i}
              onClick={() => setText((prev) => (prev ? prev + ' ' : '') + ex.result)}
              className="bg-stone-800 hover:bg-stone-700 text-amber-300 px-2.5 py-1 rounded-md transition font-mono"
            >
              <code>{mode === 'telex' ? ex.telex : ex.vni}</code> &rarr; <strong>{ex.result}</strong>
            </button>
          ))}
        </div>
        <button
          onClick={() => setText('')}
          className="text-stone-500 hover:text-stone-300 flex items-center gap-1 transition text-xs"
          title="Xóa trắng"
        >
          <RotateCcw className="w-3.5 h-3.5" /> Xóa
        </button>
      </div>
    </div>
  );
};
