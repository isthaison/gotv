// GoTV Engine in TypeScript/JavaScript
// Ported faithfully from engine/charset.c, engine/compose.c, engine/phonology.c, engine/promotion.c, engine/spell.c

export const DIAC_NONE = 0;
export const DIAC_BREVE = 1;       // ă
export const DIAC_CIRCUMFLEX = 2;  // â, ê, ô
export const DIAC_HORN = 3;        // ơ, ư
export const DIAC_STROKE = 4;      // đ

export const TONE_NONE = 0;
export const TONE_SAC = 1;         // sắc
export const TONE_HUYEN = 2;       // huyền
export const TONE_HOI = 3;         // hỏi
export const TONE_NGA = 4;         // ngã
export const TONE_NANG = 5;        // nặng

export type GtvMode = 'telex' | 'vni';

interface CharInfo {
  bare: string;
  diacritic: number;
  tone: number;
  isUpper: boolean;
}

const charInfoMap = new Map<string, CharInfo>();
const reverseMap = new Map<string, string>();

function makeKey(bare: string, dia: number, tone: number, upper: boolean): string {
  return `${bare}:${dia}:${tone}:${upper ? 1 : 0}`;
}

function registerChar(ch: string, bare: string, dia: number, tone: number, upper: boolean) {
  charInfoMap.set(ch, { bare, diacritic: dia, tone, isUpper: upper });
  reverseMap.set(makeKey(bare, dia, tone, upper), ch);
}

// Initialize charset
const lowerGroups: Array<{ bare: string; dia: number; str: string }> = [
  { bare: 'a', dia: DIAC_NONE, str: 'aáàảãạ' },
  { bare: 'a', dia: DIAC_BREVE, str: 'ăắằẳẵặ' },
  { bare: 'a', dia: DIAC_CIRCUMFLEX, str: 'âấầẩẫậ' },
  { bare: 'e', dia: DIAC_NONE, str: 'eéèẻẽẹ' },
  { bare: 'e', dia: DIAC_CIRCUMFLEX, str: 'êếềểễệ' },
  { bare: 'i', dia: DIAC_NONE, str: 'iíìỉĩị' },
  { bare: 'o', dia: DIAC_NONE, str: 'oóòỏõọ' },
  { bare: 'o', dia: DIAC_CIRCUMFLEX, str: 'ôốồổỗộ' },
  { bare: 'o', dia: DIAC_HORN, str: 'ơớờởỡợ' },
  { bare: 'u', dia: DIAC_NONE, str: 'uúùủũụ' },
  { bare: 'u', dia: DIAC_HORN, str: 'ưứừửữự' },
  { bare: 'y', dia: DIAC_NONE, str: 'yýỳỷỹỵ' },
];

const upperGroups: Array<{ bare: string; dia: number; str: string }> = [
  { bare: 'a', dia: DIAC_NONE, str: 'AÁÀẢÃẠ' },
  { bare: 'a', dia: DIAC_BREVE, str: 'ĂẮẰẲẴẶ' },
  { bare: 'a', dia: DIAC_CIRCUMFLEX, str: 'ÂẤẦẨẪẬ' },
  { bare: 'e', dia: DIAC_NONE, str: 'EÉÈẺẼẸ' },
  { bare: 'e', dia: DIAC_CIRCUMFLEX, str: 'ÊẾỀỂỄỆ' },
  { bare: 'i', dia: DIAC_NONE, str: 'IÍÌỈĨỊ' },
  { bare: 'o', dia: DIAC_NONE, str: 'OÓÒỎÕỌ' },
  { bare: 'o', dia: DIAC_CIRCUMFLEX, str: 'ÔỐỒỔỖỘ' },
  { bare: 'o', dia: DIAC_HORN, str: 'ƠỚỜỞỠỢ' },
  { bare: 'u', dia: DIAC_NONE, str: 'UÚÙỦŨỤ' },
  { bare: 'u', dia: DIAC_HORN, str: 'ƯỨỪỬỮỰ' },
  { bare: 'y', dia: DIAC_NONE, str: 'YÝỲỶỸỴ' },
];

for (const g of lowerGroups) {
  const chars = Array.from(g.str);
  for (let t = 0; t < chars.length; t++) {
    registerChar(chars[t], g.bare, g.dia, t, false);
  }
}

