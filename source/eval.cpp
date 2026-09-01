#include "eval.h"
#include <algorithm>

int PSQ_MG[12][64];
int PSQ_EG[12][64];
const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};


namespace Eval {
int PieceValue[6] = {100, 320, 330, 500, 950, 0};
bool UsePawnHash = true;

// ---------------------------------------------------------------- PeSTO tables (chessprogramming.org), rank 8 first
#include "params.h"

// pawn hash
struct PawnEntry {
    U64 key;
    Score score;         // white perspective, pawn-only terms
    U64 passed[2];
    U64 pawnAttacks[2];
};
static const size_t PAWN_HASH_SIZE = 1 << 15;
static PawnEntry pawnTable[PAWN_HASH_SIZE];

void init() {
    for (int pt = 0; pt < 6; pt++)
        for (int s = 0; s < 64; s++) {
            PSQ_MG[make_piece(WHITE, pt)][s] = mg_value[pt] + mg_tables[pt][flip_sq(s)];
            PSQ_EG[make_piece(WHITE, pt)][s] = eg_value[pt] + eg_tables[pt][flip_sq(s)];
            PSQ_MG[make_piece(BLACK, pt)][s] = mg_value[pt] + mg_tables[pt][s];
            PSQ_EG[make_piece(BLACK, pt)][s] = eg_value[pt] + eg_tables[pt][s];
        }
    memset(pawnTable, 0, sizeof(pawnTable));
}

struct EvalInfo {
    U64 attackedBy[2][7];   // by piece type; [6] = all
    U64 attackedBy2[2];
    U64 kingZone[2];
    U64 mobilityArea[2];
    int kingAttackersCount[2];
    int kingAttackWeight[2];
    U64 passed[2];
};

template <int Us>
static Score eval_pawns(const Position& pos, PawnEntry& pe) {
    constexpr int Them = Us ^ 1;
    constexpr int Up = Us == WHITE ? 8 : -8;
    U64 ourPawns = pos.pieces(Us, PAWN);
    U64 theirPawns = pos.pieces(Them, PAWN);
    Score score = 0;
    U64 b = ourPawns;
    pe.passed[Us] = 0;
    U64 theirAttacks = BB::pawn_attacks_bb(Them, theirPawns);
    U64 ourAttacks = BB::pawn_attacks_bb(Us, ourPawns);
    pe.pawnAttacks[Us] = ourAttacks;
    while (b) {
        int s = pop_lsb(b);
        int f = file_of(s), r = relative_rank(Us, s);
        U64 adjacent = BB::AdjacentFiles[f] & ourPawns;
        U64 phalanx = adjacent & rank_bb(rank_of(s));
        U64 supported = adjacent & rank_bb(rank_of(s - Up));
        bool opposed = theirPawns & BB::ForwardRanks[Us][rank_of(s)] & file_bb(f);
        bool doubled = ourPawns & bit(s - Up);
        if (!adjacent) score += PawnIsolated;
        else if (phalanx) score += PawnPhalanx[r];
        else if (supported) score += PawnConnected[r];
        if (doubled) score += PawnDoubled;
        // backward: no own pawns behind on adjacent files, and the stop square is attacked by enemy pawn
        if (!(adjacent & ~BB::ForwardRanks[Us][rank_of(s)]) && (theirAttacks & bit(s + Up)) && !opposed) score += PawnBackward;
        // passed
        if (!(BB::PassedMask[Us][s] & theirPawns)) pe.passed[Us] |= bit(s);
    }
    return score;
}

template <int Us>
static Score eval_pieces(const Position& pos, EvalInfo& ei) {
    constexpr int Them = Us ^ 1;
    Score score = 0;
    U64 occ = pos.pieces();
    int ksq = pos.king_sq(Us);
    int theirKsq = pos.king_sq(Them);
    U64 outpostRanks = Us == WHITE ? (rank_bb(3) | rank_bb(4) | rank_bb(5)) : (rank_bb(2) | rank_bb(3) | rank_bb(4));
    U64 ourPawns = pos.pieces(Us, PAWN);
    U64 theirPawns = pos.pieces(Them, PAWN);
    U64 pawnAttacks = ei.attackedBy[Us][PAWN];
    U64 theirPawnAttacks = ei.attackedBy[Them][PAWN];
    U64 behindPawn = Us == WHITE ? BB::shift_s(ourPawns) : BB::shift_n(ourPawns);

    for (int pt = KNIGHT; pt <= QUEEN; pt++) {
        U64 b = pos.pieces(Us, pt);
        ei.attackedBy[Us][pt] = 0;
        while (b) {
            int s = pop_lsb(b);
            U64 att;
            if (pt == KNIGHT) att = BB::KnightAttacks[s];
            else if (pt == BISHOP) att = BB::bishop_attacks(s, occ ^ pos.pieces(Us, QUEEN));
            else if (pt == ROOK) att = BB::rook_attacks(s, occ ^ pos.pieces(Us, QUEEN) ^ pos.pieces(Us, ROOK));
            else att = BB::queen_attacks(s, occ);
            if (pos.pinned & bit(s)) att &= BB::Line[ksq][s];
            ei.attackedBy2[Us] |= ei.attackedBy[Us][6] & att;
            ei.attackedBy[Us][6] |= att;
            ei.attackedBy[Us][pt] |= att;
            if (att & ei.kingZone[Them]) {
                ei.kingAttackersCount[Us]++;
                ei.kingAttackWeight[Us] += KingAttackWeight[pt] * popcount(att & ei.kingZone[Them]);
            }
            int mob = popcount(att & ei.mobilityArea[Us]);
            if (pt == KNIGHT) {
                score += MobilityKnight[mob];
                score += KnightPawnCount * (popcount(ourPawns) - 5);
                if ((bit(s) & outpostRanks) && (bit(s) & pawnAttacks) && !(BB::PassedMask[Us][s] & ~file_bb(file_of(s)) & theirPawns)) score += KnightOutpost;
                if (bit(s) & behindPawn) score += MinorBehindPawn;
            } else if (pt == BISHOP) {
                score += MobilityBishop[mob];
                score += BishopPawns * popcount(ourPawns & ((bit(s) & 0x55AA55AA55AA55AAULL) ? 0x55AA55AA55AA55AAULL : ~0x55AA55AA55AA55AAULL));
                if ((bit(s) & outpostRanks) && (bit(s) & pawnAttacks) && !(BB::PassedMask[Us][s] & ~file_bb(file_of(s)) & theirPawns)) score += BishopOutpost;
                if (bit(s) & behindPawn) score += MinorBehindPawn;
            } else if (pt == ROOK) {
                score += MobilityRook[mob];
                score += RookPawnCount * (popcount(ourPawns) - 5);
                U64 fileBB = file_bb(file_of(s));
                if (!(fileBB & ourPawns)) score += (fileBB & theirPawns) ? RookSemiOpenFile : RookOpenFile;
                if (relative_rank(Us, s) == 6 && relative_rank(Us, theirKsq) == 7) score += RookOnSeventh;
            } else {
                score += MobilityQueen[mob];
            }
        }
    }
    if (popcount(pos.pieces(Us, BISHOP)) >= 2) score += BishopPair;
    (void)theirPawnAttacks;
    return score;
}

template <int Us>
static Score eval_king(const Position& pos, EvalInfo& ei) {
    constexpr int Them = Us ^ 1;
    Score score = 0;
    int ksq = pos.king_sq(Us);
    U64 ourPawns = pos.pieces(Us, PAWN);
    U64 theirPawns = pos.pieces(Them, PAWN);
    // pawn shield
    int kf = file_of(ksq);
    int kr = rank_of(ksq);
    // pawn storm: enemy pawns advancing on files near the king
    {
        U64 storm = theirPawns & (file_bb(kf) | BB::AdjacentFiles[kf]) & BB::ForwardRanks[Us][kr];
        while (storm) {
            int ps = pop_lsb(storm);
            int d = std::abs(rank_of(ps) - kr);
            if (d <= 3) score += PawnStorm[d];
        }
    }
    for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
        U64 fileBB = file_bb(f);
        U64 shield = fileBB & ourPawns & BB::ForwardRanks[Us][kr];
        if (shield) {
            int ps = Us == WHITE ? lsb(shield) : msb(shield);
            int dist = std::abs(rank_of(ps) - kr) - 1;
            if (dist < 3) score += KingShield[dist];
        }
        if (!(fileBB & ourPawns)) score += (fileBB & theirPawns) ? KingSemiOpenFile : KingOpenFile;
    }
    // attacks on king zone
    if (ei.kingAttackersCount[Them] >= 2) {
        U64 occ = pos.pieces();
        U64 weak = ei.attackedBy[Them][6] & ~ei.attackedBy2[Us] & (~ei.attackedBy[Us][6] | ei.attackedBy[Us][KING] | ei.attackedBy[Us][QUEEN]);
        U64 safe = ~pos.pieces(Them) & (~ei.attackedBy[Us][6] | (weak & ei.attackedBy2[Them]));
        U64 rookChecks = BB::rook_attacks(ksq, occ) & safe & ei.attackedBy[Them][ROOK];
        U64 bishopChecks = BB::bishop_attacks(ksq, occ) & safe & ei.attackedBy[Them][BISHOP];
        U64 queenChecks = BB::queen_attacks(ksq, occ) & safe & ei.attackedBy[Them][QUEEN];
        U64 knightChecks = BB::KnightAttacks[ksq] & safe & ei.attackedBy[Them][KNIGHT];
        int danger = ei.kingAttackWeight[Them] + KingDanger[2] * popcount(ei.kingZone[Us] & weak) +
                     (knightChecks ? SafeCheckWeight[KNIGHT] : 0) + (bishopChecks ? SafeCheckWeight[BISHOP] : 0) +
                     (rookChecks ? SafeCheckWeight[ROOK] : 0) + (queenChecks ? SafeCheckWeight[QUEEN] : 0) -
                     (pos.pieces(Them, QUEEN) ? 0 : KingDanger[3]) - KingDanger[4] * popcount(ei.attackedBy[Us][KNIGHT] & ei.kingZone[Us]);
        if (danger > 0) score -= S(danger * danger / std::max(1, KingDanger[0]), danger / std::max(1, KingDanger[1]));
    }
    return score;
}

