#include "position.h"
#include <sstream>
#include <cstdio>

namespace Zobrist {
U64 psq[12][64];
U64 castling[16];
U64 ep[8];
U64 side;
static U64 seed = 0x9E3779B97F4A7C15ULL;
static U64 next_rand() {
    U64 z = (seed += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
void init() {
    for (int p = 0; p < 12; p++) for (int s = 0; s < 64; s++) psq[p][s] = next_rand();
    for (int i = 0; i < 16; i++) castling[i] = next_rand();
    for (int i = 0; i < 8; i++) ep[i] = next_rand();
    side = next_rand();
}
}

const int CastleMask[64] = {
    ~WQ_CASTLE & 15, 15, 15, 15, ~(WK_CASTLE | WQ_CASTLE) & 15, 15, 15, ~WK_CASTLE & 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    ~BQ_CASTLE & 15, 15, 15, 15, ~(BK_CASTLE | BQ_CASTLE) & 15, 15, 15, ~BK_CASTLE & 15,
};

void Position::clear() {
    memset(this, 0, sizeof(Position));
    NNUE::init_acc(acc);
    for (int s = 0; s < 64; s++) board[s] = NO_PIECE;
    ep = SQ_NONE;
    stm = WHITE;
}

void Position::put_piece(int p, int s) {
    board[s] = (uint8_t)p;
    NNUE::add_piece(acc, p, s);
    byType[piece_type(p)] |= bit(s);
    byColor[piece_color(p)] |= bit(s);
    key ^= Zobrist::psq[p][s];
    if (piece_type(p) == PAWN) pawnKey ^= Zobrist::psq[p][s];
    int sign = piece_color(p) == WHITE ? 1 : -1;
    mg += sign * PSQ_MG[p][s];
    eg += sign * PSQ_EG[p][s];
    phase += PhaseInc[piece_type(p)];
}

void Position::remove_piece(int s) {
    int p = board[s];
    NNUE::remove_piece(acc, p, s);
    board[s] = NO_PIECE;
    byType[piece_type(p)] &= ~bit(s);
    byColor[piece_color(p)] &= ~bit(s);
    key ^= Zobrist::psq[p][s];
    if (piece_type(p) == PAWN) pawnKey ^= Zobrist::psq[p][s];
    int sign = piece_color(p) == WHITE ? 1 : -1;
    mg -= sign * PSQ_MG[p][s];
    eg -= sign * PSQ_EG[p][s];
    phase -= PhaseInc[piece_type(p)];
}

void Position::move_piece(int from, int to) {
    int p = board[from];
    NNUE::move_piece(acc, p, from, to);
    U64 ft = bit(from) | bit(to);
    board[from] = NO_PIECE;
    board[to] = (uint8_t)p;
    byType[piece_type(p)] ^= ft;
    byColor[piece_color(p)] ^= ft;
    key ^= Zobrist::psq[p][from] ^ Zobrist::psq[p][to];
    if (piece_type(p) == PAWN) pawnKey ^= Zobrist::psq[p][from] ^ Zobrist::psq[p][to];
    int sign = piece_color(p) == WHITE ? 1 : -1;
    mg += sign * (PSQ_MG[p][to] - PSQ_MG[p][from]);
    eg += sign * (PSQ_EG[p][to] - PSQ_EG[p][from]);
}

void Position::set_fen(const std::string& fen) {
    clear();
    std::istringstream ss(fen);
    std::string board_s, stm_s, castle_s, ep_s;
    ss >> board_s >> stm_s >> castle_s >> ep_s;
    int hm = 0, fm = 1;
    ss >> hm >> fm;
    int f = 0, r = 7;
    for (char c : board_s) {
        if (c == '/') { f = 0; r--; continue; }
        if (c >= '1' && c <= '8') { f += c - '0'; continue; }
        int p = NO_PIECE;
        switch (c) {
            case 'P': p = make_piece(WHITE, PAWN); break; case 'N': p = make_piece(WHITE, KNIGHT); break;
            case 'B': p = make_piece(WHITE, BISHOP); break; case 'R': p = make_piece(WHITE, ROOK); break;
            case 'Q': p = make_piece(WHITE, QUEEN); break; case 'K': p = make_piece(WHITE, KING); break;
            case 'p': p = make_piece(BLACK, PAWN); break; case 'n': p = make_piece(BLACK, KNIGHT); break;
            case 'b': p = make_piece(BLACK, BISHOP); break; case 'r': p = make_piece(BLACK, ROOK); break;
            case 'q': p = make_piece(BLACK, QUEEN); break; case 'k': p = make_piece(BLACK, KING); break;
        }
        if (p != NO_PIECE && f < 8 && r >= 0) put_piece(p, make_sq(f, r));
        f++;
    }
    stm = (stm_s == "b") ? BLACK : WHITE;
    if (stm == BLACK) key ^= Zobrist::side;
    castling = 0;
    for (char c : castle_s) {
        if (c == 'K') castling |= WK_CASTLE;
        else if (c == 'Q') castling |= WQ_CASTLE;
        else if (c == 'k') castling |= BK_CASTLE;
        else if (c == 'q') castling |= BQ_CASTLE;
    }
    // sanity: drop rights if king/rook not in place
    if (!(pieces(WHITE, KING) & bit(E1))) castling &= ~(WK_CASTLE | WQ_CASTLE);
    if (!(pieces(BLACK, KING) & bit(E8))) castling &= ~(BK_CASTLE | BQ_CASTLE);
    if (!(pieces(WHITE, ROOK) & bit(H1))) castling &= ~WK_CASTLE;
    if (!(pieces(WHITE, ROOK) & bit(A1))) castling &= ~WQ_CASTLE;
    if (!(pieces(BLACK, ROOK) & bit(H8))) castling &= ~BK_CASTLE;
    if (!(pieces(BLACK, ROOK) & bit(A8))) castling &= ~BQ_CASTLE;
    key ^= Zobrist::castling[castling];
    ep = SQ_NONE;
    if (ep_s.size() == 2 && ep_s[0] >= 'a' && ep_s[0] <= 'h' && ep_s[1] >= '1' && ep_s[1] <= '8') {
        int s = make_sq(ep_s[0] - 'a', ep_s[1] - '1');
        // only keep if a capture is actually possible
        if (BB::PawnAttacks[stm ^ 1][s] & pieces(stm, PAWN)) {
            ep = s;
            key ^= Zobrist::ep[file_of(s)];
        }
    }
    halfmove = hm;
    fullmove = fm;
    compute_check_info();
}

std::string Position::fen() const {
    std::string s;
    const char* pc = "PNBRQKpnbrqk";
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[make_sq(f, r)];
            if (p == NO_PIECE) { empty++; continue; }
            if (empty) { s += (char)('0' + empty); empty = 0; }
            s += pc[p];
        }
        if (empty) s += (char)('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    if (!castling) s += "-";
    if (castling & WK_CASTLE) s += "K";
    if (castling & WQ_CASTLE) s += "Q";
    if (castling & BK_CASTLE) s += "k";
    if (castling & BQ_CASTLE) s += "q";
    s += " ";
    s += ep == SQ_NONE ? "-" : sq_str(ep);
    s += " " + std::to_string(halfmove) + " " + std::to_string(fullmove);
    return s;
}

U64 Position::attackers_to(int s, U64 occ) const {
    return (BB::PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) |
           (BB::PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
           (BB::KnightAttacks[s] & byType[KNIGHT]) |
           (BB::KingAttacks[s] & byType[KING]) |
           (BB::bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN])) |
           (BB::rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN]));
}