for (const g of upperGroups) {
  const chars = Array.from(g.str);
  for (let t = 0; t < chars.length; t++) {
    registerChar(chars[t], g.bare, g.dia, t, true);
  }
}

registerChar('d', 'd', DIAC_NONE, TONE_NONE, false);
registerChar('đ', 'd', DIAC_STROKE, TONE_NONE, false);
registerChar('D', 'd', DIAC_NONE, TONE_NONE, true);
registerChar('Đ', 'd', DIAC_STROKE, TONE_NONE, true);

export function getInfo(c: string): CharInfo | undefined {
  return charInfoMap.get(c);
}

export function lookupChar(bare: string, dia: number, tone: number, upper: boolean): string | undefined {
  return reverseMap.get(makeKey(bare, dia, tone, upper));
}

export function isVowel(c: string): boolean {
  const info = getInfo(c);
  if (!info) return false;
  return info.bare !== 'd';
}

export function toLower(c: string): string {
  return c.toLowerCase();
}

export function isUpper(c: string): boolean {
  return c !== c.toLowerCase();
}

export function getTone(c: string): number {
  const info = getInfo(c);
  return info ? info.tone : TONE_NONE;
}

export function getDiac(c: string): number {
  const info = getInfo(c);
  return info ? info.diacritic : DIAC_NONE;
}

export function bareLower(c: string): string {
  const info = getInfo(c);
  if (info) return info.bare;
  return toLower(c);
}

// Coda and Onset validity
export function isValidCoda(coda: string): boolean {
  if (coda.length === 0) return true;
  if (coda.length === 1) {
    const c = coda[0];
    return c === 'c' || c === 'm' || c === 'n' || c === 'p' || c === 't';
  }
  if (coda.length === 2) {
    return (
      (coda[0] === 'c' && coda[1] === 'h') ||
      (coda[0] === 'n' && coda[1] === 'g') ||
      (coda[0] === 'n' && coda[1] === 'h')
    );
  }
  return false;
}

export function isValidOnset(chars: string[], firstV: number): boolean {
  if (firstV <= 0) return true;
  if (firstV === 1) {
    const c = bareLower(chars[0]);
    if (
      c === 'b' || c === 'c' || c === 'd' || c === 'g' || c === 'h' ||
      c === 'k' || c === 'l' || c === 'm' || c === 'n' || c === 'p' ||
      c === 'r' || c === 's' || c === 't' || c === 'v' || c === 'x' ||
      chars[0] === 'đ' || chars[0] === 'Đ'
    ) return true;
    return false;
  }
  if (firstV === 2) {
    const c0 = bareLower(chars[0]);
    const c1 = bareLower(chars[1]);
    if (c0 === 'c' && c1 === 'h') return true;
    if (c0 === 'g' && c1 === 'h') return true;
    if (c0 === 'g' && c1 === 'i') return true;
    if (c0 === 'k' && c1 === 'h') return true;
    if (c0 === 'n' && c1 === 'g') return true;
    if (c0 === 'n' && c1 === 'h') return true;
    if (c0 === 'p' && c1 === 'h') return true;
    if (c0 === 'q' && c1 === 'u') return true;
    if (c0 === 't' && c1 === 'h') return true;
    if (c0 === 't' && c1 === 'r') return true;
    return false;
  }
  if (firstV === 3) {
    const c0 = bareLower(chars[0]);
    const c1 = bareLower(chars[1]);
    const c2 = bareLower(chars[2]);
    if (c0 === 'n' && c1 === 'g' && c2 === 'h') return true;
    return false;
  }
  return false;
}

function nucleusStart(buf: string[]): number {
  for (let i = 0; i < buf.length; i++) {
    if (!isVowel(buf[i])) continue;
    if (
      i === 1 &&
      bareLower(buf[0]) === 'q' &&
      bareLower(buf[i]) === 'u' &&
      i + 1 < buf.length &&
      isVowel(buf[i + 1])
    ) {
      continue;
    }
    return i;
  }
  return -1;
}

function nucleusEnd(buf: string[]): number {
  for (let i = buf.length - 1; i >= 0; i--) {
    if (isVowel(buf[i])) return i;
  }
  return -1;
}

