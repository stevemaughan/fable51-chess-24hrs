// Standalone perft tester: perft.exe <epd file> [maxdepth]
#include "movegen.h"
#include "eval.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>

static U64 perft(const Position& pos, int depth) {
    MoveList ml;
    gen_moves(pos, ml, GEN_ALL);
    if (depth == 1) return ml.size();
    U64 n = 0;
    Position next;
    for (int i = 0; i < ml.size(); i++) {
        pos.make_move(ml[i].move, next);
        n += perft(next, depth - 1);
    }
    return n;
}

int main(int argc, char** argv) {
    BB::init(); Zobrist::init(); Eval::init();
    const char* file = argc > 1 ? argv[1] : "resources/perft/perft.epd";
    int maxd = argc > 2 ? atoi(argv[2]) : 5;
    U64 maxnodes = argc > 3 ? strtoull(argv[3], 0, 10) : 200000000ULL;
    std::ifstream in(file);
    std::string line;
    int fails = 0, tests = 0;
    U64 total = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t p = line.find(";D");
        std::string fen = line.substr(0, p);
        Position pos; pos.set_fen(fen);
        // check fen roundtrip
        std::string rest = line.substr(p);
        std::istringstream ss(rest);
        std::string tok;
        while (ss >> tok) {
            if (tok[0] != ';') continue;
            int d = atoi(tok.c_str() + 2);
            U64 expect; ss >> expect;
            if (d > maxd || expect > maxnodes) continue;
            U64 got = perft(pos, d);
            total += got;
            tests++;
            if (got != expect) { fails++; printf("FAIL %s D%d expected %llu got %llu\n", fen.c_str(), d, (unsigned long long)expect, (unsigned long long)got); }
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("tests %d fails %d nodes %llu time %.2fs nps %.0f\n", tests, fails, (unsigned long long)total, secs, total / secs);
    return fails ? 1 : 0;
}