template <int Us>
static Score eval_threats(const Position& pos, EvalInfo& ei) {
    constexpr int Them = Us ^ 1;
    constexpr int Up = Us == WHITE ? 8 : -8;
    Score score = 0;
    U64 theirNonPawn = pos.pieces(Them) & ~pos.pieces(Them, PAWN);
    U64 theirMajors = pos.pieces(Them, ROOK, QUEEN);
    // enemy pieces attacked by our pawns
    score += ThreatByPawn * popcount(theirNonPawn & ei.attackedBy[Us][PAWN]);
    // minor attacks on majors
    score += ThreatMinorOnMajor * popcount(theirMajors & (ei.attackedBy[Us][KNIGHT] | ei.attackedBy[Us][BISHOP]));
    score += ThreatRookOnQueen * popcount(pos.pieces(Them, QUEEN) & ei.attackedBy[Us][ROOK]);
    // hanging pieces: attacked by us and not defended
    U64 weak = pos.pieces(Them) & ~ei.attackedBy[Them][6] & ei.attackedBy[Us][6];
    score += ThreatHanging * popcount(weak);
    score += ThreatByKing * popcount(weak & ei.attackedBy[Us][KING]);
    // safe pawn pushes threatening pieces
    U64 empty = ~pos.pieces();
    U64 pushes = (Us == WHITE ? BB::shift_n(pos.pieces(Us, PAWN)) : BB::shift_s(pos.pieces(Us, PAWN))) & empty;
    pushes |= (Us == WHITE ? BB::shift_n(pushes & rank_bb(2)) : BB::shift_s(pushes & rank_bb(5))) & empty;
    pushes &= ~ei.attackedBy[Them][PAWN] & (ei.attackedBy[Us][6] | ~ei.attackedBy[Them][6]);
    U64 pushAttacks = BB::pawn_attacks_bb(Us, pushes) & theirNonPawn;
    score += PawnPushThreat * popcount(pushAttacks);
    (void)Up;
    return score;
}