function syllableEligible(buf: string[]): boolean {
  const first = nucleusStart(buf);
  const last = nucleusEnd(buf);
  if (first < 0) return isValidOnset(buf, buf.length);
  if (!isValidOnset(buf, first)) return false;
  for (let i = first; i <= last; i++) {
    if (!isVowel(buf[i])) return false;
  }
  const codaLen = buf.length - last - 1;
  if (codaLen > 2) return false;
  let coda = '';
  for (let i = 0; i < codaLen; i++) {
    const c = bareLower(buf[last + 1 + i]);
    if (c.charCodeAt(0) > 127) return false;
    coda += c;
  }
  return isValidCoda(coda);
}

export function autoPromoteDiphthong(buf: string[]): void {
  if (buf.length < 3) return;
  const pairs = [
    { first: 'i', last: 'e', tail: 'u', plainFirst: false },
    { first: 'y', last: 'e', tail: 'u', plainFirst: false },
    { first: 'u', last: 'o', tail: 'i', plainFirst: true },
  ];
  let lastV = -1;
  for (let i = buf.length - 1; i >= 0; i--) {
    if (isVowel(buf[i])) {
      lastV = i;
      break;
    }
  }
  for (const pair of pairs) {
    let pos = lastV;
    if (lastV === buf.length - 1) {
      if (bareLower(buf[lastV]) !== pair.tail) continue;
      pos--;
    }
    if (pos < 1) continue;
    const first = buf[pos - 1];
    const last = buf[pos];
    if (
      bareLower(first) !== pair.first ||
      bareLower(last) !== pair.last ||
      getDiac(last) !== DIAC_NONE ||
      (pair.plainFirst && getDiac(first) !== DIAC_NONE)
    ) {
      continue;
    }
    const changed = lookupChar(pair.last, DIAC_CIRCUMFLEX, getTone(last), isUpper(last));
    if (changed) {
      buf[pos] = changed;
      return;
    }
  }
}

export function findTonePosition(word: string[], modern: boolean): number {
  if (!word.length) return -1;
  const positions: number[] = [];
  for (let i = 0; i < word.length; i++) {
    if (isVowel(word[i])) positions.push(i);
  }
  if (!positions.length) return -1;

  if (positions.length > 1 && positions[0] === 1) {
    const onset = bareLower(word[0]);
    const vowel = bareLower(word[1]);
    if ((onset === 'q' && vowel === 'u') || (onset === 'g' && vowel === 'i')) {
      positions.shift();
    }
  }

  const shapePriority = [0, 1, 2, 1, 0];
  let best = 0;
  let result = positions[0];
  for (let i = 0; i < positions.length; i++) {
    const p = positions[i];
    const priority = shapePriority[getDiac(word[p])];
    if (priority && priority >= best) {
      best = priority;
      result = p;
    }
  }

  if (!best && positions.length > 1) {
    const closed = positions[positions.length - 1] < word.length - 1;
    let target = positions.length === 3 ? 1 : 0;
    if (positions.length === 2) {
      const first = bareLower(word[positions[0]]);
      const last = bareLower(word[positions[1]]);
      const pairs = [
        { first: 'o', last: 'a', modernOnly: true },
        { first: 'o', last: 'e', modernOnly: true },
        { first: 'u', last: 'y', modernOnly: true },
        { first: 'i', last: 'e', modernOnly: false },
        { first: 'y', last: 'e', modernOnly: false },
        { first: 'u', last: 'o', modernOnly: false },
        { first: 'o', last: 'o', modernOnly: false },
      ];
      target = closed ? 1 : 0;
      for (const pair of pairs) {
        if (first === pair.first && last === pair.last && (modern || !pair.modernOnly)) {
          target = 1;
        }
      }
    }
    result = positions[target];
  }

  return result;
}

export function removeAllTones(word: string[]): void {
  for (let i = 0; i < word.length; i++) {
    const c = word[i];
    const tone = getTone(c);
    if (tone !== TONE_NONE) {
      const info = getInfo(c);
      if (info) {
        const nc = lookupChar(info.bare, info.diacritic, TONE_NONE, info.isUpper);
        if (nc) word[i] = nc;
      }
    }
  }
}