bool Position::attacked_by(int s, int c, U64 occ) const {
    if (BB::PawnAttacks[c ^ 1][s] & pieces(c, PAWN)) return true;
    if (BB::KnightAttacks[s] & pieces(c, KNIGHT)) return true;
    if (BB::KingAttacks[s] & pieces(c, KING)) return true;
    if (BB::bishop_attacks(s, occ) & pieces(c, BISHOP, QUEEN)) return true;
    if (BB::rook_attacks(s, occ) & pieces(c, ROOK, QUEEN)) return true;
    return false;
}

void Position::compute_check_info() {
    int us = stm, them = stm ^ 1;
    int ksq = king_sq(us);
    U64 occ = pieces();
    checkers = attackers_to(ksq, occ) & byColor[them];
    pinned = 0;
    U64 snipers = ((BB::rook_attacks(ksq, 0) & pieces(them, ROOK, QUEEN)) |
                   (BB::bishop_attacks(ksq, 0) & pieces(them, BISHOP, QUEEN)));
    while (snipers) {
        int s = pop_lsb(snipers);
        U64 b = BB::Between[ksq][s] & occ;
        if (b && !(b & (b - 1)) && (b & byColor[us])) pinned |= b;
    }
}

void Position::make_move(Move m, Position& next) const {
    next = *this;
    int us = stm, them = stm ^ 1;
    int from = move_from(m), to = move_to(m);
    int p = board[from];
    int pt = piece_type(p);
    int flags = move_flags(m);

    next.halfmove++;
    if (next.ep != SQ_NONE) { next.key ^= Zobrist::ep[file_of(next.ep)]; next.ep = SQ_NONE; }

    if (flags == MF_CASTLE) {
        int rfrom, rto;
        if (to > from) { rfrom = to + 1; rto = to - 1; } else { rfrom = to - 2; rto = to + 1; }
        next.move_piece(from, to);
        next.move_piece(rfrom, rto);
    } else {
        int captured = board[to];
        if (flags == MF_EP) {
            int capsq = to + (us == WHITE ? -8 : 8);
            next.remove_piece(capsq);
            next.halfmove = 0;
        } else if (captured != NO_PIECE) {
            next.remove_piece(to);
            next.halfmove = 0;
        }
        next.move_piece(from, to);
        if (pt == PAWN) {
            next.halfmove = 0;
            if (flags & MF_PROMO) {
                next.remove_piece(to);
                next.put_piece(make_piece(us, promo_type(m)), to);
            } else if ((to ^ from) == 16) {
                int epsq = (from + to) / 2;
                if (BB::PawnAttacks[us][epsq] & pieces(them, PAWN)) {
                    next.ep = epsq;
                    next.key ^= Zobrist::ep[file_of(epsq)];
                }
            }
        }
    }
    int newc = castling & CastleMask[from] & CastleMask[to];
    if (newc != castling) {
        next.key ^= Zobrist::castling[castling] ^ Zobrist::castling[newc];
        next.castling = newc;
    }
    next.stm = them;
    next.key ^= Zobrist::side;
    if (them == WHITE) next.fullmove++;
    next.compute_check_info();
}

