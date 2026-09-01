#pragma once
#include "search.h"

enum PickStage {
    ST_TT, ST_GEN_NOISY, ST_GOOD_NOISY, ST_KILLER1, ST_KILLER2, ST_COUNTER, ST_GEN_QUIET, ST_QUIET, ST_BAD_NOISY, ST_DONE,
    QS_TT, QS_GEN, QS_NOISY, QS_DONE
};

struct MovePicker {
    const Position& pos;
    const Searcher& S;
    Stack* ss;
    Move ttMove;
    Move killer1, killer2, counter;
    int stage;
    bool skipQuiets = false;
    bool inCheckQs = false;
    MoveList noisy, quiets;
    ExtMove bad[64];
    int badCount = 0;
    int idx = 0;
    int seeThreshold;
    int lastStage = ST_TT;

    MovePicker(const Position& p, const Searcher& s, Stack* st, Move tt, bool qs, int seeTh = -50)
        : pos(p), S(s), ss(st), ttMove(tt), seeThreshold(seeTh) {
        killer1 = ss->killers[0];
        killer2 = ss->killers[1];
        counter = MOVE_NONE;
        if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->movedPiece != NO_PIECE)
            counter = S.counterMove[(ss - 1)->movedPiece][move_to((ss - 1)->currentMove)];
        if (qs) {
            inCheckQs = pos.in_check();
            stage = QS_TT;
        } else stage = ST_TT;
        if (ttMove != MOVE_NONE && !(pos.is_pseudo_legal(ttMove) && is_legal(pos, ttMove))) ttMove = MOVE_NONE;
        if (qs && ttMove != MOVE_NONE && !inCheckQs && !(pos.is_capture(ttMove) || is_promo(ttMove))) ttMove = MOVE_NONE;
    }

    void score_noisy() {
        for (int i = 0; i < noisy.size(); i++) {
            Move m = noisy[i].move;
            int to = move_to(m);
            int pc = pos.board[move_from(m)];
            int victim = is_ep(m) ? PAWN : (pos.board[to] == NO_PIECE ? 6 : piece_type(pos.board[to]));
            int vval = victim == 6 ? 0 : Eval::PieceValue[victim];
            if (is_promo(m)) vval += promo_type(m) == QUEEN ? 900 : -300;
            noisy[i].score = vval * 32 - piece_type(pc) + (victim == 6 ? 0 : S.captHist[pc][to][victim]) / 16;
        }
    }
    void score_quiets() {
        for (int i = 0; i < quiets.size(); i++) {
            Move m = quiets[i].move;
            int from = move_from(m), to = move_to(m);
            int pc = pos.board[from];
            int h = S.history[pos.stm][from][to];
            if ((ss - 1)->contHist) h += (*(ss - 1)->contHist)[pc][to];
            if ((ss - 2)->contHist) h += (*(ss - 2)->contHist)[pc][to];
            quiets[i].score = h;
        }
    }
    static Move pick_best(MoveList& ml, int idx) {
        int best = idx;
        for (int i = idx + 1; i < ml.size(); i++)
            if (ml[i].score > ml[best].score) best = i;
        std::swap(ml.list[idx], ml.list[best]);
        return ml[idx].move;
    }
    bool is_quiet_special(Move m) const { return m == ttMove || m == killer1 || m == killer2 || m == counter; }
    bool killer_ok(Move m) const {
        return m != MOVE_NONE && m != ttMove && !pos.is_capture(m) && !is_promo(m) && pos.is_pseudo_legal(m) && is_legal(pos, m);
    }

    Move next() {
        while (true) {
            switch (stage) {
            case ST_TT:
            case QS_TT:
                stage++;
                lastStage = ST_TT;
                if (ttMove != MOVE_NONE) return ttMove;
                break;
            case ST_GEN_NOISY:
            case QS_GEN:
                gen_moves(pos, noisy, (stage == QS_GEN && inCheckQs) ? GEN_ALL : GEN_NOISY);
                score_noisy();
                idx = 0;
                stage++;
                break;
            case ST_GOOD_NOISY:
                while (idx < noisy.size()) {
                    Move m = pick_best(noisy, idx++);
                    if (m == ttMove) continue;
                    if (see_ge(pos, m, seeThreshold)) { lastStage = ST_GOOD_NOISY; return m; }
                    if (badCount < 64) bad[badCount++] = noisy[idx - 1];
                }
                stage = ST_KILLER1;
                break;
            case ST_KILLER1:
                stage++;
                if (!skipQuiets && killer_ok(killer1)) { lastStage = ST_KILLER1; return killer1; }
                break;
            case ST_KILLER2:
                stage++;
                if (!skipQuiets && killer2 != killer1 && killer_ok(killer2)) { lastStage = ST_KILLER2; return killer2; }
                break;
            case ST_COUNTER:
                stage++;
                if (!skipQuiets && counter != killer1 && counter != killer2 && killer_ok(counter)) { lastStage = ST_COUNTER; return counter; }
                break;
            case ST_GEN_QUIET:
                if (!skipQuiets) {
                    gen_moves(pos, quiets, GEN_QUIET);
                    score_quiets();
                }
                idx = 0;
                stage++;
                break;
            case ST_QUIET:
                if (!skipQuiets) {
                    while (idx < quiets.size()) {
                        Move m = pick_best(quiets, idx++);
                        if (is_quiet_special(m)) continue;
                        lastStage = ST_QUIET;
                        return m;
                    }
                }
                idx = 0;
                stage++;
                break;
            case ST_BAD_NOISY:
                if (idx < badCount) { lastStage = ST_BAD_NOISY; return bad[idx++].move; }
                stage = ST_DONE;
                break;
            case QS_NOISY:
                while (idx < noisy.size()) {
                    Move m = pick_best(noisy, idx++);
                    if (m == ttMove) continue;
                    bool quiet = !(pos.is_capture(m) || is_promo(m));
                    if (!inCheckQs && !see_ge(pos, m, seeThreshold)) continue;

                    lastStage = quiet ? ST_QUIET : ST_GOOD_NOISY;
                    return m;
                }
                stage = QS_DONE;
                break;
            case ST_DONE:
            case QS_DONE:
            default:
                return MOVE_NONE;
            }
        }
    }
};
