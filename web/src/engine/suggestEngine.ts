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

// Common high-frequency full word priority boost
const COMMON_PRIORITY = new Set([
  'được', 'tiếng', 'việt', 'người', 'không', 'những', 'trong', 'thành',
  'thuyền', 'nguyễn', 'hoặc', 'này', 'cho', 'với', 'khi', 'nhiều',
  'chúng', 'cũng', 'đã', 'sẽ', 'phải', 'biết', 'làm', 'toán', 'hoạt'
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

  // 1. Emoji lookup: :smile:
  if (trimmed.startsWith(':')) {
    const q = trimmed.toLowerCase();
    const hits = EMOJIS_DATA.filter((e) => e.code.toLowerCase().startsWith(q));
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

  // Sorter: prioritize common words first, then shorter words
  const scoreWord = (item: IndexedWord) => {
    let score = 0;
    if (COMMON_PRIORITY.has(item.lower)) score += 100;
    if (item.lower === lowerPrefix || item.folded === foldedPrefix) score += 50;
    score -= item.raw.length;
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
