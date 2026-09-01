#!/bin/bash
# Launch N datagen processes. Usage: ./datagen.sh <engine.exe> <games per proc> <nodes> <nprocs> <outprefix>
ENGINE=$1; GAMES=$2; NODES=$3; NPROC=$4; PREFIX=$5
for i in $(seq 1 $NPROC); do
  "$ENGINE" datagen $GAMES $NODES $((RANDOM + i * 100003)) "${PREFIX}_$i.txt" > "${PREFIX}_$i.log" 2>&1 &
done
wait
cat ${PREFIX}_*.txt > ${PREFIX}_all.txt
wc -l ${PREFIX}_all.txt