void Position::make_null(Position& next) const {
    next = *this;
    if (next.ep != SQ_NONE) { next.key ^= Zobrist::ep[file_of(next.ep)]; next.ep = SQ_NONE; }
    next.stm = stm ^ 1;
    next.key ^= Zobrist::side;
    next.halfmove++;
    next.compute_check_info();
}

int Position::non_pawn_material(int c) const {
    return 3 * popcount(pieces(c, KNIGHT)) + 3 * popcount(pieces(c, BISHOP)) + 5 * popcount(pieces(c, ROOK)) + 9 * popcount(pieces(c, QUEEN));
}

bool Position::gives_check(Move m) const {
    int us = stm, them = stm ^ 1;
    int from = move_from(m), to = move_to(m);
    int ksq = king_sq(them);
    int pt = piece_type(board[from]);
    int flags = move_flags(m);
    U64 occ = (pieces() ^ bit(from)) | bit(to);
    if (flags & MF_PROMO) pt = promo_type(m);
    // direct check
    U64 att = 0;
    switch (pt) {
        case PAWN: att = BB::PawnAttacks[us][to]; break;
        case KNIGHT: att = BB::KnightAttacks[to]; break;
        case BISHOP: att = BB::bishop_attacks(to, occ); break;
        case ROOK: att = BB::rook_attacks(to, occ); break;
        case QUEEN: att = BB::queen_attacks(to, occ); break;
        default: break;
    }
    if (att & bit(ksq)) return true;
    // discovered check
    if (flags == MF_EP) {
        int capsq = to + (us == WHITE ? -8 : 8);
        occ ^= bit(capsq);
    }
    if (flags == MF_CASTLE) {
        int rto = to > from ? to - 1 : to + 1;
        int rfrom = to > from ? to + 1 : to - 2;
        occ = (occ ^ bit(rfrom)) | bit(rto);
        return (BB::rook_attacks(rto, occ) & bit(ksq)) != 0;
    }
    if (BB::bishop_attacks(ksq, occ) & pieces(us, BISHOP, QUEEN)) return true;
    if (BB::rook_attacks(ksq, occ) & pieces(us, ROOK, QUEEN)) return true;
    return false;
}

