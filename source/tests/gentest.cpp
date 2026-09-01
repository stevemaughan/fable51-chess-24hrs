#include "movegen.h"
#include "eval.h"
#include <cstdio>
#include <fstream>
#include <set>
static long bad = 0, total = 0;
static void test(const Position& pos, int depth) {
    MoveList all, n, q; gen_moves(pos, all, GEN_ALL); gen_moves(pos, n, GEN_NOISY); gen_moves(pos, q, GEN_QUIET);
    std::set<Move> sa, sn;
    for (int i = 0; i < all.size(); i++) sa.insert(all[i].move);
    for (int i = 0; i < n.size(); i++) { sn.insert(n[i].move); if (!(pos.is_capture(n[i].move) || is_promo(n[i].move))) { bad++; printf("quiet in noisy %s %s\n", move_str(n[i].move).c_str(), pos.fen().c_str()); } }
    for (int i = 0; i < q.size(); i++) { if (sn.count(q[i].move)) { bad++; printf("dup %s\n", pos.fen().c_str()); } sn.insert(q[i].move); if (pos.is_capture(q[i].move) || is_promo(q[i].move)) { bad++; printf("noisy in quiet %s %s\n", move_str(q[i].move).c_str(), pos.fen().c_str()); } }
    total++;
    if (sa != sn) { bad++; if (bad < 10) printf("MISMATCH all=%d noisy=%d quiet=%d %s\n", all.size(), n.size(), q.size(), pos.fen().c_str()); }
    if (depth == 0) return;
    Position next;
    for (int i = 0; i < all.size(); i++) { pos.make_move(all[i].move, next); test(next, depth - 1); }
}
int main() {
    BB::init(); Zobrist::init(); Eval::init();
    std::ifstream in("../resources/perft/perft.epd"); std::string line;
    while (std::getline(in, line)) { Position p; p.set_fen(line.substr(0, line.find(";D"))); test(p, 2); }
    printf("positions %ld bad %ld\n", total, bad);
}
