#pragma once
#include "types.h"
#include "bitboard.h"
#include <string>
#include "nnue.h"

namespace Zobrist {
extern U64 psq[12][64];
extern U64 castling[16];
extern U64 ep[8];
extern U64 side;
void init();
}

// Incremental PSQT tables (defined in eval.cpp), indexed [piece][square]
extern int PSQ_MG[12][64];
extern int PSQ_EG[12][64];
extern const int PhaseInc[6];

struct Position {
    U64 byType[6];
    U64 byColor[2];
    uint8_t board[64];
    int stm;
    int castling;
    int ep;
    int halfmove;
    int fullmove;
    U64 key;
    U64 pawnKey;
    U64 checkers;   // pieces giving check to side to move
    U64 pinned;     // own pieces pinned to own king
    int mg, eg;     // incremental PSQT+material, white perspective
    Accumulator acc;
    int phase;

    void set_fen(const std::string& fen);
    std::string fen() const;
    void clear();
    void put_piece(int p, int s);
    void remove_piece(int s);
    void move_piece(int from, int to);

    U64 pieces() const { return byColor[0] | byColor[1]; }
    U64 pieces(int c) const { return byColor[c]; }
    U64 pieces_pt(int pt) const { return byType[pt]; }
    U64 pieces(int c, int pt) const { return byColor[c] & byType[pt]; }
    U64 pieces(int c, int pt1, int pt2) const { return byColor[c] & (byType[pt1] | byType[pt2]); }
    int king_sq(int c) const { return lsb(pieces(c, KING)); }
    int piece_on(int s) const { return board[s]; }
    bool in_check() const { return checkers != 0; }

    U64 attackers_to(int s, U64 occ) const;
    bool attacked_by(int s, int c, U64 occ) const;
    bool attacked_by(int s, int c) const { return attacked_by(s, c, pieces()); }
    void compute_check_info();

    void make_move(Move m, Position& next) const;
    void make_null(Position& next) const;
    bool is_capture(Move m) const { return board[move_to(m)] != NO_PIECE || is_ep(m); }
    bool is_pseudo_legal(Move m) const;
    bool gives_check(Move m) const;
    int non_pawn_material(int c) const;
    bool has_non_pawn(int c) const { return (byColor[c] & ~(byType[PAWN] | byType[KING])) != 0; }
};

extern const int CastleMask[64];
