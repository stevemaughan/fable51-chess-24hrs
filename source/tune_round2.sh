#!/bin/bash
cd "$(dirname "$0")/.."
until [ "$(grep -l '^done' source/tests/data/dg2_*.log 2>/dev/null | wc -l)" -ge 10 ]; do sleep 15; done
cat source/tests/data/dg2_*.txt > source/tests/data/dg2_all.txt
shuf source/tests/data/dg2_all.txt > source/tests/data/dg2_shuf.txt
wc -l source/tests/data/dg2_shuf.txt
./source/build/tune3.exe source/tests/data/dg2_shuf.txt 1500000 6 6 source/tests/params_tuned3.h 0.5 1 > source/tests/tune3.log 2>&1
echo TUNE3 DONE
