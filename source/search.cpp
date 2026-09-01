#include "search.h"
#include "eval.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <mutex>
#include <thread>

TranspositionTable TT;
int MoveOverhead = 40;
extern std::mutex outMutex;

static const int SeeValue[7] = {100, 320, 330, 500, 950, 20000, 0};

// ---------------------------------------------------------------- SEE
bool see_ge(const Position& pos, Move m, int threshold) {
    if (move_flags(m) != MF_NORMAL) return 0 >= threshold;  // castle/ep/promo: treat conservatively
    int from = move_from(m), to = move_to(m);
    int swap = SeeValue[piece_type(pos.board[to]) < 6 && pos.board[to] != NO_PIECE ? piece_type(pos.board[to]) : 6] - threshold;
    if (pos.board[to] == NO_PIECE) swap = -threshold;
    if (swap < 0) return false;
    swap = SeeValue[piece_type(pos.board[from])] - swap;
    if (swap <= 0) return true;
    U64 occ = pos.pieces() ^ bit(from) ^ bit(to);
    int stm = pos.stm;
    U64 attackers = pos.attackers_to(to, occ);
    int res = 1;
    U64 bq = pos.byType[BISHOP] | pos.byType[QUEEN];
    U64 rq = pos.byType[ROOK] | pos.byType[QUEEN];
    while (true) {
        stm ^= 1;
        attackers &= occ;
        U64 stmAtt = attackers & pos.byColor[stm];
        if (!stmAtt) break;
        res ^= 1;
        U64 bb;
        if ((bb = stmAtt & pos.byType[PAWN])) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= BB::bishop_attacks(to, occ) & bq;
        } else if ((bb = stmAtt & pos.byType[KNIGHT])) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= bit(lsb(bb));
        } else if ((bb = stmAtt & pos.byType[BISHOP])) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= BB::bishop_attacks(to, occ) & bq;
        } else if ((bb = stmAtt & pos.byType[ROOK])) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= BB::rook_attacks(to, occ) & rq;
        } else if ((bb = stmAtt & pos.byType[QUEEN])) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= bit(lsb(bb));
            attackers |= (BB::bishop_attacks(to, occ) & bq) | (BB::rook_attacks(to, occ) & rq);
        } else {
            // king captures: only if no enemy attackers remain
            return (attackers & ~pos.byColor[stm]) ? (res ^ 1) : res;
        }
    }
    return res;
}

// ---------------------------------------------------------------- helpers
static inline int score_to_tt(int s, int ply) {
    if (s >= VALUE_MATE_IN_MAX) return s + ply;
    if (s <= -VALUE_MATE_IN_MAX) return s - ply;
    return s;
}
static inline int score_from_tt(int s, int ply) {
    if (s == VALUE_NONE) return VALUE_NONE;
    if (s >= VALUE_MATE_IN_MAX) return s - ply;
    if (s <= -VALUE_MATE_IN_MAX) return s + ply;
    return s;
}

static inline void update_hist(int16_t& h, int bonus) {
    int b = std::clamp(bonus, -2000, 2000);
    h += b - (int)h * std::abs(b) / 16384;
}

Searcher::Searcher() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++)
            lmrTable[d][m] = (d && m) ? (int)(0.8 + std::log(d) * std::log(m) / 2.4) : 0;
    clear_history();
}

void Searcher::clear_history() {
    memset(history, 0, sizeof(history));
    memset(captHist, 0, sizeof(captHist));
    memset(contHist, 0, sizeof(contHist));
    memset(counterMove, 0, sizeof(counterMove));
    memset(corrHist, 0, sizeof(corrHist));
}

bool Searcher::is_repetition(const Position& pos, int ply) const {
    // keyStack[0..ply] holds keys from root to current; gameKeys holds pre-root history
    int hm = pos.halfmove;
    int count = 0;
    // walk back through search stack
    int i = ply - 2;
    int steps = 2;
    while (steps <= hm) {
        U64 k;
        if (i >= 0) k = keyStack[i];
        else {
            int gi = (int)gameKeys.size() + i;  // i negative
            if (gi < 0) break;
            k = gameKeys[gi];
        }
        if (k == pos.key) {
            if (i >= 0) return true;      // repetition inside search tree: treat as draw
            if (++count >= 2) return true; // two prior occurrences in game history = threefold
        }
        i -= 2;
        steps += 2;
    }
    return false;
}

