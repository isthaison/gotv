import { gtvTransform, autoPromoteDiphthong } from './gtvEngine.ts';

const telexCases = [
  ["as", "á"],
  ["af", "à"],
  ["ar", "ả"],
  ["ax", "ã"],
  ["aj", "ạ"],
  ["hienr", "hiển"],
  ["hienj", "hiện"],
  ["vietj", "việt"],
  ["muons", "muốn"],
  ["cuocj", "cuộc"],
  ["tieur", "tiểu"],
  ["chuois", "chuối"],
  ["dd", "đ"],
  ["ddd", "dd"],
  ["reddit", "reddit"],
  ["aa", "â"],
  ["aaa", "aa"],
  ["ee", "ê"],
  ["eee", "ee"],
  ["meet", "mêt"],
  ["meeet", "meet"],
  ["oo", "ô"],
  ["ooo", "oo"],
  ["book", "bôk"],
  ["boook", "book"],
  ["w", "ư"],
  ["ww", "w"],
  ["www", "ww"],
  ["wwww", "www"],
  ["uw", "ư"],
  ["ow", "ơ"],
  ["oww", "ow"],
  ["showw", "showw"],
  ["aw", "ă"],
  ["aww", "aw"],
  ["raww", "raw"],
  ["sw", "sư"],
  ["sww", "sw"],
  ["pass", "pas"],
  ["buff", "buf"],
  ["kiss", "kis"],
  ["boss", "bos"],
  ["toanss", "toans"],
  ["phaps", "pháp"],
  ["phasp", "pháp"],
  ["toans", "toán"],
  ["toasn", "toán"],
  ["hoacw", "hoăc"],
  ["hoacwj", "hoặc"],
  ["password", "pasword"],
  ["passw", "pasw"],
  ["class", "class"],
  ["test", "tét"],
  ["post", "pót"],
  ["fast", "fast"],
  ["fish", "fish"],
  ["code", "code"],
  ["game", "game"],
  ["[", "ươ"],
  ["[[", "["],
  ["]", "ư"],
  ["]]", "]"],
  ["duocwjd", "được"],
  ["duocjwd", "được"],
  ["bawst", "bắt"],
  ["bawts", "bắt"],
  ["BAWST", "BẮT"],
  ["caast", "cất"],
  ["coost", "cốt"],
  ["bowst", "bớt"],
  ["task", "ták"],
  ["dduocjw", "được"],
  ["duowcjd", "được"],
  ["duongwf", "dường"],
  ["duocjd", "đuộc"],
  ["dand", "đan"],
];

const vniCases = [
  ["a1", "á"],
  ["a2", "à"],
  ["a3", "ả"],
  ["a4", "ã"],
  ["a5", "ạ"],
  ["a6", "â"],
  ["e6", "ê"],
  ["o6", "ô"],
  ["o7", "ơ"],
  ["u7", "ư"],
  ["uo7", "ươ"],
  ["a8", "ă"],
  ["d9", "đ"],
  ["hien2", "hiền"],
  ["viet5", "việt"],
  ["muon1", "muốn"],
  ["tieu3", "tiểu"],
  ["chuoi1", "chuối"],
  ["d9uoc75", "được"],
  ["duoc579", "được"],
  ["duoc795", "được"],
  ["d9uong72", "đường"],
  ["d99", "d9"],
  ["a88", "a8"],
  ["o77", "o7"],
  ["u77", "u7"],
  ["uo77", "uo7"],
  ["a66", "a6"],
  ["e66", "e6"],
  ["o66", "o6"],
  ["a11", "a1"],
  ["ban11", "ban1"],
  ["toan1", "toán"],
  ["toa1n", "toán"],
  ["phap1", "pháp"],
  ["pha1p", "pháp"],
  ["hoac85", "hoặc"],
  ["win10", "win10"],
  ["password1", "password1"],
  ["test1", "test1"],
  ["fast8", "fast8"],
  ["a10", "a"],
  ["toan10", "toan"],
];

console.log('--- Testing Telex cases ---');
let telexFail = 0;
for (const [inp, expected] of telexCases) {
  const got = gtvTransform(inp, 'telex', true);
  if (got !== expected) {
    console.error(`FAIL: input="${inp}", expected="${expected}", got="${got}"`);
    telexFail++;
  }
}
if (telexFail === 0) console.log(`Telex passed all ${telexCases.length} cases!`);

console.log('--- Testing VNI cases ---');
let vniFail = 0;
for (const [inp, expected] of vniCases) {
  const got = gtvTransform(inp, 'vni', true);
  if (got !== expected) {
    console.error(`FAIL: input="${inp}", expected="${expected}", got="${got}"`);
    vniFail++;
  }
}
if (vniFail === 0) console.log(`VNI passed all ${vniCases.length} cases!`);

if (telexFail + vniFail === 0) {
  console.log('ALL TESTS PASSED 100%!');
} else {
  process.exit(1);
}
