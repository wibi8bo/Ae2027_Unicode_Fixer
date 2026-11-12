#!/bin/sh
# Build !00_UTF8_Fixer_2027.plugin into build/
set -eu
cd "$(dirname "$0")"

NAME='!00_UTF8_Fixer_2027'
OUT="build/${NAME}.plugin"

rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

cp src/Info.plist "$OUT/Contents/Info.plist"
printf 'AEgxFXTC' > "$OUT/Contents/PkgInfo"
python3 src/make_rsrc.py "$OUT/Contents/Resources/${NAME}.rsrc"

clang++ -arch arm64 -std=c++17 -O2 -Wall -bundle -undefined dynamic_lookup \
  -o "$OUT/Contents/MacOS/${NAME}" src/fixer.cpp -liconv

codesign --force --deep -s - "$OUT"
codesign --verify --deep --strict "$OUT"

echo "built: $OUT"