export function applyToneAt(word: string[], pos: number, tone: number): boolean {
  if (pos < 0 || pos >= word.length) return false;
  const c = word[pos];
  const info = getInfo(c);
  if (!info || info.bare === 'd') return false;
  const nc = lookupChar(info.bare, info.diacritic, tone, info.isUpper);
  if (!nc) return false;
  word[pos] = nc;
  return true;
}

function currentTone(buf: string[]): number {
  for (let i = 0; i < buf.length; i++) {
    const t = getTone(buf[i]);
    if (t) return t;
  }
  return TONE_NONE;
}

function normalizeTone(buf: string[], modern: boolean): void {
  const tone = currentTone(buf);
  if (tone && syllableEligible(buf)) {
    autoPromoteDiphthong(buf);
    removeAllTones(buf);
    applyToneAt(buf, findTonePosition(buf, modern), tone);
  }
}

// Key rules
const OP_TONE = 0;
const OP_SHAPE = 1;
const OP_CLEAR = 2;
const OP_SHORTCUT = 3;
type Operation = 0 | 1 | 2 | 3;

interface KeyRule {
  mode: GtvMode;
  key: string;
  operation: Operation;
  mark: number;
  targets: string | null;
  expansion: string | null;
}

const keyRules: KeyRule[] = [
  { mode: 'telex', key: 's', operation: OP_TONE, mark: TONE_SAC, targets: null, expansion: null },
  { mode: 'telex', key: 'f', operation: OP_TONE, mark: TONE_HUYEN, targets: null, expansion: null },
  { mode: 'telex', key: 'r', operation: OP_TONE, mark: TONE_HOI, targets: null, expansion: null },
  { mode: 'telex', key: 'x', operation: OP_TONE, mark: TONE_NGA, targets: null, expansion: null },
  { mode: 'telex', key: 'j', operation: OP_TONE, mark: TONE_NANG, targets: null, expansion: null },
  { mode: 'telex', key: 'a', operation: OP_SHAPE, mark: DIAC_CIRCUMFLEX, targets: 'a', expansion: null },
  { mode: 'telex', key: 'e', operation: OP_SHAPE, mark: DIAC_CIRCUMFLEX, targets: 'e', expansion: null },
  { mode: 'telex', key: 'o', operation: OP_SHAPE, mark: DIAC_CIRCUMFLEX, targets: 'o', expansion: null },
  { mode: 'telex', key: 'd', operation: OP_SHAPE, mark: DIAC_STROKE, targets: 'd', expansion: null },
  { mode: 'telex', key: 'w', operation: OP_SHAPE, mark: DIAC_HORN, targets: 'uoa', expansion: 'ư' },
  { mode: 'telex', key: 'z', operation: OP_CLEAR, mark: 0, targets: null, expansion: null },
  { mode: 'telex', key: '[', operation: OP_SHORTCUT, mark: 0, targets: null, expansion: 'ươ' },
  { mode: 'telex', key: ']', operation: OP_SHORTCUT, mark: 0, targets: null, expansion: 'ư' },
  { mode: 'telex', key: '{', operation: OP_SHORTCUT, mark: 0, targets: null, expansion: 'ƯƠ' },
  { mode: 'telex', key: '}', operation: OP_SHORTCUT, mark: 0, targets: null, expansion: 'Ư' },

  { mode: 'vni', key: '1', operation: OP_TONE, mark: TONE_SAC, targets: null, expansion: null },
  { mode: 'vni', key: '2', operation: OP_TONE, mark: TONE_HUYEN, targets: null, expansion: null },
  { mode: 'vni', key: '3', operation: OP_TONE, mark: TONE_HOI, targets: null, expansion: null },
  { mode: 'vni', key: '4', operation: OP_TONE, mark: TONE_NGA, targets: null, expansion: null },
  { mode: 'vni', key: '5', operation: OP_TONE, mark: TONE_NANG, targets: null, expansion: null },
  { mode: 'vni', key: '6', operation: OP_SHAPE, mark: DIAC_CIRCUMFLEX, targets: 'aeo', expansion: null },
  { mode: 'vni', key: '7', operation: OP_SHAPE, mark: DIAC_HORN, targets: 'uo', expansion: null },
  { mode: 'vni', key: '8', operation: OP_SHAPE, mark: DIAC_BREVE, targets: 'a', expansion: null },
  { mode: 'vni', key: '9', operation: OP_SHAPE, mark: DIAC_STROKE, targets: 'd', expansion: null },
  { mode: 'vni', key: '0', operation: OP_CLEAR, mark: 0, targets: null, expansion: null },
];

