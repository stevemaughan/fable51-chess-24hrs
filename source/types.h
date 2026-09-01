#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <immintrin.h>

typedef uint64_t U64;
typedef uint16_t Move;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, PT_NB = 6, NO_PT = 6 };
// Piece = color*6 + type ; 12 = empty
enum { NO_PIECE = 12 };

inline constexpr int make_piece(int c, int pt) { return c * 6 + pt; }
inline constexpr int piece_type(int p) { return p % 6; }
inline constexpr int piece_color(int p) { return p / 6; }

// Squares a1=0 ... h8=63
inline constexpr int make_sq(int f, int r) { return r * 8 + f; }
inline constexpr int file_of(int s) { return s & 7; }
inline constexpr int rank_of(int s) { return s >> 3; }
inline constexpr int relative_rank(int c, int s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
inline constexpr int flip_sq(int s) { return s ^ 56; }

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8, SQ_NONE = 64
};

// Move encoding: from(6) | to(6)<<6 | flags(4)<<12
// flags: 0 normal, 1 castle, 2 en passant, 4..7 promotion (pt = KNIGHT + (flags&3))
enum { MF_NORMAL = 0, MF_CASTLE = 1, MF_EP = 2, MF_PROMO = 4 };
constexpr Move MOVE_NONE = 0;

inline constexpr Move make_move(int from, int to, int flags = 0) { return (Move)(from | (to << 6) | (flags << 12)); }
inline constexpr int move_from(Move m) { return m & 63; }
inline constexpr int move_to(Move m) { return (m >> 6) & 63; }
inline constexpr int move_flags(Move m) { return m >> 12; }
inline constexpr bool is_promo(Move m) { return (m >> 12) & MF_PROMO; }
inline constexpr int promo_type(Move m) { return KNIGHT + ((m >> 12) & 3); }
inline constexpr bool is_castle(Move m) { return move_flags(m) == MF_CASTLE; }
inline constexpr bool is_ep(Move m) { return move_flags(m) == MF_EP; }

// Castling rights bits
enum { WK_CASTLE = 1, WQ_CASTLE = 2, BK_CASTLE = 4, BQ_CASTLE = 8 };

// Scores
constexpr int VALUE_INFINITE = 32000;
constexpr int VALUE_MATE = 31000;
constexpr int VALUE_MATE_IN_MAX = VALUE_MATE - 256;
constexpr int VALUE_NONE = 32001;
constexpr int VALUE_DRAW = 0;
constexpr int MAX_PLY = 128;

inline int popcount(U64 b) { return (int)_mm_popcnt_u64(b); }
inline int lsb(U64 b) { return (int)_tzcnt_u64(b); }
inline int msb(U64 b) { return 63 - (int)_lzcnt_u64(b); }
inline int pop_lsb(U64& b) { int s = lsb(b); b &= b - 1; return s; }
inline constexpr U64 bit(int s) { return 1ULL << s; }

inline std::string sq_str(int s) { std::string r; r += (char)('a' + file_of(s)); r += (char)('1' + rank_of(s)); return r; }
inline std::string move_str(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s = sq_str(move_from(m)) + sq_str(move_to(m));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}
