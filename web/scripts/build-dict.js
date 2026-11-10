// Generates TypeScript dictionary modules from data/dict-vi.txt and data/emojis.txt
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const rootDir = path.resolve(__dirname, '../..');

const viPath = path.join(rootDir, 'data/dict-vi.txt');
const emojisPath = path.join(rootDir, 'data/emojis.txt');

const viWords = fs.readFileSync(viPath, 'utf8')
  .split(/\r?\n/)
  .map((w) => w.trim())
  .filter((w) => w && !w.startsWith('#'));

const emojis = fs.readFileSync(emojisPath, 'utf8')
  .split(/\r?\n/)
  .map((l) => l.trim())
  .filter((l) => l && !l.startsWith('#'))
  .map((l) => {
    const parts = l.split('=');
    return { code: parts[0], char: parts[1] };
  })
  .filter((e) => e.code && e.char);

const outDir = path.resolve(__dirname, '../src/data');
if (!fs.existsSync(outDir)) {
  fs.mkdirSync(outDir, { recursive: true });
}

fs.writeFileSync(
  path.join(outDir, 'viDict.ts'),
  `export const VI_DICTIONARY: string[] = ${JSON.stringify(viWords)};\n`
);

fs.writeFileSync(
  path.join(outDir, 'emojisData.ts'),
  `export const EMOJIS_DATA: { code: string; char: string }[] = ${JSON.stringify(emojis)};\n`
);

console.log(`✅ Dictionary built: ${viWords.length} words, ${emojis.length} emojis`);
