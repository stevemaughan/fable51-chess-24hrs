#pragma once
#include "position.h"

struct ExtMove {
    Move move;
    int score;
};

struct MoveList {
    ExtMove list[256];
    int count = 0;
    void add(Move m) { list[count++].move = m; }
    ExtMove& operator[](int i) { return list[i]; }
    int size() const { return count; }
};

enum GenType { GEN_NOISY, GEN_QUIET, GEN_ALL };

// Generates fully legal moves.
void gen_moves(const Position& pos, MoveList& ml, GenType t);
bool is_legal(const Position& pos, Move m);  // for pseudo-legal moves already geometrically valid