template <int Us>
static Score eval_passed(const Position& pos, EvalInfo& ei) {
    constexpr int Them = Us ^ 1;
    constexpr int Up = Us == WHITE ? 8 : -8;
    Score score = 0;
    U64 b = ei.passed[Us];
    int ksq = pos.king_sq(Us), theirKsq = pos.king_sq(Them);
    while (b) {
        int s = pop_lsb(b);
        int r = relative_rank(Us, s);
        Score bonus = PassedRank[r];
        if (r >= 3) {
            int front = s + Up;
            int w = r - 2;  // 1..4
            // king proximity (endgame)
            int kd = std::min(BB::SqDist[theirKsq][front], 5) * 5 - std::min(BB::SqDist[ksq][front], 5) * 2;
            bonus += S(0, kd * w);
            if (!(pos.pieces() & bit(front))) {
                U64 path = BB::ForwardRanks[Us][rank_of(s)] & file_bb(file_of(s));
                bool safePath = !(path & ei.attackedBy[Them][6]);
                bool defended = (path & ei.attackedBy[Us][6]) == path;
                if (safePath) bonus += S(6 * w, 12 * w);
                else if (!(bit(front) & ei.attackedBy[Them][6])) bonus += S(3 * w, 6 * w);
                if (defended) bonus += S(2 * w, 4 * w);
            } else if (pos.pieces(Them) & bit(front)) {
                bonus -= S(2 * w, 4 * w);
                bonus += PassedBlocked;
            }
            U64 behind = BB::ForwardRanks[Them][rank_of(s)] & file_bb(file_of(s));
            if (behind & pos.pieces(Us, ROOK, QUEEN) && !(BB::Between[s][Us == WHITE ? lsb(behind & pos.pieces(Us, ROOK, QUEEN)) : msb(behind & pos.pieces(Us, ROOK, QUEEN))] & pos.pieces())) bonus += RookBehindPasser;
        }
        score += bonus;
    }
    return score;
}

