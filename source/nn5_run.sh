#!/bin/bash
cd "$(dirname "$0")/.."
until [ "$(grep -l '^done' source/tests/data/dg3_*.log 2>/dev/null | wc -l)" -ge 10 ]; do sleep 20; done
cat source/tests/data/dg3_*.txt > source/tests/data/dg3_all.txt
wc -l source/tests/data/dg3_all.txt
mkdir -p source/build/t5/source/build && cd source/build/t5
NN_DECAY=30 ../nnue_train256.exe ../nn5_256.h 100 6 0.5 0.001 ../../tests/data/dg1_all.txt ../../tests/data/dg2_all.txt ../../tests/data/dg3_all.txt > ../../tests/nn5.log 2>&1
echo NN5 DONE
