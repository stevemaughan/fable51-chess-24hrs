// Self-play data generation for evaluation tuning.
// Usage: engine.exe datagen <games> <nodes> <seed> <outfile>
#include "search.h"
#include "eval.h"
#include <cstdio>
#include <vector>
#include <string>

static U64 rng_state;
static U64 rng() {
    U64 z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static bool insufficient_material(const Position& pos) {
    if (pos.byType[PAWN] | pos.byType[ROOK] | pos.byType[QUEEN]) return false;
    int minors = popcount(pos.byType[KNIGHT] | pos.byType[BISHOP]);
    return minors <= 1;
}

void datagen(int games, int nodes, U64 seed, const char* outfile) {
    rng_state = seed * 0x9E3779B97F4A7C15ULL + 12345;
    FILE* f = fopen(outfile, "w");
    if (!f) { printf("cannot open %s\n", outfile); return; }
    Searcher* S = new Searcher();
    S->quiet = true;
    TT.resize(16);
    U64 totalPos = 0;
    for (int g = 0; g < games; g++) {
        Position pos;
        pos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        std::vector<U64> keys;
        // random opening
        int randomPlies = 8 + (int)(rng() % 3);
        bool ok = true;
        for (int i = 0; i < randomPlies; i++) {
            MoveList ml;
            gen_moves(pos, ml, GEN_ALL);
            if (ml.size() == 0) { ok = false; break; }
            Move m = ml[(int)(rng() % ml.size())].move;
            keys.push_back(pos.key);
            Position next;
            pos.make_move(m, next);
            pos = next;
        }
        if (!ok) { g--; continue; }
        {
            MoveList ml;
            gen_moves(pos, ml, GEN_ALL);
            if (ml.size() == 0) { g--; continue; }
        }
        TT.clear();
        S->clear_history();
        std::vector<std::string> fens;
        std::vector<int> scores;   // white POV
        double result = -1;
        int adjCount = 0;
        int ply = 0;
        while (true) {
            MoveList ml;
            gen_moves(pos, ml, GEN_ALL);
            if (ml.size() == 0) {
                result = pos.in_check() ? (pos.stm == WHITE ? 0.0 : 1.0) : 0.5;
                break;
            }
            if (pos.halfmove >= 100 || insufficient_material(pos) || ply > 400) { result = 0.5; break; }
            // threefold repetition in game
            int rep = 0;
            for (int i = (int)keys.size() - 2; i >= 0 && i >= (int)keys.size() - pos.halfmove; i -= 2)
                if (keys[i] == pos.key) rep++;
            if (rep >= 2) { result = 0.5; break; }

            S->rootPos = pos;
            S->gameKeys = keys;
            S->limits = Limits();
            S->limits.nodes = nodes;
            S->stopFlag = false;
            S->start();
            Move m = S->bestMove;
            int score = S->rootScore;
            if (m == MOVE_NONE) { result = 0.5; break; }
            int wscore = pos.stm == WHITE ? score : -score;
            if (std::abs(score) >= VALUE_MATE_IN_MAX) {
                result = (wscore > 0) ? 1.0 : 0.0;
                break;
            }
            if (std::abs(score) >= 1500) { if (++adjCount >= 4) { result = wscore > 0 ? 1.0 : 0.0; break; } }
            else adjCount = 0;

            bool noisy = pos.is_capture(m) || is_promo(m);
            if (!pos.in_check() && !noisy && std::abs(score) < 1200) {
                fens.push_back(pos.fen());
                scores.push_back(wscore);
            }
            keys.push_back(pos.key);
            Position next;
            pos.make_move(m, next);
            pos = next;
            ply++;
        }
        for (size_t i = 0; i < fens.size(); i++)
            fprintf(f, "%s | %d | %.1f\n", fens[i].c_str(), scores[i], result);
        totalPos += fens.size();
        if ((g + 1) % 50 == 0) { printf("games %d positions %llu\n", g + 1, (unsigned long long)totalPos); fflush(stdout); fflush(f); }
    }
    fclose(f);
    printf("done games %d positions %llu\n", games, (unsigned long long)totalPos);
}