void Searcher::check_time() {
    if (useTime && elapsed() >= hardLimit) stopFlag = true;
    if (limits.nodes && nodes >= limits.nodes) stopFlag = true;
}

void Searcher::set_time_limits() {
    useTime = false;
    int mytime = rootPos.stm == WHITE ? limits.wtime : limits.btime;
    int myinc = rootPos.stm == WHITE ? limits.winc : limits.binc;
    if (limits.movetime > 0) {
        useTime = true;
        softLimit = hardLimit = std::max(1, limits.movetime - MoveOverhead);
        return;
    }
    if (mytime > 0) {
        useTime = true;
        int64_t t = std::max(1, mytime - MoveOverhead);
        int64_t soft, hard;
        if (limits.movestogo > 0) {
            int mtg = std::min(limits.movestogo, 40);
            soft = t / (mtg + 2) + myinc * 3 / 4;
            hard = std::min(t * 3 / 4, soft * 4);
        } else {
            soft = t / 24 + myinc * 3 / 4;
            hard = std::min(t / 3, soft * 4);
        }
        soft = std::min(soft, t);
        hard = std::max(hard, soft);
        softLimit = std::max<int64_t>(1, soft);
        hardLimit = std::max<int64_t>(1, hard);
    }
}

void Searcher::print_info(int depth, int score, Stack* ss, int bound) {
    if (quiet) return;
    int64_t ms = elapsed();
    U64 nps = ms > 0 ? nodes * 1000 / ms : nodes;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(selDepth);
    s += " multipv 1 score ";
    if (score >= VALUE_MATE_IN_MAX) s += "mate " + std::to_string((VALUE_MATE - score + 1) / 2);
    else if (score <= -VALUE_MATE_IN_MAX) s += "mate " + std::to_string(-(VALUE_MATE + score) / 2);
    else s += "cp " + std::to_string(score);
    if (bound == BOUND_LOWER) s += " lowerbound";
    else if (bound == BOUND_UPPER) s += " upperbound";
    s += " nodes " + std::to_string(nodes) + " nps " + std::to_string(nps) + " hashfull " + std::to_string(TT.hashfull()) + " time " + std::to_string(ms) + " pv";
    for (int i = 0; i < ss->pvLen; i++) s += " " + move_str(ss->pv[i]);
    std::lock_guard<std::mutex> lk(outMutex);
    printf("%s\n", s.c_str());
    fflush(stdout);
}

// ---------------------------------------------------------------- move ordering
static void score_moves(const Searcher& S, const Position& pos, MoveList& ml, Move ttMove, Stack* ss) {
    for (int i = 0; i < ml.size(); i++) {
        Move m = ml[i].move;
        int from = move_from(m), to = move_to(m);
        int pc = pos.board[from];
        if (m == ttMove) { ml[i].score = 2000000000; continue; }
        if (pos.board[to] != NO_PIECE || is_ep(m) || is_promo(m)) {
            int victim = is_ep(m) ? PAWN : (pos.board[to] == NO_PIECE ? 6 : piece_type(pos.board[to]));
            int vval = victim == 6 ? 0 : SeeValue[victim];
            if (is_promo(m)) vval += promo_type(m) == QUEEN ? 900 : -200;
            int sc = vval * 32 - piece_type(pc) + (victim == 6 ? 0 : S.captHist[pc][to][victim]) / 16;
            if (see_ge(pos, m, -50)) ml[i].score = 1000000000 + sc;
            else ml[i].score = -100000000 + sc;
        } else {
            if (m == ss->killers[0]) { ml[i].score = 900000000; continue; }
            if (m == ss->killers[1]) { ml[i].score = 899000000; continue; }
            if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->movedPiece != NO_PIECE &&
                S.counterMove[(ss - 1)->movedPiece][move_to((ss - 1)->currentMove)] == m) { ml[i].score = 898000000; continue; }
            int h = S.history[pos.stm][from][to];
            if ((ss - 1)->contHist) h += (*(ss - 1)->contHist)[pc][to];
            if ((ss - 2)->contHist) h += (*(ss - 2)->contHist)[pc][to];
            ml[i].score = h;
        }
    }
}

