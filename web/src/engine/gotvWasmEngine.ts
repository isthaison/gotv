import { GOTV_WASM_BASE64 } from './gotvWasmBase64.ts';

export type GtvMode = 'telex' | 'vni';

interface WasmExports {
  memory: WebAssembly.Memory;
  gtv_wasm_init: () => void;
  gtv_wasm_malloc: (size: number) => number;
  gtv_wasm_free: (ptr: number) => void;
  gtv_wasm_transform: (inputPtr: number, mode: number, modern: number) => number;
  gtv_wasm_compose_key: (wordPtr: number, key: number, mode: number, modern: number) => number;
  gtv_wasm_analyze_syllable: (wordPtr: number) => number;
}

let wasmExports: WasmExports | null = null;
let initPromise: Promise<WasmExports> | null = null;

function base64ToUint8Array(b64: string): Uint8Array {
  if (typeof Buffer !== 'undefined') {
    return new Uint8Array(Buffer.from(b64, 'base64'));
  }
  const binaryString = atob(b64);
  const len = binaryString.length;
  const bytes = new Uint8Array(len);
  for (let i = 0; i < len; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }
  return bytes;
}

export function initGotvWasm(): Promise<WasmExports> {
  if (wasmExports) return Promise.resolve(wasmExports);
  if (initPromise) return initPromise;

  initPromise = (async () => {
    const bytes = base64ToUint8Array(GOTV_WASM_BASE64);
    const result = await WebAssembly.instantiate(bytes, {});
    const exports = result.instance.exports as unknown as WasmExports;
    exports.gtv_wasm_init();
    wasmExports = exports;
    return exports;
  })();

  return initPromise;
}

// Ensure instant initialization if possible
initGotvWasm().catch((err) => {
  console.warn('GoTV WASM deferred init:', err);
});

export function isGotvWasmReady(): boolean {
  return wasmExports !== null;
}

/**
 * Transforms an entire word or string using the pure C GoTV WebAssembly engine.
 */
export function gtvWasmTransform(input: string, mode: GtvMode = 'telex', modern: boolean = true): string {
  if (!input) return '';
  if (!wasmExports) return input; // fallback if not ready

  const encoder = new TextEncoder();
  const inputBytes = encoder.encode(input + '\0');
  const ptr = wasmExports.gtv_wasm_malloc(inputBytes.length);

  const mem = new Uint8Array(wasmExports.memory.buffer);
  mem.set(inputBytes, ptr);

  const modeInt = mode === 'vni' ? 1 : 0;
  const modernInt = modern ? 1 : 0;

  const resPtr = wasmExports.gtv_wasm_transform(ptr, modeInt, modernInt);

  // Read null-terminated string from resPtr
  let end = resPtr;
  const updatedMem = new Uint8Array(wasmExports.memory.buffer);
  while (updatedMem[end] !== 0) end++;

  const decoder = new TextDecoder();
  return decoder.decode(updatedMem.subarray(resPtr, end));
}

/**
 * Composes a single key into an existing word fragment using the pure C GoTV WebAssembly engine.
 */
export function gtvWasmCompose(word: string, keyChar: string, mode: GtvMode = 'telex', modern: boolean = true): string {
  if (!keyChar) return word;
  if (!wasmExports) return word + keyChar;

  const encoder = new TextEncoder();
  const wordBytes = encoder.encode((word || '') + '\0');
  const ptr = wasmExports.gtv_wasm_malloc(wordBytes.length);

  const mem = new Uint8Array(wasmExports.memory.buffer);
  mem.set(wordBytes, ptr);

  const key = keyChar.codePointAt(0) || 0;
  const modeInt = mode === 'vni' ? 1 : 0;
  const modernInt = modern ? 1 : 0;

  const resPtr = wasmExports.gtv_wasm_compose_key(ptr, key, modeInt, modernInt);

  let end = resPtr;
  const updatedMem = new Uint8Array(wasmExports.memory.buffer);
  while (updatedMem[end] !== 0) end++;

  const decoder = new TextDecoder();
  return decoder.decode(updatedMem.subarray(resPtr, end));
}

export interface WasmSyllableAnalysis {
  valid: boolean;
  word?: string;
  onset?: string;
  nucleus?: string;
  coda?: string;
  tone?: number;
  toneName?: string;
  diacName?: string;
}

/**
 * Analyzes a Vietnamese syllable directly via the pure C GoTV WebAssembly engine.
 * Returns null if the word is not a valid Vietnamese syllable or contains non-alphabetic characters.
 */
export function gtvWasmAnalyzeSyllable(word: string): WasmSyllableAnalysis | null {
  if (!word || !wasmExports) return null;

  const encoder = new TextEncoder();
  const wordBytes = encoder.encode(word.trim() + '\0');
  const ptr = wasmExports.gtv_wasm_malloc(wordBytes.length);

  const mem = new Uint8Array(wasmExports.memory.buffer);
  mem.set(wordBytes, ptr);

  const resPtr = wasmExports.gtv_wasm_analyze_syllable(ptr);

  let end = resPtr;
  const updatedMem = new Uint8Array(wasmExports.memory.buffer);
  while (updatedMem[end] !== 0) end++;

  const decoder = new TextDecoder();
  const jsonStr = decoder.decode(updatedMem.subarray(resPtr, end));
  try {
    const parsed = JSON.parse(jsonStr) as WasmSyllableAnalysis;
    return parsed.valid ? parsed : null;
  } catch {
    return null;
  }
}
