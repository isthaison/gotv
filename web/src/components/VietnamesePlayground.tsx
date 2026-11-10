import React, { useState, useRef, useEffect, useCallback } from 'react';
import {
  Sparkles,
  RotateCcw,
  Copy,
  Check,
  Play,
  Sliders,
  Keyboard,
  Info,
  CheckCircle2,
} from 'lucide-react';
import {
  gtvCompose,
  gtvTransform,
  analyzeSyllable,
  SyllableAnalysis,
  GtvMode,
} from '../engine/gtvEngine';
import {
  gtvWasmCompose,
  gtvWasmTransform,
  isGotvWasmReady,
  initGotvWasm,
} from '../engine/gotvWasmEngine';

export const VietnamesePlayground: React.FC = () => {
  const [text, setText] = useState('Gõ tiếng Việt cực nhanh với GoTV! Hãy gõ thử: duocjwd -> được');
  const [mode, setMode] = useState<GtvMode>('telex');
  const [modern, setModern] = useState<boolean>(true);
  const [enabled, setEnabled] = useState<boolean>(true);
  const [copied, setCopied] = useState<boolean>(false);
  const [isSimulating, setIsSimulating] = useState<boolean>(false);
  const [analysis, setAnalysis] = useState<SyllableAnalysis | null>(() =>
    analyzeSyllable('được')
  );

  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const simulationTimeoutRef = useRef<number | null>(null);

  // Update analysis when text changes or cursor moves
  const updateCurrentWordAnalysis = useCallback((currentText: string, cursorIndex: number) => {
    // Find the word at or just before the cursor
    let start = cursorIndex;
    while (start > 0 && !/[ \t\r\n.,;:!?'"()[\]{}<>+\-*/=_\\|~`@#$%^&]/.test(currentText[start - 1])) {
      start--;
    }
    let end = cursorIndex;
    while (end < currentText.length && !/[ \t\r\n.,;:!?'"()[\]{}<>+\-*/=_\\|~`@#$%^&]/.test(currentText[end])) {
      end++;
    }
    const word = currentText.substring(start, end).trim();
    if (word) {
      setAnalysis(analyzeSyllable(word));
    }
  }, []);

  // Handle keystroke interception in the textarea
  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (!enabled || isSimulating) return;

    // We only intercept single printable characters
    if (e.key.length !== 1 || e.ctrlKey || e.altKey || e.metaKey) {
      return;
    }

    const key = e.key;
    const target = e.currentTarget;
    const startPos = target.selectionStart;
    const endPos = target.selectionEnd;

    // Only handle collapsed selection
    if (startPos !== endPos) return;

    const val = target.value;

    // Find the word boundary before the cursor
    let wordStart = startPos;
    while (wordStart > 0) {
      const prevChar = val[wordStart - 1];
      const isWordChar = !/[ \t\r\n.,;:!?'"()[\]{}<>+\-*/=_\\|~`@#$%^&]/.test(prevChar);
      if (isWordChar) {
        wordStart--;
      } else {
        break;
      }
    }

    const currentWord = val.substring(wordStart, startPos);
    // Apply GoTV WebAssembly core algorithm directly from C!
    const transformedWord = isGotvWasmReady()
      ? gtvWasmCompose(currentWord, key, mode, modern)
      : (() => {
          const wordBuf = Array.from(currentWord);
          gtvCompose(wordBuf, key, mode, modern);
          return wordBuf.join('');
        })();

    // If composition resulted in the same as standard appending, let browser handle or replace
    e.preventDefault();

    const newVal = val.substring(0, wordStart) + transformedWord + val.substring(startPos);
    setText(newVal);

    const newCursor = wordStart + transformedWord.length;
    setTimeout(() => {
      if (textareaRef.current) {
        textareaRef.current.selectionStart = newCursor;
        textareaRef.current.selectionEnd = newCursor;
        updateCurrentWordAnalysis(newVal, newCursor);
      }
    }, 0);
  };

  const handleTextChange = (e: React.ChangeEvent<HTMLTextAreaElement>) => {
    const newVal = e.target.value;
    setText(newVal);
    updateCurrentWordAnalysis(newVal, e.target.selectionStart || newVal.length);
  };

  const handleSelect = (e: React.SyntheticEvent<HTMLTextAreaElement>) => {
    const target = e.currentTarget;
    updateCurrentWordAnalysis(target.value, target.selectionStart);
  };

  const handleCopy = () => {
    if (!navigator.clipboard) return;
    navigator.clipboard.writeText(text);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const handleClear = () => {
    setText('');
    setAnalysis(null);
    if (textareaRef.current) {
      textareaRef.current.focus();
    }
  };

  const handleReset = () => {
    const defaultSentence = 'Gõ tiếng Việt cực nhanh với GoTV! Hãy gõ thử: duocjwd -> được';
    setText(defaultSentence);
    setAnalysis(analyzeSyllable('được'));
    if (textareaRef.current) {
      textareaRef.current.focus();
    }
  };

  // Play an automated key-by-key typing simulation
  const simulateTyping = (rawKeys: string) => {
    if (isSimulating) return;
    setIsSimulating(true);

    if (simulationTimeoutRef.current) {
      clearTimeout(simulationTimeoutRef.current);
    }

    // Append a space if not empty
    const baseText = text ? text + (text.endsWith(' ') ? '' : ' ') : '';
    setText(baseText);

    const keysArray = Array.from(rawKeys);
    let buf: string[] = [];
    let i = 0;

    const typeNextKey = () => {
      if (i >= keysArray.length) {
        setIsSimulating(false);
        updateCurrentWordAnalysis(baseText + buf.join(''), (baseText + buf.join('')).length);
        if (textareaRef.current) {
          textareaRef.current.focus();
          const len = (baseText + buf.join('')).length;
          textareaRef.current.selectionStart = len;
          textareaRef.current.selectionEnd = len;
        }
        return;
      }

      const k = keysArray[i];
      if (isGotvWasmReady()) {
        const nextWord = gtvWasmCompose(buf.join(''), k, mode, modern);
        buf = Array.from(nextWord);
      } else {
        gtvCompose(buf, k, mode, modern);
      }
      i++;

      const currentVal = baseText + buf.join('');
      setText(currentVal);
      updateCurrentWordAnalysis(currentVal, currentVal.length);

      simulationTimeoutRef.current = window.setTimeout(typeNextKey, 140);
    };

    simulationTimeoutRef.current = window.setTimeout(typeNextKey, 80);
  };

  useEffect(() => {
    return () => {
      if (simulationTimeoutRef.current) {
        clearTimeout(simulationTimeoutRef.current);
      }
    };
  }, []);

  const telexExamples = [
    { raw: 'duocjwd', label: 'được', desc: 'Dấu tự do cuối từ' },
    { raw: 'tieengs', label: 'tiếng', desc: 'Âm đôi ie + s' },
    { raw: 'vieetj', label: 'việt', desc: 'Âm ee + j' },
    { raw: 'nguyeenv', label: 'nguyễn', desc: 'Dấu ngã v/x' },
    { raw: 'thuyeern', label: 'thuyền', desc: 'Âm uyên phức tạp' },
    { raw: 'hoawcj', label: 'hoặc', desc: 'Âm oặc' },
    { raw: 'toanss', label: 'toans', desc: 'Khôi phục tiếng Anh' },
  ];

  const vniExamples = [
    { raw: 'duoc579', label: 'được', desc: 'VNI tự do cuối từ' },
    { raw: 'tie6ng1', label: 'tiếng', desc: '6 mũ, 1 sắc' },
    { raw: 'vie6t5', label: 'việt', desc: '6 mũ, 5 nặng' },
    { raw: 'nguye6n4', label: 'nguyễn', desc: '4 ngã' },
    { raw: 'thuye6n2', label: 'thuyền', desc: '2 huyền' },
    { raw: 'hoa8c5', label: 'hoặc', desc: '8 trăng, 5 nặng' },
    { raw: 'toan11', label: 'toan1', desc: 'Lặp phím khôi phục' },
  ];

  const examples = mode === 'telex' ? telexExamples : vniExamples;

  return (
    <div className="bg-stone-900 text-stone-100 rounded-3xl p-6 sm:p-7 shadow-2xl border border-stone-800 max-w-3xl mx-auto my-8 text-left transition-all">
      {/* Top Header */}
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-stone-800 pb-4 mb-5">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-red-700/20 border border-red-600/40 flex items-center justify-center text-red-500 font-black text-sm">
            {mode === 'telex' ? 'T' : 'V'}
          </div>
          <div>
            <h3 className="font-bold text-sm tracking-wide uppercase text-stone-200 flex items-center gap-2">
              <span>Gõ thử trực tuyến (Live Demo)</span>
              <span className="inline-flex items-center gap-1 text-[11px] font-normal lowercase bg-emerald-950 text-emerald-400 px-2 py-0.5 rounded-full border border-emerald-800">
                <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse"></span>
                lõi C trực tiếp qua WebAssembly (18KB)
              </span>
            </h3>
            <p className="text-xs text-stone-400">
              Gõ phím trực tiếp vào khung dưới — hoạt động 100% không cần cài thêm bộ gõ.
            </p>
          </div>
        </div>

        {/* Mode & Switch Controls */}
        <div className="flex flex-wrap items-center gap-2">
          {/* Method Selector */}
          <div className="inline-flex bg-stone-950 rounded-xl p-1 border border-stone-800 text-xs">
            <button
              onClick={() => setMode('telex')}
              className={`px-3 py-1 rounded-lg font-bold transition ${
                mode === 'telex'
                  ? 'bg-red-700 text-white shadow-sm'
                  : 'text-stone-400 hover:text-stone-200'
              }`}
              title="Kiểu gõ Telex: s, f, r, x, j, aa, oo, ee, dd, w..."
            >
              Telex
            </button>
            <button
              onClick={() => setMode('vni')}
              className={`px-3 py-1 rounded-lg font-bold transition ${
                mode === 'vni'
                  ? 'bg-red-700 text-white shadow-sm'
                  : 'text-stone-400 hover:text-stone-200'
              }`}
              title="Kiểu gõ VNI: 1, 2, 3, 4, 5, 6, 7, 8, 9, 0..."
            >
              VNI
            </button>
          </div>

          {/* Tone Style Option */}
          <button
            onClick={() => setModern(!modern)}
            className="px-2.5 py-1 rounded-xl text-xs bg-stone-950 border border-stone-800 hover:border-stone-700 text-stone-300 transition flex items-center gap-1.5"
            title="Đổi kiểu đặt dấu: Mới (hòa, thúy) hoặc Truyền thống (hoà, thuý)"
          >
            <Sliders className="w-3 h-3 text-amber-400" />
            <span>{modern ? 'Dấu mới (hòa)' : 'Dấu cũ (hoà)'}</span>
          </button>

          {/* Engine Power Toggle */}
          <button
            onClick={() => setEnabled(!enabled)}
            className={`px-2.5 py-1 rounded-xl text-xs font-semibold border transition flex items-center gap-1.5 ${
              enabled
                ? 'bg-stone-800 border-stone-700 text-emerald-400'
                : 'bg-stone-950 border-stone-800 text-stone-500 hover:text-stone-300'
            }`}
            title="Bật/Tắt gõ tiếng Việt"
          >
            <span className={`w-2 h-2 rounded-full ${enabled ? 'bg-emerald-400' : 'bg-stone-600'}`}></span>
            <span>{enabled ? 'Tiếng Việt' : 'Tiếng Anh'}</span>
          </button>
        </div>
      </div>

      {/* Editor Textarea */}
      <div className="relative mb-4">
        <textarea
          ref={textareaRef}
          value={text}
          onKeyDown={handleKeyDown}
          onChange={handleTextChange}
          onSelect={handleSelect}
          placeholder="Nhấp vào đây và bắt đầu gõ thử (ví dụ: duocjwd -> được)..."
          rows={4}
          disabled={isSimulating}
          className="w-full bg-stone-950 border border-stone-800 rounded-2xl p-4 text-stone-100 placeholder-stone-600 focus:outline-none focus:border-red-600 focus:ring-1 focus:ring-red-600 font-mono text-base leading-relaxed resize-none shadow-inner transition"
        />

        {/* Text Actions Floating in Editor */}
        <div className="absolute right-3 bottom-3 flex items-center gap-1.5 bg-stone-900/90 backdrop-blur-sm px-2 py-1 rounded-xl border border-stone-800 text-xs">
          <button
            onClick={handleCopy}
            className="p-1.5 rounded-lg text-stone-400 hover:text-stone-100 hover:bg-stone-800 transition flex items-center gap-1"
            title="Sao chép văn bản"
          >
            {copied ? (
              <>
                <Check className="w-3.5 h-3.5 text-emerald-400" />
                <span className="text-emerald-400 text-[11px] font-semibold">Đã chép</span>
              </>
            ) : (
              <>
                <Copy className="w-3.5 h-3.5" />
                <span className="text-[11px]">Chép</span>
              </>
            )}
          </button>

          <span className="w-px h-3 bg-stone-800"></span>

          <button
            onClick={handleClear}
            className="p-1.5 rounded-lg text-stone-400 hover:text-red-400 hover:bg-stone-800 transition flex items-center gap-1"
            title="Xóa văn bản"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            <span className="text-[11px]">Xóa</span>
          </button>
        </div>
      </div>

      {/* Syllable Inspector & Phonology Analysis */}
      {analysis && (
        <div className="bg-stone-950/80 rounded-2xl p-3.5 mb-4 border border-stone-800/80 text-xs text-stone-300">
          <div className="flex items-center justify-between pb-2 mb-2 border-b border-stone-800/60">
            <span className="font-semibold text-stone-400 flex items-center gap-1.5">
              <Info className="w-3.5 h-3.5 text-amber-400" />
              <span>Phân tích âm tiết GoTV cho từ:</span>
              <strong className="text-amber-300 font-mono text-sm">"{analysis.word}"</strong>
            </span>
            <span className="text-[11px] text-stone-500">Thuật toán ngữ âm lõi C</span>
          </div>

          <div className="grid grid-cols-2 sm:grid-cols-4 gap-2 text-[11px]">
            <div className="bg-stone-900/80 rounded-lg p-2 border border-stone-800/50">
              <span className="text-stone-500 block">Âm đầu:</span>
              <span className="font-mono font-bold text-stone-200">{analysis.onset}</span>
            </div>
            <div className="bg-stone-900/80 rounded-lg p-2 border border-stone-800/50">
              <span className="text-stone-500 block">Nguyên âm chính:</span>
              <span className="font-mono font-bold text-red-400">{analysis.nucleus}</span>
            </div>
            <div className="bg-stone-900/80 rounded-lg p-2 border border-stone-800/50">
              <span className="text-stone-500 block">Âm cuối (coda):</span>
              <span className="font-mono font-bold text-stone-200">{analysis.coda}</span>
            </div>
            <div className="bg-stone-900/80 rounded-lg p-2 border border-stone-800/50">
              <span className="text-stone-500 block">Thanh điệu:</span>
              <span className="font-mono font-bold text-amber-300">{analysis.toneName}</span>
            </div>
          </div>
        </div>
      )}

      {/* Quick Demo Simulator Buttons */}
      <div>
        <div className="flex items-center justify-between mb-2 text-xs">
          <span className="text-stone-400 flex items-center gap-1.5 font-medium">
            <Keyboard className="w-3.5 h-3.5 text-red-500" />
            <span>Thử nhanh các từ tiêu biểu (Bấm để xem mô phỏng gõ từng phím):</span>
          </span>
          <button
            onClick={handleReset}
            className="text-stone-500 hover:text-stone-300 text-[11px] transition"
          >
            Đặt lại câu mẫu
          </button>
        </div>

        <div className="flex flex-wrap items-center gap-2">
          {examples.map((ex, i) => (
            <button
              key={i}
              onClick={() => simulateTyping(ex.raw)}
              disabled={isSimulating}
              className="group bg-stone-950 hover:bg-stone-800 border border-stone-800 hover:border-amber-500/50 rounded-xl px-3 py-1.5 text-xs transition flex items-center gap-2 shadow-sm"
              title={`${ex.desc} — Nhấp để mô phỏng gõ phím`}
            >
              <span className="font-mono text-amber-300/90 group-hover:text-amber-200">{ex.raw}</span>
              <span className="text-stone-600 group-hover:text-stone-400">&rarr;</span>
              <strong className="text-stone-100 group-hover:text-white font-bold">{ex.label}</strong>
              <Play className="w-2.5 h-2.5 text-stone-500 group-hover:text-amber-400 ml-0.5 opacity-60 group-hover:opacity-100 transition" />
            </button>
          ))}
        </div>
      </div>
    </div>
  );
};