static inline Move pick_next(MoveList& ml, int idx) {
    int best = idx;
    for (int i = idx + 1; i < ml.size(); i++)
        if (ml[i].score > ml[best].score) best = i;
    std::swap(ml.list[idx], ml.list[best]);
    return ml[idx].move;
}

// ---------------------------------------------------------------- quiescence
int Searcher::qsearch(Position& pos, int alpha, int beta, Stack* ss) {
    nodes++;
    if ((nodes & 1023) == 0) check_time();
    if (stopFlag) return 0;
    ss->pvLen = 0;
    bool pvNode = beta - alpha > 1;
    if (ss->ply >= MAX_PLY) return Eval::evaluate(pos);
    if (ss->ply > selDepth) selDepth = ss->ply;
    if (is_repetition(pos, ss->ply) || pos.halfmove >= 100) return 0;

    bool ttHit;
    TTEntry* tte = TT.probe(pos.key, ttHit);
    Move ttMove = ttHit ? tte->move : MOVE_NONE;
    int ttScore = ttHit ? score_from_tt(tte->score, ss->ply) : VALUE_NONE;
    int ttBound = ttHit ? tte->bound() : BOUND_NONE;
    if (!pvNode && ttHit && ttScore != VALUE_NONE &&
        (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha)))
        return ttScore;

    bool inCheck = pos.in_check();
    int bestScore, eval = VALUE_NONE;
    if (inCheck) {
        bestScore = -VALUE_INFINITE;
        ss->staticEval = VALUE_NONE;
    } else {
        eval = (ttHit && tte->eval != VALUE_NONE) ? tte->eval : Eval::evaluate(pos);
        ss->staticEval = eval;
        bestScore = eval;
        if (ttHit && ttScore != VALUE_NONE && ((ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval)))
            bestScore = ttScore;
        if (bestScore >= beta) {
            if (!ttHit) TT.store(tte, pos.key, MOVE_NONE, score_to_tt(bestScore, ss->ply), eval, 0, BOUND_LOWER);
            return bestScore;
        }
        if (bestScore > alpha) alpha = bestScore;
    }

    MoveList ml;
    gen_moves(pos, ml, inCheck ? GEN_ALL : GEN_NOISY);
    score_moves(*this, pos, ml, ttMove, ss);
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Position next;
    for (int i = 0; i < ml.size(); i++) {
        Move m = pick_next(ml, i);
        if (!inCheck) {
            // skip losing captures
            if (ml[i].score < 0 && ml[i].score > -200000000) continue;
            // delta pruning
            int victim = is_ep(m) ? PAWN : (pos.board[move_to(m)] == NO_PIECE ? 6 : piece_type(pos.board[move_to(m)]));
            int gain = victim == 6 ? 0 : SeeValue[victim];
            if (is_promo(m)) gain += 800;
            if (eval + gain + 200 <= alpha && !pvNode) continue;
        }
        moveCount++;
        pos.make_move(m, next);
        keyStack[ss->ply + 1] = next.key;
        ss->currentMove = m;
        ss->movedPiece = pos.board[move_from(m)];
        ss->contHist = &contHist[ss->movedPiece][move_to(m)];
        int score = -qsearch(next, -beta, -alpha, ss + 1);
        if (stopFlag) return 0;
        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (pvNode) {
                    ss->pv[0] = m;
                    memcpy(ss->pv + 1, (ss + 1)->pv, (ss + 1)->pvLen * sizeof(Move));
                    ss->pvLen = (ss + 1)->pvLen + 1;
                }
                if (score >= beta) break;
            }
        }
    }
    if (inCheck && moveCount == 0) return -VALUE_MATE + ss->ply;
    int bound = bestScore >= beta ? BOUND_LOWER : (pvNode && bestMove != MOVE_NONE) ? BOUND_EXACT : BOUND_UPPER;
    TT.store(tte, pos.key, bestMove, score_to_tt(bestScore, ss->ply), eval, 0, bound);
    return bestScore;
}