// Check a (TT/killer) move is pseudo-legal in this position; legality still verified by movegen rules
bool Position::is_pseudo_legal(Move m) const {
    if (m == MOVE_NONE) return false;
    int us = stm, them = stm ^ 1;
    int from = move_from(m), to = move_to(m);
    int p = board[from];
    if (p == NO_PIECE || piece_color(p) != us) return false;
    if (board[to] != NO_PIECE && piece_color(board[to]) == us) return false;
    int pt = piece_type(p);
    int flags = move_flags(m);
    U64 occ = pieces();
    if (flags == MF_CASTLE) {
        if (pt != KING || checkers) return false;
        int rank = us == WHITE ? 0 : 7;
        if (from != make_sq(4, rank)) return false;
        if (to == make_sq(6, rank)) {
            if (!(castling & (us == WHITE ? WK_CASTLE : BK_CASTLE))) return false;
            if (occ & (bit(from + 1) | bit(from + 2))) return false;
            if (attacked_by(from + 1, them) || attacked_by(from + 2, them)) return false;
            return true;
        } else if (to == make_sq(2, rank)) {
            if (!(castling & (us == WHITE ? WQ_CASTLE : BQ_CASTLE))) return false;
            if (occ & (bit(from - 1) | bit(from - 2) | bit(from - 3))) return false;
            if (attacked_by(from - 1, them) || attacked_by(from - 2, them)) return false;
            return true;
        }
        return false;
    }
    if (flags == 3) return false;
    if (pt == PAWN) {
        int push = us == WHITE ? 8 : -8;
        int promoRank = us == WHITE ? 7 : 0;
        if ((rank_of(to) == promoRank) != ((flags & MF_PROMO) != 0)) return false;
        if (flags == MF_EP) {
            if (to != ep) return false;
            return (BB::PawnAttacks[us][from] & bit(to)) != 0;
        }
        if (BB::PawnAttacks[us][from] & bit(to)) return board[to] != NO_PIECE;
        if (to == from + push) return board[to] == NO_PIECE;
        if (to == from + 2 * push && relative_rank(us, from) == 1) return board[to] == NO_PIECE && board[from + push] == NO_PIECE;
        return false;
    }
    if (flags != MF_NORMAL) return false;
    U64 att = 0;
    switch (pt) {
        case KNIGHT: att = BB::KnightAttacks[from]; break;
        case BISHOP: att = BB::bishop_attacks(from, occ); break;
        case ROOK: att = BB::rook_attacks(from, occ); break;
        case QUEEN: att = BB::queen_attacks(from, occ); break;
        case KING: att = BB::KingAttacks[from]; break;
    }
    if (!(att & bit(to))) return false;
    // check-evasion consistency and pins
    if (pt == KING) {
        return !attacked_by(to, them, occ ^ bit(from));
    }
    if (checkers) {
        if (checkers & (checkers - 1)) return false; // double check: only king moves
        int csq = lsb(checkers);
        if (!((BB::Between[king_sq(us)][csq] | bit(csq)) & bit(to))) return false;
    }
    if (pinned & bit(from)) {
        if (!(BB::Line[king_sq(us)][from] & bit(to))) return false;
    }
    return true;
}
