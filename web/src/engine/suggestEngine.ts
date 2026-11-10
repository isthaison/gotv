import { VI_DICTIONARY } from '../data/viDict';
import { EMOJIS_DATA } from '../data/emojisData';

export interface SuggestionItem {
  word: string;
  display: string;
  kind: 'word' | 'emoji' | 'correction';
}

/**
 * Remove Vietnamese accents/diacritics for prefix and fuzzy matching.
 */
export function foldAccents(str: string): string {
  return str
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/đ/g, 'd')
    .replace(/Đ/g, 'D');
}

// Pre-build index for fast prefix lookups
interface IndexedWord {
  raw: string;
  lower: string;
  folded: string;
}

const INDEXED_DICT: IndexedWord[] = VI_DICTIONARY.map((w) => ({
  raw: w,
  lower: w.toLowerCase(),
  folded: foldAccents(w.toLowerCase()),
}));

// Common high-frequency Vietnamese full words for suggestion priority boost
const COMMON_PRIORITY = new Set([
  'được', 'tiếng', 'việt', 'người', 'không', 'những', 'trong', 'thành',
  'thuyền', 'nguyễn', 'hoặc', 'này', 'cho', 'với', 'khi', 'nhiều',
  'chúng', 'cũng', 'đã', 'sẽ', 'phải', 'biết', 'làm', 'toán', 'hoạt',
  'là', 'của', 'có', 'và', 'các', 'về', 'một', 'đến', 'để', 'từ',
  'thì', 'ra', 'vào', 'lại', 'ở', 'theo', 'nói', 'đang', 'như',
  'sau', 'còn', 'lên', 'đi', 'đó', 'tôi', 'bạn', 'em', 'anh', 'ông',
  'bà', 'cô', 'họ', 'nó', 'mình', 'ai', 'gì', 'nào', 'đây', 'đâu',
  'thế', 'rất', 'quá', 'hơn', 'nhất', 'chỉ', 'mới', 'vẫn', 'đều',
  'qua', 'trước', 'ngay', 'luôn', 'rồi', 'cùng', 'khác', 'mỗi',
  'mọi', 'tất', 'cả', 'nếu', 'vì', 'do', 'nhưng', 'tuy', 'nên'
]);

/**
 * Match casing pattern from source prefix to target word
 */
function matchCase(source: string, target: string): string {
  if (!source || !target) return target;
  if (source === source.toUpperCase() && source.length > 1) {
    return target.toUpperCase();
  }
  if (source[0] === source[0].toUpperCase()) {
    return target.charAt(0).toUpperCase() + target.slice(1);
  }
  return target;
}

/**
 * Get suggestions for the current word being typed
 */
export function getSuggestions(prefix: string, maxCount = 6): SuggestionItem[] {
  const trimmed = prefix.trim();
  if (!trimmed) return [];

  // 1. Emoji lookup: :smile:, :d, :), etc.
  if (
    trimmed.startsWith(':') ||
    trimmed.startsWith(';') ||
    trimmed.startsWith('<') ||
    trimmed.startsWith('(')
  ) {
    const q = trimmed.toLowerCase();
    const cleanQ = q.replace(/^:+|:+$/g, '');

    const hits = EMOJIS_DATA.filter((e) => {
      const c = e.code.toLowerCase();
      const cleanC = c.replace(/^:+|:+$/g, '');
      return (
        c.startsWith(q) ||
        cleanC.startsWith(cleanQ) ||
        c.includes(q)
      );
    });

    const scoreEmoji = (code: string) => {
      const c = code.toLowerCase();
      const cleanC = c.replace(/^:+|:+$/g, '');
      let score = 0;

      // 1. Exact match e.g. :d === :d (:D) or :smile: === :smile:
      if (c === q) {
        score += 20000;
      } else if (c === q + ':') {
        score += 15000;
      } else if (cleanC === cleanQ) {
        score += 12000;
      } else if (c.startsWith(q)) {
        score += 8000;
      } else if (cleanC.startsWith(cleanQ)) {
        score += 5000;
      } else {
        score += 1000;
      }

      // Shorter emoji codes always rank ahead of longer ones (e.g. :dog: before :dislike:)
      score -= c.length * 30;
      return score;
    };

    hits.sort((a, b) => scoreEmoji(b.code) - scoreEmoji(a.code));

    return hits.slice(0, maxCount).map((e) => ({
      word: e.char,
      display: `${e.char} ${e.code}`,
      kind: 'emoji',
    }));
  }

  // 2. Vietnamese word completion
  const lowerPrefix = trimmed.toLowerCase();
  const foldedPrefix = foldAccents(lowerPrefix);

  if (foldedPrefix.length < 1) return [];

  const exactMatches: IndexedWord[] = [];
  const foldedMatches: IndexedWord[] = [];

  for (let i = 0; i < INDEXED_DICT.length; i++) {
    const item = INDEXED_DICT[i];
    if (item.lower.startsWith(lowerPrefix)) {
      exactMatches.push(item);
    } else if (item.folded.startsWith(foldedPrefix)) {
      foldedMatches.push(item);
    }
  }

  // Sorter: prioritize exact matches, common words, and shorter words
  const scoreWord = (item: IndexedWord) => {
    let score = 0;
    if (item.lower === lowerPrefix) score += 5000;
    else if (item.folded === foldedPrefix) score += 3000;

    if (COMMON_PRIORITY.has(item.lower)) score += 600;

    // Shorter words rank earlier
    score -= item.raw.length * 15;
    return score;
  };

  exactMatches.sort((a, b) => scoreWord(b) - scoreWord(a));
  foldedMatches.sort((a, b) => scoreWord(b) - scoreWord(a));

  const seen = new Set<string>();
  const results: SuggestionItem[] = [];

  // Add exact matches first
  for (const item of exactMatches) {
    if (!seen.has(item.lower)) {
      seen.add(item.lower);
      const cased = matchCase(trimmed, item.raw);
      results.push({
        word: cased,
        display: cased,
        kind: 'word',
      });
      if (results.length >= maxCount) return results;
    }
  }

  // Fill up with folded matches
  for (const item of foldedMatches) {
    if (!seen.has(item.lower)) {
      seen.add(item.lower);
      const cased = matchCase(trimmed, item.raw);
      results.push({
        word: cased,
        display: cased,
        kind: 'word',
      });
      if (results.length >= maxCount) return results;
    }
  }

  return results;
}
