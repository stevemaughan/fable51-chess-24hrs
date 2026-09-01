#pragma once
#include "position.h"
#include "movegen.h"
#include "tt.h"
#include <atomic>
#include <vector>
#include <chrono>

struct Limits {
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0, movetime = 0, depth = 0;
    bool infinite = false;
    U64 nodes = 0;
};

struct Stack {
    Move killers[2];
    Move currentMove;
    Move excluded;
    int staticEval;
    int ply;
    int movedPiece;
    Move pv[MAX_PLY + 1];
    int pvLen;
    int16_t (*contHist)[12][64];
};

extern TranspositionTable TT;
extern int MoveOverhead;

class Searcher {
public:
    Position rootPos;
    std::vector<U64> gameKeys;   // keys of positions before root (for repetition)
    Limits limits;
    std::atomic<bool> stopFlag{false};
    U64 nodes = 0;
    int selDepth = 0;
    std::chrono::steady_clock::time_point startTime;
    int64_t softLimit = 0, hardLimit = 0;
    bool useTime = false;
    Move bestMove = MOVE_NONE;
    int rootScore = 0;
    bool quiet = false;
    int rootDepth = 0;

    // history tables
    int16_t history[2][64][64];
    int16_t captHist[12][64][6];
    int16_t contHist[12][64][12][64];
    Move counterMove[12][64];
    int32_t corrHist[2][16384];
    int lmrTable[64][64];

    Stack stackBuf[MAX_PLY + 10];
    U64 keyStack[MAX_PLY + 10];

    Searcher();
    void clear_history();
    void start();  // runs the search on rootPos and prints bestmove
    int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    }

private:
    int search(Position& pos, int alpha, int beta, int depth, Stack* ss, bool cutNode);
    int qsearch(Position& pos, int alpha, int beta, Stack* ss);
    bool is_repetition(const Position& pos, int ply) const;
    void check_time();
    void print_info(int depth, int score, Stack* ss, int bound);
    void set_time_limits();
};

int see(const Position& pos, Move m);
bool see_ge(const Position& pos, Move m, int threshold);
