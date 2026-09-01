#pragma once
#include "position.h"

namespace Eval {
void init();
int evaluate(const Position& pos);  // from side to move's perspective
extern int PieceValue[6];           // for SEE / ordering (mg-ish)
}