// ---------------------------------------------------------------- main search
int Searcher::search(Position& pos, int alpha, int beta, int depth, Stack* ss, bool cutNode) {
    bool pvNode = beta - alpha > 1;
    bool rootNode = ss->ply == 0;
    ss->pvLen = 0;
    if (depth <= 0) return qsearch(pos, alpha, beta, ss);

    nodes++;
    if ((nodes & 1023) == 0) check_time();
    if (stopFlag) return 0;
    if (ss->ply >= MAX_PLY) return Eval::evaluate(pos);
    if (ss->ply > selDepth) selDepth = ss->ply;

    if (!rootNode) {
        if (is_repetition(pos, ss->ply) || pos.halfmove >= 100) return 1 - (int)(nodes & 2);
        alpha = std::max(alpha, -VALUE_MATE + ss->ply);
        beta = std::min(beta, VALUE_MATE - ss->ply - 1);
        if (alpha >= beta) return alpha;
    }

    bool inCheck = pos.in_check();
    (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;
    (ss + 1)->excluded = MOVE_NONE;
    Move excluded = ss->excluded;

    bool ttHit = false;
    TTEntry* tte = nullptr;
    Move ttMove = MOVE_NONE;
    int ttScore = VALUE_NONE, ttDepth = 0, ttBound = BOUND_NONE, ttEval = VALUE_NONE;
    if (!excluded) {
        tte = TT.probe(pos.key, ttHit);
        if (ttHit) {
            ttMove = tte->move;
            ttScore = score_from_tt(tte->score, ss->ply);
            ttDepth = tte->depth;
            ttBound = tte->bound();
            ttEval = tte->eval;
        }
        if (!pvNode && ttHit && ttDepth >= depth && ttScore != VALUE_NONE &&
            (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha)))
            return ttScore;
    }

    int eval, rawEval = VALUE_NONE;
    int corrIdx = (int)(pos.pawnKey & 16383);
    if (inCheck) {
        eval = ss->staticEval = VALUE_NONE;
    } else {
        rawEval = (ttEval != VALUE_NONE) ? ttEval : Eval::evaluate(pos);
#ifdef NO_CORRHIST
        eval = ss->staticEval = rawEval;
#else
        eval = ss->staticEval = std::clamp(rawEval + corrHist[pos.stm][corrIdx] / 256, -VALUE_MATE_IN_MAX + 1, VALUE_MATE_IN_MAX - 1);
#endif
        if (ttHit && ttScore != VALUE_NONE && ((ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval)))
            eval = ttScore;
    }
    bool improving = !inCheck && ss->ply >= 2 && (ss - 2)->staticEval != VALUE_NONE && ss->staticEval > (ss - 2)->staticEval;

    if (!pvNode && !inCheck && !excluded) {
        // reverse futility pruning
        if (depth <= 8 && eval < VALUE_MATE_IN_MAX && eval - 75 * depth + (improving ? 50 : 0) >= beta)
            return eval;
        // razoring
        if (depth <= 3 && eval + 200 * depth <= alpha) {
            int v = qsearch(pos, alpha, beta, ss);
            if (v <= alpha) return v;
        }
        // null move pruning
        if (depth >= 3 && eval >= beta && ss->staticEval >= beta - 20 * depth && (ss - 1)->currentMove != MOVE_NONE && pos.has_non_pawn(pos.stm)) {
            int R = 3 + depth / 3 + std::min((eval - beta) / 200, 3);
            Position next;
            pos.make_null(next);
            keyStack[ss->ply + 1] = next.key;
            ss->currentMove = MOVE_NONE;
            ss->movedPiece = NO_PIECE;
            ss->contHist = nullptr;
            int score = -search(next, -beta, -beta + 1, depth - R, ss + 1, !cutNode);
            if (stopFlag) return 0;
            if (score >= beta) return score >= VALUE_MATE_IN_MAX ? beta : score;
        }
    }
    // probcut
#ifndef NO_PROBCUT
    if (!pvNode && !inCheck && !excluded && depth >= 5 && std::abs(beta) < VALUE_MATE_IN_MAX) {
        int probBeta = beta + 180;
        if (!(ttHit && ttDepth >= depth - 3 && ttScore != VALUE_NONE && ttScore < probBeta)) {
            MoveList pml;
            gen_moves(pos, pml, GEN_NOISY);
            score_moves(*this, pos, pml, ttMove, ss);
            Position pnext;
            for (int pi = 0; pi < pml.size(); pi++) {
                Move pm = pick_next(pml, pi);
                if (!see_ge(pos, pm, probBeta - ss->staticEval)) continue;
                pos.make_move(pm, pnext);
                keyStack[ss->ply + 1] = pnext.key;
                ss->currentMove = pm;
                ss->movedPiece = pos.board[move_from(pm)];
                ss->contHist = &contHist[ss->movedPiece][move_to(pm)];
                int v = -qsearch(pnext, -probBeta, -probBeta + 1, ss + 1);
                if (v >= probBeta) v = -search(pnext, -probBeta, -probBeta + 1, depth - 4, ss + 1, !cutNode);
                if (stopFlag) return 0;
                if (v >= probBeta) {
                    TT.store(tte, pos.key, pm, score_to_tt(v, ss->ply), rawEval, depth - 3, BOUND_LOWER);
                    return v;
                }
            }
        }
    }
#endif
    // internal iterative reduction
    if (depth >= 4 && ttMove == MOVE_NONE && !excluded) depth--;

    MoveList ml;
    gen_moves(pos, ml, GEN_ALL);
    score_moves(*this, pos, ml, ttMove, ss);

    int bestScore = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move quietsTried[128];
    int quietCount = 0;
    Move capturesTried[64];
    int captureCount = 0;
    Position next;
    bool skipQuiets = false;

    for (int i = 0; i < ml.size(); i++) {
        Move m = pick_next(ml, i);
        if (m == excluded) continue;
        int from = move_from(m), to = move_to(m);
        int pc = pos.board[from];
        bool isCapture = pos.board[to] != NO_PIECE || is_ep(m);
        bool isNoisy = isCapture || is_promo(m);
        bool isKillerOrCounter = ml[i].score >= 898000000 && ml[i].score < 1000000000;
        if (skipQuiets && !isNoisy && !isKillerOrCounter) continue;
        moveCount++;

        int hist = 0;
        if (!isNoisy) {
            hist = history[pos.stm][from][to];
            if ((ss - 1)->contHist) hist += (*(ss - 1)->contHist)[pc][to];
            if ((ss - 2)->contHist) hist += (*(ss - 2)->contHist)[pc][to];
        }

        if (!rootNode && bestScore > -VALUE_MATE_IN_MAX) {
            if (!isNoisy) {
                // late move pruning
                if (!inCheck && depth <= 8 && moveCount >= (3 + depth * depth) / (improving ? 1 : 2)) { skipQuiets = true; }
                // futility pruning
                if (!inCheck && depth <= 8 && ss->staticEval + 120 + 100 * depth <= alpha && std::abs(alpha) < VALUE_MATE_IN_MAX) { skipQuiets = true; continue; }
                // history pruning
                if (depth <= 4 && hist < -3000 * depth) continue;
                // SEE pruning for quiets
                if (depth <= 8 && !see_ge(pos, m, -40 * depth)) continue;
            } else {
                if (depth <= 8 && !see_ge(pos, m, -100 * depth)) continue;
            }
        }

        int extension = 0;
        // singular extension
        if (!rootNode && depth >= 8 && m == ttMove && !excluded && ttBound != BOUND_UPPER && ttDepth >= depth - 3 &&
            std::abs(ttScore) < VALUE_MATE_IN_MAX) {
            int sBeta = ttScore - 2 * depth;
            ss->excluded = m;
            int sScore = search(pos, sBeta - 1, sBeta, (depth - 1) / 2, ss, cutNode);
            ss->excluded = MOVE_NONE;
            if (stopFlag) return 0;
            if (sScore < sBeta) extension = 1;
            else if (sBeta >= beta) return sBeta;  // multi-cut
            else if (ttScore >= beta) extension = -1;
        }

        pos.make_move(m, next);
        keyStack[ss->ply + 1] = next.key;
        TT.prefetch(next.key);
        bool givesCheck = next.in_check();
        if (givesCheck && extension == 0 && ss->ply < 2 * rootDepth && see_ge(pos, m, 0)) extension = 1;

        ss->currentMove = m;
        ss->movedPiece = pc;
        ss->contHist = &contHist[pc][to];
        int newDepth = depth - 1 + extension;
        int score;
        bool doFull = true;

        if (depth >= 3 && moveCount > 1 + (rootNode ? 1 : 0) && (!isNoisy || !pvNode)) {
            int R = lmrTable[std::min(depth, 63)][std::min(moveCount, 63)];
            if (!isNoisy) {
                if (!improving) R++;
                if (cutNode) R++;
                if (pvNode) R--;
                if (isKillerOrCounter) R--;
                R -= std::clamp(hist / 6000, -2, 2);
            } else {
                R = R / 2;
                if (cutNode) R++;
            }
            if (givesCheck) R--;
            if (inCheck) R--;
            R = std::clamp(R, 0, newDepth - 1);
            if (R > 0) {
                score = -search(next, -alpha - 1, -alpha, newDepth - R, ss + 1, true);
                doFull = score > alpha;
            }
        }
        if (doFull && (!pvNode || moveCount > 1)) {
            score = -search(next, -alpha - 1, -alpha, newDepth, ss + 1, !cutNode);
        }
        if (pvNode && (moveCount == 1 || score > alpha)) {
            score = -search(next, -beta, -alpha, newDepth, ss + 1, false);
        }
        if (stopFlag) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (pvNode) {
                    ss->pv[0] = m;
                    memcpy(ss->pv + 1, (ss + 1)->pv, (ss + 1)->pvLen * sizeof(Move));
                    ss->pvLen = (ss + 1)->pvLen + 1;
                }
                if (score >= beta) {
                    int bonus = std::min(2000, 16 * depth * depth + 80 * depth);
                    if (!isNoisy) {
                        if (ss->killers[0] != m) { ss->killers[1] = ss->killers[0]; ss->killers[0] = m; }
                        if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->movedPiece != NO_PIECE)
                            counterMove[(ss - 1)->movedPiece][move_to((ss - 1)->currentMove)] = m;
                        update_hist(history[pos.stm][from][to], bonus);
                        if ((ss - 1)->contHist) update_hist((*(ss - 1)->contHist)[pc][to], bonus);
                        if ((ss - 2)->contHist) update_hist((*(ss - 2)->contHist)[pc][to], bonus);
                        for (int j = 0; j < quietCount; j++) {
                            Move q = quietsTried[j];
                            int qpc = pos.board[move_from(q)];
                            update_hist(history[pos.stm][move_from(q)][move_to(q)], -bonus);
                            if ((ss - 1)->contHist) update_hist((*(ss - 1)->contHist)[qpc][move_to(q)], -bonus);
                            if ((ss - 2)->contHist) update_hist((*(ss - 2)->contHist)[qpc][move_to(q)], -bonus);
                        }
                    } else if (isCapture) {
                        int victim = is_ep(m) ? PAWN : piece_type(pos.board[to]);
                        update_hist(captHist[pc][to][victim], bonus);
                    }
                    for (int j = 0; j < captureCount; j++) {
                        Move c = capturesTried[j];
                        int cpc = pos.board[move_from(c)];
                        int victim = is_ep(c) ? PAWN : piece_type(pos.board[move_to(c)]);
                        update_hist(captHist[cpc][move_to(c)][victim], -bonus);
                    }
                    break;
                }
            }
        }
        if (!isNoisy) { if (quietCount < 128) quietsTried[quietCount++] = m; }
        else if (isCapture) { if (captureCount < 64) capturesTried[captureCount++] = m; }
    }

    if (moveCount == 0) {
        if (excluded) return alpha;
        return inCheck ? -VALUE_MATE + ss->ply : 0;
    }
    if (!excluded) {
        int bound = bestScore >= beta ? BOUND_LOWER : (pvNode && bestMove != MOVE_NONE) ? BOUND_EXACT : BOUND_UPPER;
        TT.store(tte, pos.key, bestMove, score_to_tt(bestScore, ss->ply), rawEval, depth, bound);
        if (!inCheck && std::abs(bestScore) < VALUE_MATE_IN_MAX && (bestMove == MOVE_NONE || !(pos.is_capture(bestMove) || is_promo(bestMove))) &&
            ((bestScore > ss->staticEval && bound != BOUND_UPPER) || (bestScore < ss->staticEval && bound != BOUND_LOWER))) {
            int w = std::min(depth + 1, 16);
            int diff = std::clamp((bestScore - ss->staticEval) * 256, -8192 * 256, 8192 * 256);
            int32_t& c = corrHist[pos.stm][corrIdx];
            c = (int32_t)(((int64_t)c * (256 - w) + (int64_t)diff * w) / 256);
            c = std::clamp(c, -64 * 256, 64 * 256);
        }
    }
    return bestScore;
}

