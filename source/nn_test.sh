#!/bin/bash
# wait for training log "done", build engine with the net, play vs HCE. Usage: nn_test.sh <log> <header> <name> <games> <conc>
cd "$(dirname "$0")/.."
until grep -q "^done" "$1"; do sleep 10; done
cp "$2" source/nnue_weights.h
./source/build.sh build/$3.exe -DUSE_NNUE 2>&1 | grep error
./source/build/$3.exe bench 11 | tail -1
powershell -NoProfile -Command "& '.\resources\fastchess\fastchess.exe' -engine cmd=source\build\$3.exe name=$3 -engine cmd=source\build\engine20.exe name=hce -each tc=5+0.05 option.Hash=64 -openings file=resources\fastchess\UHO.pgn format=pgn order=random plies=16 -rounds $4 -repeat -concurrency $5 -ratinginterval 1000 -recover -pgnout file=source\tests\$3_vs_hce.pgn 2>&1 | Select-String -Pattern 'Elo:|Games:'"
