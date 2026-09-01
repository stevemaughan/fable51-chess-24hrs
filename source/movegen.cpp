#include "movegen.h"

// Legality for a non-king, non-ep move given pins and checkers already accounted for by target mask.
static inline bool pin_ok(const Position& pos, int ksq, int from, int to) {
    return !(pos.pinned & bit(from)) || (BB::Line[ksq][from] & bit(to));
}

static inline bool ep_legal(const Position& pos, int from, int to) {
    int us = pos.stm, them = us ^ 1;
    int ksq = pos.king_sq(us);
    int capsq = to + (us == WHITE ? -8 : 8);
    U64 occ = (pos.pieces() ^ bit(from) ^ bit(capsq)) | bit(to);
    if (BB::rook_attacks(ksq, occ) & pos.pieces(them, ROOK, QUEEN)) return false;
    if (BB::bishop_attacks(ksq, occ) & pos.pieces(them, BISHOP, QUEEN)) return false;
    return true;
}

bool is_legal(const Position& pos, Move m) {
    int us = pos.stm, them = us ^ 1;
    int from = move_from(m), to = move_to(m);
    int ksq = pos.king_sq(us);
    if (is_ep(m)) return ep_legal(pos, from, to);
    if (from == ksq) {
        if (is_castle(m)) return true; // validated at generation
        return !pos.attacked_by(to, them, pos.pieces() ^ bit(from));
    }
    if (pos.checkers) {
        if (pos.checkers & (pos.checkers - 1)) return false;
        int csq = lsb(pos.checkers);
        if (!((BB::Between[ksq][csq] | bit(csq)) & bit(to))) return false;
    }
    return pin_ok(pos, ksq, from, to);
}

template <int Us>
static void gen_pawn_moves(const Position& pos, MoveList& ml, U64 target, bool noisy, bool quiet) {
    constexpr int Them = Us ^ 1;
    constexpr int Up = Us == WHITE ? 8 : -8;
    constexpr U64 Rank7 = Us == WHITE ? RANK_7_BB : RANK_2_BB;
    constexpr U64 Rank3 = Us == WHITE ? (RANK_1_BB << 16) : (RANK_1_BB << 40);
    U64 pawns = pos.pieces(Us, PAWN);
    U64 empty = ~pos.pieces();
    U64 enemies = pos.pieces(Them);
    int ksq = pos.king_sq(Us);
    U64 pawnsOn7 = pawns & Rank7;
    U64 pawnsNot7 = pawns & ~Rank7;

    if (noisy) {
        // promotions (all four) — pushes and captures
        if (pawnsOn7) {
            U64 b1 = BB::pawn_push<Us>(pawnsOn7) & empty & target;
            U64 b2 = (Us == WHITE ? BB::shift_ne(pawnsOn7) : BB::shift_se(pawnsOn7)) & enemies & target;
            U64 b3 = (Us == WHITE ? BB::shift_nw(pawnsOn7) : BB::shift_sw(pawnsOn7)) & enemies & target;
            while (b1) { int to = pop_lsb(b1); int from = to - Up; if (pin_ok(pos, ksq, from, to)) for (int p = 3; p >= 0; p--) ml.add(make_move(from, to, MF_PROMO | p)); }
            while (b2) { int to = pop_lsb(b2); int from = to - (Us == WHITE ? 9 : -7); if (pin_ok(pos, ksq, from, to)) for (int p = 3; p >= 0; p--) ml.add(make_move(from, to, MF_PROMO | p)); }
            while (b3) { int to = pop_lsb(b3); int from = to - (Us == WHITE ? 7 : -9); if (pin_ok(pos, ksq, from, to)) for (int p = 3; p >= 0; p--) ml.add(make_move(from, to, MF_PROMO | p)); }
        }
        // captures
        U64 b2 = (Us == WHITE ? BB::shift_ne(pawnsNot7) : BB::shift_se(pawnsNot7)) & enemies & target;
        U64 b3 = (Us == WHITE ? BB::shift_nw(pawnsNot7) : BB::shift_sw(pawnsNot7)) & enemies & target;
        while (b2) { int to = pop_lsb(b2); int from = to - (Us == WHITE ? 9 : -7); if (pin_ok(pos, ksq, from, to)) ml.add(make_move(from, to)); }
        while (b3) { int to = pop_lsb(b3); int from = to - (Us == WHITE ? 7 : -9); if (pin_ok(pos, ksq, from, to)) ml.add(make_move(from, to)); }
        // en passant
        if (pos.ep != SQ_NONE) {
            int capsq = pos.ep - Up;
            // when in check, ep is only valid if it captures the checker or blocks (ep square in target)
            if (!pos.checkers || (pos.checkers & bit(capsq)) || (target & bit(pos.ep))) {
                U64 att = BB::PawnAttacks[Them][pos.ep] & pawnsNot7;
                while (att) { int from = pop_lsb(att); if (ep_legal(pos, from, pos.ep)) ml.add(make_move(from, pos.ep, MF_EP)); }
            }
        }
    }
    if (quiet) {
        U64 b1 = BB::pawn_push<Us>(pawnsNot7) & empty;
        U64 b2 = BB::pawn_push<Us>(b1 & Rank3) & empty & target;
        b1 &= target;
        while (b1) { int to = pop_lsb(b1); int from = to - Up; if (pin_ok(pos, ksq, from, to)) ml.add(make_move(from, to)); }
        while (b2) { int to = pop_lsb(b2); int from = to - 2 * Up; if (pin_ok(pos, ksq, from, to)) ml.add(make_move(from, to)); }
    }
}

