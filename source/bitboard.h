#pragma once
#include "types.h"

constexpr U64 FILE_A_BB = 0x0101010101010101ULL;
constexpr U64 FILE_H_BB = FILE_A_BB << 7;
constexpr U64 RANK_1_BB = 0xFFULL;
constexpr U64 RANK_2_BB = RANK_1_BB << 8;
constexpr U64 RANK_7_BB = RANK_1_BB << 48;
constexpr U64 RANK_8_BB = RANK_1_BB << 56;
inline constexpr U64 file_bb(int f) { return FILE_A_BB << f; }
inline constexpr U64 rank_bb(int r) { return RANK_1_BB << (8 * r); }

namespace BB {
extern U64 PawnAttacks[2][64];
extern U64 KnightAttacks[64];
extern U64 KingAttacks[64];
extern U64 RookMask[64];
extern U64 BishopMask[64];
extern U64* RookAttackPtr[64];
extern U64* BishopAttackPtr[64];
extern U64 Between[64][64];   // squares strictly between (0 if not aligned)
extern U64 Line[64][64];      // full line through both (0 if not aligned)
extern U64 AdjacentFiles[8];
extern U64 PassedMask[2][64];
extern U64 ForwardRanks[2][8];
extern int  SqDist[64][64];

void init();

inline U64 rook_attacks(int s, U64 occ) { return RookAttackPtr[s][_pext_u64(occ, RookMask[s])]; }
inline U64 bishop_attacks(int s, U64 occ) { return BishopAttackPtr[s][_pext_u64(occ, BishopMask[s])]; }
inline U64 queen_attacks(int s, U64 occ) { return rook_attacks(s, occ) | bishop_attacks(s, occ); }

inline U64 shift_n(U64 b) { return b << 8; }
inline U64 shift_s(U64 b) { return b >> 8; }
inline U64 shift_e(U64 b) { return (b & ~FILE_H_BB) << 1; }
inline U64 shift_w(U64 b) { return (b & ~FILE_A_BB) >> 1; }
inline U64 shift_ne(U64 b) { return (b & ~FILE_H_BB) << 9; }
inline U64 shift_nw(U64 b) { return (b & ~FILE_A_BB) << 7; }
inline U64 shift_se(U64 b) { return (b & ~FILE_H_BB) >> 7; }
inline U64 shift_sw(U64 b) { return (b & ~FILE_A_BB) >> 9; }
template <int C> inline U64 pawn_push(U64 b) { return C == WHITE ? shift_n(b) : shift_s(b); }
template <int C> inline U64 pawn_attacks_bb(U64 b) { return C == WHITE ? (shift_ne(b) | shift_nw(b)) : (shift_se(b) | shift_sw(b)); }
inline U64 pawn_attacks_bb(int c, U64 b) { return c == WHITE ? (shift_ne(b) | shift_nw(b)) : (shift_se(b) | shift_sw(b)); }
}