function getKeyRule(mode: GtvMode, key: string): KeyRule | null {
  const lowerKey = key.toLowerCase();
  for (const r of keyRules) {
    if (r.mode === mode && r.key === lowerKey) return r;
  }
  return null;
}

function shortcutMatches(buf: string[], expansion: string | null): boolean {
  if (!expansion || !buf.length) return false;
  const expChars = Array.from(expansion);
  if (buf.length < expChars.length) return false;
  for (let i = 0; i < expChars.length; i++) {
    const b = toLower(buf[buf.length - expChars.length + i]);
    const e = toLower(expChars[i]);
    if (b !== e) return false;
  }
  return true;
}

function shortcutUndo(buf: string[], expansion: string, key: string): boolean {
  const expChars = Array.from(expansion);
  if (buf.length < expChars.length) return false;
  const lastC = buf[buf.length - 1];
  buf.splice(buf.length - expChars.length, expChars.length);
  const outKey = (isUpper(lastC) || isUpper(key)) ? key.toUpperCase() : key;
  buf.push(outKey);
  return true;
}

function shortcutApply(buf: string[], expansion: string, key: string): boolean {
  const expChars = Array.from(expansion);
  const upper = isUpper(key);
  for (const c of expChars) {
    buf.push(upper ? c.toUpperCase() : c);
  }
  return true;
}

function shape(buf: string[], rule: KeyRule, key: string, modern: boolean): boolean {
  const first = nucleusStart(buf);
  const last = nucleusEnd(buf);
  const positions: number[] = [];
  const closed = last >= 0 && last < buf.length - 1;

  if (rule.mark === DIAC_HORN) {
    for (let i = last; i > first; i--) {
      if (bareLower(buf[i - 1]) === 'u' && bareLower(buf[i]) === 'o') {
        positions[0] = i - 1;
        positions[1] = i;
        break;
      }
    }
  }

  if (positions.length === 0) {
    let best = -1;
    for (let i = buf.length - 1; i >= 0; i--) {
      const bare = bareLower(buf[i]);
      if (bare.charCodeAt(0) > 127 || !rule.targets || !rule.targets.includes(bare)) continue;
      if (rule.mark !== DIAC_STROKE && i < first) continue;
      const priority = (rule.mark === DIAC_HORN && rule.targets.includes('a'))
        ? ((bare === 'a') === closed ? 1 : 0)
        : 0;
      if (priority > best) {
        positions[0] = i;
        best = priority;
      }
    }
  }

  if (positions.length === 0) return false;

  let repeat = true;
  const marks: number[] = [];
  for (let i = 0; i < positions.length; i++) {
    const c = buf[positions[i]];
    marks[i] = (rule.mark === DIAC_HORN && bareLower(c) === 'a') ? DIAC_BREVE : rule.mark;
    repeat = repeat && (getDiac(c) === marks[i]);
  }

  for (let i = 0; i < positions.length; i++) {
    const c = buf[positions[i]];
    const changed = lookupChar(bareLower(c), repeat ? DIAC_NONE : marks[i], getTone(c), isUpper(c));
    if (!changed) return false;
    buf[positions[i]] = changed;
  }

  if (repeat) {
    buf.push(key);
  } else {
    normalizeTone(buf, modern);
  }
  return true;
}