void gen_moves(const Position& pos, MoveList& ml, GenType t) {
    int us = pos.stm, them = us ^ 1;
    int ksq = pos.king_sq(us);
    U64 occ = pos.pieces();
    U64 enemies = pos.pieces(them);
    U64 empty = ~occ;
    bool noisy = t != GEN_QUIET, quiet = t != GEN_NOISY;

    // king moves (always legal-checked via attacked)
    {
        U64 kt = BB::KingAttacks[ksq] & ~pos.pieces(us);
        if (!noisy) kt &= empty;
        if (!quiet) kt &= enemies;
        U64 occNoK = occ ^ bit(ksq);
        while (kt) {
            int to = pop_lsb(kt);
            if (!pos.attacked_by(to, them, occNoK)) ml.add(make_move(ksq, to));
        }
    }
    U64 target = ~pos.pieces(us);
    if (pos.checkers) {
        if (pos.checkers & (pos.checkers - 1)) return; // double check
        int csq = lsb(pos.checkers);
        target = BB::Between[ksq][csq] | bit(csq);
    }
    U64 ntarget = target;
    if (!noisy) ntarget &= empty;
    if (!quiet) ntarget &= enemies;

    if (us == WHITE) gen_pawn_moves<WHITE>(pos, ml, target, noisy, quiet);
    else gen_pawn_moves<BLACK>(pos, ml, target, noisy, quiet);

    U64 b = pos.pieces(us, KNIGHT);
    while (b) {
        int from = pop_lsb(b);
        if (pos.pinned & bit(from)) continue; // pinned knight can never move
        U64 att = BB::KnightAttacks[from] & ntarget;
        while (att) ml.add(make_move(from, pop_lsb(att)));
    }
    b = pos.pieces(us, BISHOP, QUEEN);
    while (b) {
        int from = pop_lsb(b);
        U64 att = BB::bishop_attacks(from, occ) & ntarget;
        if (pos.pinned & bit(from)) att &= BB::Line[ksq][from];
        while (att) ml.add(make_move(from, pop_lsb(att)));
    }
    b = pos.pieces(us, ROOK, QUEEN);
    while (b) {
        int from = pop_lsb(b);
        U64 att = BB::rook_attacks(from, occ) & ntarget;
        if (pos.pinned & bit(from)) att &= BB::Line[ksq][from];
        while (att) ml.add(make_move(from, pop_lsb(att)));
    }
    // castling
    if (quiet && !pos.checkers) {
        if (us == WHITE) {
            if ((pos.castling & WK_CASTLE) && !(occ & (bit(F1) | bit(G1))) && !pos.attacked_by(F1, BLACK) && !pos.attacked_by(G1, BLACK))
                ml.add(make_move(E1, G1, MF_CASTLE));
            if ((pos.castling & WQ_CASTLE) && !(occ & (bit(B1) | bit(C1) | bit(D1))) && !pos.attacked_by(D1, BLACK) && !pos.attacked_by(C1, BLACK))
                ml.add(make_move(E1, C1, MF_CASTLE));
        } else {
            if ((pos.castling & BK_CASTLE) && !(occ & (bit(F8) | bit(G8))) && !pos.attacked_by(F8, WHITE) && !pos.attacked_by(G8, WHITE))
                ml.add(make_move(E8, G8, MF_CASTLE));
            if ((pos.castling & BQ_CASTLE) && !(occ & (bit(B8) | bit(C8) | bit(D8))) && !pos.attacked_by(D8, WHITE) && !pos.attacked_by(C8, WHITE))
                ml.add(make_move(E8, C8, MF_CASTLE));
        }
    }
}