// ---------------------------------------------------------------- iterative deepening
void Searcher::start() {
    startTime = std::chrono::steady_clock::now();
    nodes = 0;
    selDepth = 0;
    set_time_limits();
    TT.new_search();

    Stack* ss = stackBuf + 4;
    memset(stackBuf, 0, sizeof(stackBuf));
    for (int i = 0; i < MAX_PLY + 10; i++) { stackBuf[i].ply = i - 4; stackBuf[i].staticEval = VALUE_NONE; }
    keyStack[0] = rootPos.key;

    MoveList rootMoves;
    gen_moves(rootPos, rootMoves, GEN_ALL);
    bestMove = rootMoves.size() ? rootMoves[0].move : MOVE_NONE;
    int maxDepth = limits.depth > 0 ? limits.depth : MAX_PLY - 1;
    int score = 0, prevScore = 0;
    Move prevBest = MOVE_NONE;
    int stableCount = 0;

    for (int depth = 1; depth <= maxDepth && rootMoves.size() > 1; depth++) {
        int delta = 20, alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 5) { alpha = std::max(score - delta, -VALUE_INFINITE); beta = std::min(score + delta, VALUE_INFINITE); }
        rootDepth = depth;
        while (true) {
            selDepth = 0;
            score = search(rootPos, alpha, beta, depth, ss, false);
            if (stopFlag) break;
            if (score <= alpha) {
                print_info(depth, score, ss, BOUND_UPPER);
                beta = (alpha + beta) / 2;
                alpha = std::max(score - delta, -VALUE_INFINITE);
            } else if (score >= beta) {
                print_info(depth, score, ss, BOUND_LOWER);
                beta = std::min(score + delta, VALUE_INFINITE);
            } else break;
            delta += delta / 2;
        }
        if (stopFlag) break;
        if (ss->pvLen > 0) bestMove = ss->pv[0];
        rootScore = score;
        print_info(depth, score, ss, BOUND_EXACT);
        if (bestMove == prevBest) stableCount++; else stableCount = 0;
        prevBest = bestMove;
        if (useTime) {
            double factor = 1.0;
            if (stableCount >= 6) factor = 0.6;
            else if (stableCount >= 3) factor = 0.75;
            else if (stableCount >= 1) factor = 0.9;
#ifndef NO_TIMEEXT
            if (depth >= 6 && score < prevScore - 30) factor = std::max(factor, 1.3);
            if (depth >= 6 && score < prevScore - 80) factor = std::max(factor, 1.6);
#endif
            if (elapsed() >= softLimit * factor) break;
        }
        prevScore = score;
        if (std::abs(score) >= VALUE_MATE_IN_MAX && depth >= 10 && !limits.infinite && limits.depth == 0) break;
    }
    if (rootMoves.size() == 1 && limits.depth == 0 && !limits.infinite) {
        // single legal move: quick search for info only
        score = search(rootPos, -VALUE_INFINITE, VALUE_INFINITE, 1, ss, false);
        print_info(1, score, ss, BOUND_EXACT);
    }
    (void)prevScore;
    if (rootMoves.size() == 1) rootScore = score;
    if (quiet) return;
    // In infinite mode we must wait for stop before printing bestmove
    while (limits.infinite && !stopFlag) std::this_thread::yield();
    std::lock_guard<std::mutex> lk(outMutex);
    printf("bestmove %s\n", move_str(bestMove).c_str());
    fflush(stdout);
}