export function gtvApplyKey(buf: string[], key: string, mode: GtvMode, modern: boolean): boolean {
  const rule = getKeyRule(mode, key);
  if (!rule || !syllableEligible(buf)) return false;

  if (rule.expansion && shortcutMatches(buf, rule.expansion)) {
    return shortcutUndo(buf, rule.expansion, key);
  }

  if (rule.operation === OP_SHORTCUT && rule.expansion) {
    return shortcutApply(buf, rule.expansion, key);
  }

  if (rule.operation === OP_CLEAR) {
    if (!currentTone(buf)) return false;
    removeAllTones(buf);
    return true;
  }

  if (rule.operation === OP_SHAPE) {
    if (shape(buf, rule, key, modern)) return true;
    if (rule.expansion && nucleusStart(buf) < 0) {
      return shortcutApply(buf, rule.expansion, key);
    }
    return false;
  }

  // Tone accent
  if (nucleusStart(buf) < 0) return false;
  const repeat = currentTone(buf) === rule.mark;
  removeAllTones(buf);
  if (repeat) {
    buf.push(key);
  } else {
    autoPromoteDiphthong(buf);
    applyToneAt(buf, findTonePosition(buf, modern), rule.mark);
  }
  return true;
}

export function gtvCompose(buf: string[], key: string, mode: GtvMode, modern: boolean): void {
  if (!gtvApplyKey(buf, key, mode, modern)) {
    buf.push(key);
    normalizeTone(buf, modern);
  }
}

/**
 * Transforms an input string of keys (e.g. "duocjwd") into Vietnamese using GoTV logic.
 */
export function gtvTransform(input: string, mode: GtvMode = 'telex', modern: boolean = true): string {
  if (!input) return '';
  const buf: string[] = [];
  for (const ch of Array.from(input)) {
    gtvCompose(buf, ch, mode, modern);
  }
  return buf.join('');
}

/**
 * Full text typing processor: processes input text word by word or keystroke by keystroke.
 */
export function gtvProcessText(fullText: string, mode: GtvMode = 'telex', modern: boolean = true): string {
  // Tokenize by word separators
  const parts = fullText.split(/([ \t\r\n.,;:!?'"()[\]{}<>+\-*/=_\\|~`@#$%^&])/);
  return parts.map((part) => {
    // If it's a separator or empty, keep it
    if (!part || /^[ \t\r\n.,;:!?'"()[\]{}<>+\-*/=_\\|~`@#$%^&]+$/.test(part)) {
      return part;
    }
    return gtvTransform(part, mode, modern);
  }).join('');
}

export interface SyllableAnalysis {
  word: string;
  onset: string;
  nucleus: string;
  coda: string;
  toneName: string;
  diacName: string;
}

export function analyzeSyllable(word: string): SyllableAnalysis | null {
  if (!word || word.trim().length === 0) return null;
  const chars = Array.from(word.trim());
  let firstV = -1;
  let lastV = -1;
  let tone = 0;
  const diacs: string[] = [];

  for (let i = 0; i < chars.length; i++) {
    const c = chars[i];
    const t = getTone(c);
    if (t) tone = t;
    const d = getDiac(c);
    if (d === DIAC_BREVE && !diacs.includes('Trăng (ă)')) diacs.push('Trăng (ă)');
    if (d === DIAC_CIRCUMFLEX && !diacs.includes('Mũ (â/ê/ô)')) diacs.push('Mũ (â/ê/ô)');
    if (d === DIAC_HORN && !diacs.includes('Móc (ơ/ư)')) diacs.push('Móc (ơ/ư)');
    if (d === DIAC_STROKE && !diacs.includes('Gạch ngang (đ)')) diacs.push('Gạch ngang (đ)');
    if (isVowel(c)) {
      if (firstV === -1) firstV = i;
      lastV = i;
    }
  }

  const toneNames = ['Không dấu (Ngang)', 'Sắc (´)', 'Huyền (`)', 'Hỏi (?)', 'Ngã (~)', 'Nặng (.)'];

  if (firstV === -1) {
    return {
      word: word.trim(),
      onset: word.trim(),
      nucleus: '-',
      coda: '-',
      toneName: toneNames[tone],
      diacName: diacs.length ? diacs.join(', ') : 'Không có',
    };
  }

  const onset = chars.slice(0, firstV).join('') || '(Trống)';
  const nucleus = chars.slice(firstV, lastV + 1).join('');
  const coda = chars.slice(lastV + 1).join('') || '(Trống)';

  return {
    word: word.trim(),
    onset,
    nucleus,
    coda,
    toneName: toneNames[tone],
    diacName: diacs.length ? diacs.join(', ') : 'Không có',
  };
}
