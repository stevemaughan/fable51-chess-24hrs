#include "bitboard.h"
#include <vector>
#include <cstdlib>

namespace BB {
U64 PawnAttacks[2][64];
U64 KnightAttacks[64];
U64 KingAttacks[64];
U64 RookMask[64];
U64 BishopMask[64];
U64* RookAttackPtr[64];
U64* BishopAttackPtr[64];
U64 Between[64][64];
U64 Line[64][64];
U64 AdjacentFiles[8];
U64 PassedMask[2][64];
U64 ForwardRanks[2][8];
int  SqDist[64][64];

static U64 RookTable[102400];
static U64 BishopTable[5248];

static U64 sliding_attack(int sq, U64 occ, const int dirs[4][2]) {
    U64 att = 0;
    for (int d = 0; d < 4; d++) {
        int f = file_of(sq) + dirs[d][0], r = rank_of(sq) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            int s = make_sq(f, r);
            att |= bit(s);
            if (occ & bit(s)) break;
            f += dirs[d][0]; r += dirs[d][1];
        }
    }
    return att;
}

static const int RookDirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
static const int BishopDirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};

static void init_slider(bool rook) {
    U64* table = rook ? RookTable : BishopTable;
    size_t idx = 0;
    for (int s = 0; s < 64; s++) {
        U64 edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(s))) | ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(s)));
        U64 mask = sliding_attack(s, 0, rook ? RookDirs : BishopDirs) & ~edges;
        (rook ? RookMask : BishopMask)[s] = mask;
        (rook ? RookAttackPtr : BishopAttackPtr)[s] = table + idx;
        // enumerate subsets
        U64 sub = 0;
        do {
            table[idx + _pext_u64(sub, mask)] = sliding_attack(s, sub, rook ? RookDirs : BishopDirs);
            sub = (sub - mask) & mask;
        } while (sub);
        idx += 1ULL << popcount(mask);
    }
}

void init() {
    for (int s = 0; s < 64; s++) {
        U64 b = bit(s);
        PawnAttacks[WHITE][s] = shift_ne(b) | shift_nw(b);
        PawnAttacks[BLACK][s] = shift_se(b) | shift_sw(b);
        KnightAttacks[s] = ((b << 17) & ~FILE_A_BB) | ((b << 15) & ~FILE_H_BB) | ((b << 10) & ~(FILE_A_BB | FILE_A_BB << 1)) |
                           ((b << 6) & ~(FILE_H_BB | FILE_H_BB >> 1)) | ((b >> 17) & ~FILE_H_BB) | ((b >> 15) & ~FILE_A_BB) |
                           ((b >> 10) & ~(FILE_H_BB | FILE_H_BB >> 1)) | ((b >> 6) & ~(FILE_A_BB | FILE_A_BB << 1));
        KingAttacks[s] = shift_n(b) | shift_s(b) | shift_e(b) | shift_w(b) | shift_ne(b) | shift_nw(b) | shift_se(b) | shift_sw(b);
    }
    init_slider(true);
    init_slider(false);
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            SqDist[a][b] = std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));
            Between[a][b] = 0; Line[a][b] = 0;
            if (a == b) continue;
            if (rook_attacks(a, 0) & bit(b)) {
                Between[a][b] = rook_attacks(a, bit(b)) & rook_attacks(b, bit(a));
                Line[a][b] = (rook_attacks(a, 0) & rook_attacks(b, 0)) | bit(a) | bit(b);
            } else if (bishop_attacks(a, 0) & bit(b)) {
                Between[a][b] = bishop_attacks(a, bit(b)) & bishop_attacks(b, bit(a));
                Line[a][b] = (bishop_attacks(a, 0) & bishop_attacks(b, 0)) | bit(a) | bit(b);
            }
        }
    for (int f = 0; f < 8; f++)
        AdjacentFiles[f] = (f > 0 ? file_bb(f - 1) : 0) | (f < 7 ? file_bb(f + 1) : 0);
    for (int r = 0; r < 8; r++) {
        ForwardRanks[WHITE][r] = 0; ForwardRanks[BLACK][r] = 0;
        for (int r2 = r + 1; r2 < 8; r2++) ForwardRanks[WHITE][r] |= rank_bb(r2);
        for (int r2 = r - 1; r2 >= 0; r2--) ForwardRanks[BLACK][r] |= rank_bb(r2);
    }
    for (int c = 0; c < 2; c++)
        for (int s = 0; s < 64; s++)
            PassedMask[c][s] = ForwardRanks[c][rank_of(s)] & (file_bb(file_of(s)) | AdjacentFiles[file_of(s)]);
}
}
