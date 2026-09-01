# Progress log — Fable 5.1 chess 24hrs

Start: 2026-09-01T16:16:56-04:00. Deadline: 2026-09-02T16:16:56-04:00.

## Plan (hour 0)
- Language: C++ (GCC 15.2, -O3 -flto -static -march=x86-64-v3).
- Board: bitboards, PEXT sliding attacks, copy-make, 16-bit moves.
- Eval: PeSTO tapered PSQT (published tables on CPW) to start; add terms later, possibly tune.
- Search: iterative deepening, PVS, TT, QS, killers/history, null move, LMR, futility, aspiration.
- Time: soft/hard limits, node-count polling, stdin reader thread for stop/isready.
- Budget: perft-correct by ~H3, first playable in final/ by ~H5, then strength work; last 1.5h reserved for final build/verification.
- Assumptions: fastchess timemargin unknown -> keep a conservative move overhead. Hash 256 MB set by harness.

## Hour 0 — 2026-09-01 16:31 (elapsed 0:15)
- Wrote bitboard/PEXT movegen, copy-make position, PeSTO eval, PVS search (TT, QS, killers/history/counter/contHist, NMP, LMR, RFP, razoring, LMP, futility, SEE pruning, singular ext), UCI with search thread.
- **First full perft suite pass: 16:24:59** (753 tests to depth 6 capped at 400M nodes, 0 fails).
- fastchess compliance: passed. First build placed in final/ at 16:31.
- Elo: not yet measurable (first match vs stash-20 running).
- Next: verify match stability (no timeouts/crashes), then measure vs stash-21/25, then eval improvements.
## Hour 1 — 2026-09-01 17:00 (elapsed 0:45)
- v1 (PeSTO only) vs stash-20 at 10+0.1: 15.8% (60 games) → ~2200. No time losses/crashes.
- Wrote full HCE (pawn hash, mobility, king safety, threats, passed pawns). First version scored 20% (100 games): found king-danger formula bug (danger²/48 → thousands of cp). Fixed (danger²/400): running ~47% vs stash-20 mid-match.
- Wrote self-play datagen (engine.exe datagen) and Texel coordinate-descent tuner (source/tune.cpp) over params.h; both smoke-tested.
- Search reviewed for bugs; none found. Check extension bounded to ply < 2*rootDepth.
- Next: install eval3 in final/, commit; run datagen (10 procs, ~20 min), tune eval; then search tuning.
## Hour 2 — 2026-09-01 17:15 (elapsed 1:00)
- eval3 (king-danger fix) vs stash-20: 44% over 100 games at 10+0.1 → est. ~2470 Elo. Installed in final/ (commit "v3").
- Datagen done: 20k self-play games at 6000 nodes/move → 1.97M labelled positions. Tuner running (1.2M positions, 6 threads, coordinate descent).
- Added staged move picker, probcut, correction history, score-drop time extension. Combined build lost 25% vs eval3 (40 games) → bisecting with 4 variant builds (80 games each vs eval3).
- Next hour: identify the regression, adopt tuned eval if it wins a match, then re-measure vs stash-20/21.
### Interim note — 17:50
- Staged move picker (movepick.h) build lost ~160 Elo at 5+0.05 vs v3 but was equal at fixed depth 8/12 and only -57 at fixed movetime. Spent ~45 min; could not find the cause (validated pseudo-legality and generator equivalence exhaustively). Reverted to v3 move ordering; movepick.h kept on disk but unused.
- engine7 = v3 ordering + probcut + correction history + threefold-repetition fix (2-fold of pre-root positions no longer scored as draw) + score-drop time extension: 49.5% vs v3 (100 games, 5+0.05).
- Texel tuning of all 977 params on 1.2M low-node self-play positions: tuned eval = v3 (49.5%). No gain; data too noisy. Trying non-PSQT subset with lambda 0.5.
## Hour 3 — 2026-09-01 18:10 (elapsed 1:53)
- engine7b installed in final/ (commit 19e0874): 59.5% vs stash-20 (100 games, 10+0.1) → est. ~2560-2580 Elo. Compliance passed. stash-21 match running.
- Tuning: full-param Texel = no gain; subset (241 non-PSQT knobs, lambda 0.5) being tested (engine8 vs engine7b).
- Added 24 UCI-settable search parameters (sparams.h) + abtest.sh for A/B tests without rebuilds; bench-identical to engine7b.
- Plan next hour: A/B the main search margins (LMR divisor, RFP, NMP, LMP, futility, aspiration delta) at 5+0.05 with 150-300 games each; keep only clear winners. Datagen round 2 with the stronger engine when cores are free.
