#pragma once
#include "position.h"
#include <vector>

namespace Eval {
typedef int Score;
static constexpr Score S(int mg, int eg) { return (int)((unsigned)mg << 16) + eg; }
static inline int eg_of(Score s) { return (int16_t)(uint16_t)(s & 0xffff); }
static inline int mg_of(Score s) { return (int16_t)(uint16_t)(((unsigned)s + 0x8000) >> 16); }

void init();
int evaluate(const Position& pos);  // from side to move's perspective
extern int PieceValue[6];           // for SEE / ordering
extern bool UsePawnHash;

struct TuneEntry { const char* name; void* ptr; int n; int kind; };  // kind: 0 Score, 1 int, 2 psqt, 3 frozen int
std::vector<TuneEntry> tune_entries();
}
