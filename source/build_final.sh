#!/bin/bash
# Final build: PGO, NNUE, hidden tuning options, static, x86-64-v3. Usage: ./build_final.sh [out.exe]
cd "$(dirname "$0")"
OUT=${1:-build/final_pgo.exe}
rm -rf build/pgo; mkdir -p build/pgo
SRCS="uci.cpp search.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp datagen.cpp"
FLAGS="-O3 -flto -static -march=x86-64-v3 -mtune=znver4 -std=c++17 -DNDEBUG -DUSE_NNUE -DNO_TUNE_OPTIONS -fno-exceptions -fno-rtti -Wall -Wno-unused-result"
g++ $FLAGS -fprofile-generate -fprofile-dir=build/pgo -o build/pgo/gen.exe $SRCS || exit 1
./build/pgo/gen.exe bench 14 > /dev/null
g++ $FLAGS -fprofile-use -fprofile-dir=build/pgo -fprofile-correction -Wno-missing-profile -o "$OUT" $SRCS || exit 1
echo "built $OUT"
objdump -p "$OUT" | grep "DLL Name"
"$OUT" bench 11 | tail -1
