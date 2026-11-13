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

  // 3. Typo correction fallback when no prefix matches exist
  if (results.length === 0) {
    return correctTypos(trimmed, lowerPrefix, foldedPrefix, maxCount);
  }

  return results;
}

function popcount32(x: number): number {
  x = x - ((x >>> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >>> 2) & 0x33333333);
  return (((x + (x >>> 4)) & 0x0f0f0f0f) * 0x01010101) >>> 24;
}

function editDistanceCap(a: string, b: string, cap: number): number {
  const na = a.length;
  const nb = b.length;
  if (Math.abs(na - nb) > cap) return cap + 1;

  let prev2 = new Array<number>(nb + 1).fill(0);
  let prev = new Array<number>(nb + 1);
  for (let j = 0; j <= nb; j++) prev[j] = j;

  for (let i = 1; i <= na; i++) {
    const cur = new Array<number>(nb + 1);
    cur[0] = i;
    let rowMin = cur[0];
    for (let j = 1; j <= nb; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      let m = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
      if (i > 1 && j > 1 && a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) {
        const trans = prev2[j - 2] + 1;
        if (trans < m) m = trans;
      }
      cur[j] = m;
      if (m < rowMin) rowMin = m;
    }
    if (rowMin > cap) return cap + 1;
    prev2 = prev;
    prev = cur;
  }
  return prev[nb];
}

function vnOnsetRank(a: string, b: string): number {
  if (!a || !b) return 2;
  if (a[0] === b[0]) return 0;
  const a0 = a[0];
  const b0 = b[0];
  if (
    ('ckq'.includes(a0) && 'ckq'.includes(b0)) ||
    ('sx'.includes(a0) && 'sx'.includes(b0)) ||
    ('ln'.includes(a0) && 'ln'.includes(b0)) ||
    ('dgr'.includes(a0) && 'dgr'.includes(b0)) ||
    (a.startsWith('tr') && b.startsWith('ch')) ||
    (a.startsWith('ch') && b.startsWith('tr'))
  ) {
    return 1;
  }
  return 2;
}

function isIyEquiv(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diffIy = false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) {
      if ((a[i] === 'i' && b[i] === 'y') || (a[i] === 'y' && b[i] === 'i')) {
        diffIy = true;
      } else {
        return false;
      }
    }
  }
  return diffIy;
}

function computeMarkAffinity(typedLower: string, candLower: string): number {
  let score = 0;
  for (const chA of typedLower) {
    if (chA.charCodeAt(0) < 0x80) continue;
    const baseA = foldAccents(chA);
    for (const chB of candLower) {
      if (foldAccents(chB) !== baseA) continue;
      if (chA === chB) score += 3;
      else score += 1;
      break;
    }
  }
  return score;
}

function correctTypos(
  trimmed: string,
  lowerPrefix: string,
  foldedPrefix: string,
  maxCount: number
): SuggestionItem[] {
  const wantLen = foldedPrefix.length;
  const maxDist = wantLen <= 2 ? 1 : 2;
  const wantMask = foldedAsciiMask(foldedPrefix);
  const hasNonAscii = lowerPrefix !== foldedPrefix;

  interface Cand {
    item: IndexedWord;
    dist: number;
    iyBonus: number;
    firstRank: number;
    markAff: number;
    commonBonus: number;
    lenDiff: number;
    prefixMatch: number;
  }

  const cands: Cand[] = [];
  let hasDistZero = false;

  for (let i = 0; i < INDEXED_DICT.length; i++) {
    const item = INDEXED_DICT[i];
    if (item.lower === lowerPrefix) continue;
    const lenDiff = Math.abs(item.folded.length - wantLen);
    if (lenDiff > maxDist) continue;
    if (popcount32((item.asciiMask ^ wantMask) >>> 0) > maxDist * 2) continue;

    const d = editDistanceCap(foldedPrefix, item.folded, maxDist);
    if (d <= maxDist) {
      if (d === 0) hasDistZero = true;
      let pfx = 0;
      while (pfx < wantLen && pfx < item.folded.length && foldedPrefix[pfx] === item.folded[pfx]) {
        pfx++;
      }
      cands.push({
        item,
        dist: d,
        iyBonus: d === 1 && isIyEquiv(foldedPrefix, item.folded) ? 1 : 0,
        firstRank: vnOnsetRank(foldedPrefix, item.folded),
        markAff: hasNonAscii ? computeMarkAffinity(lowerPrefix, item.lower) : 0,
        commonBonus: COMMON_PRIORITY.has(item.lower) ? 1 : 0,
        lenDiff,
        prefixMatch: pfx,
      });
    }
  }

  cands.sort((a, b) => {
    if (a.dist !== b.dist) return a.dist - b.dist;
    if (a.iyBonus !== b.iyBonus) return b.iyBonus - a.iyBonus;
    if (a.firstRank !== b.firstRank) return a.firstRank - b.firstRank;
    if (a.markAff !== b.markAff) return b.markAff - a.markAff;
    if (a.commonBonus !== b.commonBonus) return b.commonBonus - a.commonBonus;
    if (a.lenDiff !== b.lenDiff) return a.lenDiff - b.lenDiff;
    if (a.prefixMatch !== b.prefixMatch) return b.prefixMatch - a.prefixMatch;
    return a.item.lower.localeCompare(b.item.lower);
  });

  const results: SuggestionItem[] = [];
  const seen = new Set<string>();
  for (const c of cands) {
    if (hasNonAscii && hasDistZero && c.dist > 0) continue;
    if (seen.has(c.item.lower)) continue;
    seen.add(c.item.lower);
    const cased = matchCase(trimmed, c.item.raw);
    results.push({
      word: cased,
      display: cased,
      kind: 'correction',
    });
    if (results.length >= maxCount) break;
  }
  return results;
}
