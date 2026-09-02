#!/bin/bash
# PGO build. Usage: ./build_pgo.sh <output.exe>
cd "$(dirname "$0")"
OUT=${1:-build/engine_pgo.exe}
mkdir -p build/pgo
rm -rf build/pgo; mkdir -p build/pgo
SRCS="uci.cpp search.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp datagen.cpp"
FLAGS="-O3 -flto -static -march=x86-64-v3 -mtune=znver4 -std=c++17 -DNDEBUG -DNO_TUNE_OPTIONS -fno-exceptions -fno-rtti -Wall -Wno-unused-result"
g++ $FLAGS -fprofile-generate -fprofile-dir=build/pgo -o build/pgo/gen.exe $SRCS || exit 1
./build/pgo/gen.exe bench 13 > /dev/null
g++ $FLAGS -fprofile-use -fprofile-dir=build/pgo -fprofile-correction -Wno-missing-profile -o "$OUT" $SRCS || exit 1
echo "built $OUT"