int evaluate(const Position& pos) {
    EvalInfo ei;
    memset(&ei, 0, sizeof(ei));
    // pawn attacks and king info
    for (int c = 0; c < 2; c++) {
        int ksq = pos.king_sq(c);
        ei.attackedBy[c][PAWN] = BB::pawn_attacks_bb(c, pos.pieces(c, PAWN));
        ei.attackedBy[c][KING] = BB::KingAttacks[ksq];
        ei.attackedBy2[c] = ei.attackedBy[c][PAWN] & ei.attackedBy[c][KING];
        ei.attackedBy[c][6] = ei.attackedBy[c][PAWN] | ei.attackedBy[c][KING];
        // king zone: king ring plus the ring shifted one rank forward
        U64 zone = BB::KingAttacks[ksq] | bit(ksq);
        zone |= c == WHITE ? BB::shift_n(zone) : BB::shift_s(zone);
        ei.kingZone[c] = zone;
    }
    for (int c = 0; c < 2; c++) {
        U64 lowRanks = c == WHITE ? (rank_bb(1) | rank_bb(2)) : (rank_bb(6) | rank_bb(5));
        U64 blockedPawns = pos.pieces(c, PAWN) & ((c == WHITE ? BB::shift_s(pos.pieces()) : BB::shift_n(pos.pieces())) | lowRanks);
        ei.mobilityArea[c] = ~(ei.attackedBy[c ^ 1][PAWN] | blockedPawns | pos.pieces(c, KING) | pos.pieces(c, QUEEN));
    }

    // pawn structure (hashed)
    PawnEntry localPe;
    PawnEntry& pe = UsePawnHash ? pawnTable[pos.pawnKey & (PAWN_HASH_SIZE - 1)] : localPe;
    if (!UsePawnHash) localPe.key = 0;
    if (pe.key != pos.pawnKey) {
        pe.key = pos.pawnKey;
        pe.score = eval_pawns<WHITE>(pos, pe) - eval_pawns<BLACK>(pos, pe);
    }
    ei.passed[WHITE] = pe.passed[WHITE];
    ei.passed[BLACK] = pe.passed[BLACK];

    Score score = pe.score;
    score += eval_pieces<WHITE>(pos, ei) - eval_pieces<BLACK>(pos, ei);
    score += eval_king<WHITE>(pos, ei) - eval_king<BLACK>(pos, ei);
    score += eval_threats<WHITE>(pos, ei) - eval_threats<BLACK>(pos, ei);
    score += eval_passed<WHITE>(pos, ei) - eval_passed<BLACK>(pos, ei);
    // space: safe squares in the centre files behind our pawns, middlegame only
    {
        const U64 centerFiles = file_bb(2) | file_bb(3) | file_bb(4) | file_bb(5);
        for (int c = 0; c < 2; c++) {
            U64 area = centerFiles & (c == WHITE ? (rank_bb(1) | rank_bb(2) | rank_bb(3)) : (rank_bb(6) | rank_bb(5) | rank_bb(4)));
            U64 safe = area & ~pos.pieces(c, PAWN) & ~ei.attackedBy[c ^ 1][PAWN];
            U64 behind = pos.pieces(c, PAWN);
            behind |= c == WHITE ? (behind >> 8) | (behind >> 16) : (behind << 8) | (behind << 16);
            int cnt = popcount(safe & behind & ~ei.attackedBy[c ^ 1][6]) + popcount(safe);
            int pieces = popcount(pos.pieces(c)) - popcount(pos.pieces(c, PAWN)) - 1;
            Score sp = Space * (cnt * pieces / 8);
            score += c == WHITE ? sp : -sp;
        }
    }
    score += pos.stm == WHITE ? Tempo : -Tempo;

    int mg = pos.mg + mg_of(score);
    int eg = pos.eg + eg_of(score);

    // endgame scaling: drawish endings
    int scale = 64;
    {
        int strong = eg > 0 ? WHITE : BLACK;
        int weak = strong ^ 1;
        int sp = popcount(pos.pieces(strong, PAWN));
        int snp = pos.non_pawn_material(strong), wnp = pos.non_pawn_material(weak);
        if (sp == 0) {
            if (snp <= 3) scale = 0;                       // lone minor cannot win
            else if (snp - wnp <= 3) scale = snp >= 10 ? 32 : 8;  // small material edge without pawns
        } else if (sp <= 2 && snp <= wnp) {
            scale = 40 + 8 * sp;
        }
        // opposite coloured bishops
        if (scale == 64 && pos.pieces_pt(BISHOP) && popcount(pos.pieces(WHITE, BISHOP)) == 1 && popcount(pos.pieces(BLACK, BISHOP)) == 1) {
            bool oppColor = ((pos.pieces(WHITE, BISHOP) & 0x55AA55AA55AA55AAULL) != 0) != ((pos.pieces(BLACK, BISHOP) & 0x55AA55AA55AA55AAULL) != 0);
            if (oppColor) {
                if (snp == 3 && wnp == 3) scale = 32;
                else scale = 52;
            }
        }
    }

    int phase = pos.phase > 24 ? 24 : pos.phase;
    int v = (mg * phase + eg * (24 - phase) * scale / 64) / 24;
    return pos.stm == WHITE ? v : -v;
}
}
