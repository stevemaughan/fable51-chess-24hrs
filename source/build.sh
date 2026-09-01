#!/bin/bash
# Build script. Usage: ./build.sh [output.exe] [extra flags]
cd "$(dirname "$0")"
OUT=${1:-build/engine.exe}
shift
mkdir -p build
g++ -O3 -flto -static -march=x86-64-v3 -mtune=znver4 -std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -Wall -Wno-unused-result \
    "$@" -o "$OUT" uci.cpp search.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp
