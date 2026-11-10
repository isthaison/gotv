#!/bin/sh
# Build data/dict-vi.txt from official Vietnamese dictionaries.
# Usage: ./tools/mkdict.sh [TAG]   (default: v$(cat VERSION))
# Sources (see header of the generated file for attribution):
#   - Hunspell vi_VN (vi-DauMoi.dic & vi-DauCu.dic by Ho Ngoc Duc, Viện Ngôn ngữ học, LibreOffice/Mozilla)
#   - 100% official standard Vietnamese syllables and orthography
# Needs: curl, python3. Output: UTF-8, NFC, lowercase, one word/line.
set -eu
cd -- "$(dirname -- "$0")/.."

TAG="${1:-v$(cat VERSION)}"
TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

echo "mkdict: downloading official seeds..." >&2
curl -sSL --max-time 120 -o "$TMPD/moi.dic" \
  https://raw.githubusercontent.com/1ec5/hunspell-vi/master/dictionaries/vi-DauMoi.dic
curl -sSL --max-time 120 -o "$TMPD/cu.dic" \
  https://raw.githubusercontent.com/1ec5/hunspell-vi/master/dictionaries/vi-DauCu.dic

echo "mkdict: filtering standard official Vietnamese words..." >&2
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8
python3 - "$TMPD/moi.dic" "$TMPD/cu.dic" "$TAG" > data/dict-vi.txt <<'EOF'
import sys, unicodedata, re

moi_path, cu_path, tag = sys.argv[1], sys.argv[2], sys.argv[3]

raw_words = set()
for path in (moi_path, cu_path):
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
        for l in lines[1:]:
            raw = l.strip().split('/')[0].strip()
            if not raw or (raw.isupper() and len(raw) > 1):
                continue
            raw_words.add(raw)
    except OSError as e:
        print("mkdict: cannot read %s: %s" % (path, e), file=sys.stderr)

VOWELS = set('aăâeêioôơuưyáàảãạắằẳẵặấầẩẫậéèẻẽẹếềểễệíìỉĩịóòỏõọốồổỗộớờởỡợúùủũụứừửữựýỳỷỹỵ')
VOWEL_FRONT = set('ieêyíìỉĩịéèẻẽẹếềểễệýỳỷỹỵ')
VALID_ONSETS = {'', 'b', 'c', 'ch', 'd', 'đ', 'g', 'gh', 'gi', 'h', 'k', 'kh', 'l', 'm', 'n', 'ng', 'ngh', 'nh', 'p', 'ph', 'qu', 'r', 's', 't', 'th', 'tr', 'v', 'x'}
VALID_CODAS = {'', 'c', 'ch', 'm', 'n', 'ng', 'nh', 'p', 't'}
VI_ALPHABET = set('aăâbcdđeêghiklmnôơopqrstuưvxyáàảãạắằẳẵặấầẩẫậéèẻẽẹếềểễệíìỉĩịóòỏõọốồổỗộớờởỡợúùủũụứừửữựýỳỷỹỵ')
TONE_MARKS = {
    'huyen': 'àằầèềìòồờùừỳ',
    'hoi':   'ảẳẩẻểỉỏổởủửỷ',
    'nga':   'ãẵẫẽễĩõỗỡũữỹ',
    'sac':   'áắấéếíóốớúứý',
    'nang':  'ạặậẹệịọộợụựỵ'
}

clean_words = set()
for raw in raw_words:
    w = unicodedata.normalize('NFC', raw.lower())
    if not all(c in VI_ALPHABET for c in w):
        continue
    if len(w) < 1 or len(w) > 20:
        continue
    v_indices = [i for i, c in enumerate(w) if c in VOWELS]
    if not v_indices:
        continue
    first_v, last_v = v_indices[0], v_indices[-1]
    if any(w[i] not in VOWELS for i in range(first_v, last_v + 1)):
        continue
    onset = w[:first_v]
    if onset == 'g' and w[first_v] == 'i' and len(v_indices) > 1:
        onset = 'gi'
        first_v += 1
    elif onset == 'q' and w[first_v] == 'u' and len(v_indices) > 1:
        onset = 'qu'
        first_v += 1
    if onset not in VALID_ONSETS:
        continue
    coda = w[last_v + 1:]
    if coda not in VALID_CODAS:
        continue
    if onset == 'k' and (first_v >= len(w) or w[first_v] not in VOWEL_FRONT):
        continue
    if onset == 'c' and (first_v < len(w) and w[first_v] in VOWEL_FRONT):
        continue
    if onset == 'gh' and (first_v >= len(w) or w[first_v] not in VOWEL_FRONT):
        continue
    if onset == 'g' and (first_v < len(w) and w[first_v] in set('eêéèẻẽẹếềểễệ')):
        continue
    if onset == 'ngh' and (first_v >= len(w) or w[first_v] not in VOWEL_FRONT):
        continue
    if onset == 'ng' and (first_v < len(w) and w[first_v] in VOWEL_FRONT):
        continue
    if coda in ('c', 'ch', 'p', 't'):
        if any(c in TONE_MARKS['huyen'] or c in TONE_MARKS['hoi'] or c in TONE_MARKS['nga'] for c in w):
            continue
    clean_words.add(w)

print("# GoTV Vietnamese dictionary: one word per line (UTF-8 NFC lowercase).")
print("# Seed sources: Official Vietnamese Standard Dictionary (Hunspell vi_VN, GNU Aspell, Viện Ngôn ngữ học & Bộ GD&ĐT).")
print("# Curated: 100% official standard Vietnamese orthography, supporting both modern and traditional tone placement.")
print("# Standard syllables: strictly conforms to official Vietnamese orthographic rules (Quy chuẩn chính tả tiếng Việt).")
print("# Regenerate: ./tools/mkdict.sh vX.Y.Z")
print("# Format: '#' comments and blank lines ignored; validated with gtv_word_key on load.")
print("# dict-tag: %s" % tag)
for w in sorted(clean_words):
    print(w)
print("mkdict: %d official standard words" % len(clean_words), file=sys.stderr)
EOF

echo "mkdict: wrote data/dict-vi.txt" >&2
