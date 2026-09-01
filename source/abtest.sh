#!/bin/bash
# A/B test one or more UCI options against defaults. Usage: ./abtest.sh <engine.exe> <rounds> <tc> <conc> <label> option.X=val [option.Y=val ...]
E=$1; R=$2; TC=$3; C=$4; L=$5; shift 5
cd "$(dirname "$0")/.."
powershell -NoProfile -Command "& '.\resources\fastchess\fastchess.exe' -engine cmd=$E name=new $* -engine cmd=$E name=base -each tc=$TC option.Hash=64 -openings file=resources\fastchess\UHO.pgn format=pgn order=random plies=16 -rounds $R -repeat -concurrency $C -ratinginterval 1000 -recover -pgnout file=source\tests\ab_$L.pgn 2>&1 | Select-String -Pattern 'Elo:|Games:'"
