#include "movegen.h"
#include "eval.h"
#include <cstdio>
#include <fstream>
#include <set>
static long bad1 = 0, bad2 = 0, total = 0;
static void test(const Position& pos, int depth) {
    MoveList ml; gen_moves(pos, ml, GEN_ALL);
    std::set<Move> legal;
    for (int i = 0; i < ml.size(); i++) legal.insert(ml[i].move);
    for (int i = 0; i < ml.size(); i++) {
        total++;
        if (!(pos.is_pseudo_legal(ml[i].move) && is_legal(pos, ml[i].move))) { bad1++; if (bad1 < 10) printf("REJECT legal %s in %s\n", move_str(ml[i].move).c_str(), pos.fen().c_str()); }
    }
    // try all from/to with all flag combos for a sample of moves
    for (int from = 0; from < 64; from++) for (int to = 0; to < 64; to++) for (int fl = 0; fl < 8; fl++) {
        Move m = make_move(from, to, fl);
        if (legal.count(m)) continue;
        if (pos.is_pseudo_legal(m) && is_legal(pos, m)) { bad2++; if (bad2 < 10) printf("ACCEPT illegal %s fl%d in %s\n", move_str(m).c_str(), fl, pos.fen().c_str()); }
    }
    if (depth == 0) return;
    Position next;
    for (int i = 0; i < ml.size(); i++) { pos.make_move(ml[i].move, next); test(next, depth - 1); }
}
int main() {
    BB::init(); Zobrist::init(); Eval::init();
    std::ifstream in("../resources/perft/perft.epd"); std::string line;
    while (std::getline(in, line)) { Position p; p.set_fen(line.substr(0, line.find(";D"))); test(p, 1); }
    printf("total %ld rejected-legal %ld accepted-illegal %ld\n", total, bad1, bad2);
}
