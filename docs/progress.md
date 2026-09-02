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
### 18:10 — tuning success
- engine7b vs stash-21: 35.5% (100 games, 10+0.1) → est ~2600.
- Subset Texel tuning (241 non-PSQT knobs, lambda 0.5 result/score blend, 1.2M positions): engine8 vs engine7b = 64% (+100 Elo, 150 games at 5+0.05). Adopted (params.h). engine10 = engine9 (UCI params) + tuned eval; verifying vs stash-21 at 10+0.1.
- Datagen round 2 launched: engine10, 10 procs × 1500 games × 10000 nodes, BelowNormal priority.
- A/B batch of search params cancelled (was on untuned eval); will redo on engine10.
### 18:40 — search/time wins
- engine10 vs stash-25: 33% (100 games, 10+0.1) → est ~2780 (with stash-21 +45).
- New eval terms (bishop pawns, rook behind passer, pawn storm, pawn-count imbalance, space, blocked passer): +16 ±48 vs engine10 (150 games) — kept, to be tuned.
- Node-based time management: +72 ±44 (200 games, 5+0.05). Singular double extension: +31 ±40 (200 games). Both kept.
- engine13f (all of the above, options hidden) being verified vs stash-25 at 10+0.1; datagen round 2 ~90% done; tune round 2 queued automatically.
## Hour 4 — 2026-09-01 19:05 (elapsed 2:48)
- final/ = engine13f (commit 56bedab): 42.5% vs stash-25 (100 games, 10+0.1) → est. ~2880 Elo.
- Tuning round 2 (subset, fresh 1.4M positions at 10k nodes): neutral (49%) — not adopted. Full-PSQT tuning with lambda 0.5 running in background (997 knobs, ~1 h).
- A/B on engine13 at 5+0.05 (150 games each): AspDelta 40: -37 (reject). LmrDiv 200: +44 (candidate). Remaining: LmrDiv 300, RfpMargin 100, NmpBase 4, FutMargin 160, LmpBase 5, HistDiv 4000.
- Next hour: finish A/B batch, adopt winners, verify combined build vs stash-25/30; PGO build test.
## Hour 5 — 2026-09-01 20:05 (elapsed 3:48)
- final/ still engine13f (~2880). engine16 (LmrDiv 200, FutMargin 160) = +30 vs engine13 (200 games, 5+0.05); to be verified vs stash-25 at 10+0.1 before install.
- Full-PSQT Texel tuning (lambda 0.5): -44 → rejected. Eval tuning exhausted for now; params from round-1 subset tuning remain.
- A/B batch 2 (engine16, 150 games each): RfpMargin 60 +44 (candidate), LmrDiv 170 +12 (neutral). Running: FutMargin 220, LmrCaptPct 30, QsDelta 120, SingMargin 3, SeeQuiet 60, LmrBase 110.
- gprof unusable under MinGW static; skipped profiling.
- Next: finish batch, build engine19 with winners, verify at 10+0.1 vs stash-25 (200 games), install; then time-management A/B (TmDiv/TmIncPct) at 10+0.1.
## Hour 6 — 2026-09-01 21:05 (elapsed 4:48)
- A/B batch 2 winners: RfpMargin 60 (+44), FutMargin 220 (+49), QsDelta 120 (+68), SingMargin 3 (+58); neutral: LmrDiv 170, SeeQuiet 60, LmrBase 110; rejected: LmrCaptPct 30 (-44).
- engine19 (all winners) vs engine16: +92 (200 games, 5+0.05).
- Gauntlet at 10+0.1 (100 games each, same openings): engine13f: 54% vs stash-21, 31% vs stash-25, 13% vs stash-30. engine19f: 69.5% vs stash-21 (in progress vs 25/30). Earlier 42.5% of engine13f vs stash-25 was noise.
- final/ = engine19f (commit 0e871b6). Estimated Elo ~2850-2900 (stash-21 +143, stash-25 -96).
- Next: A/B batch 3 (FutMargin 300, RfpMargin 45, QsDelta 60, SingMargin 4, LmrCaptPct 70, NmpEvalDiv 150, LmpBase 2, FutDepth 10, RfpDepth 10, HistBonusQ 24), then time-management A/B at 10+0.1.
## Hour 7 — 2026-09-01 21:50 (elapsed 5:33)
- final/ = engine19f (~2850). Gauntlet 10+0.1: engine19f 69.5% vs stash-21, 31% vs stash-25, 11.5% vs stash-30.
- NNUE built: trainer (nnue_train.cpp, 768->128x2->1, CReLU, Adam, 4 s/epoch on 5.4M positions), AVX2 int16 inference with incremental accumulators (verified exact vs from-scratch and vs trainer float output). Net nn2 (100 epochs, val MSE 0.0261 < HCE 0.0272 on same target) still loses 18% vs HCE at 5+0.05 — evaluation quality (king safety) too weak with 5M low-depth positions. HL=256 net training; datagen round 3 (engine19, 10k nodes) continues at low priority to grow the dataset.
- A/B batch 3 on engine19 (150 games each): FutMargin 300 +21, RfpMargin 45 +12, QsDelta 60 ?, SingMargin 4 +7, LmrCaptPct 70 -2 → none adopted yet (noise level). Remaining: LmpBase 2, FutDepth 10, RfpDepth 10, HistBonusQ 24.
- Decision: HCE remains the main line; NNUE only ships if it beats HCE head-to-head and vs Stash.
