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
## Hour 8 — 2026-09-01 22:05 (elapsed 5:48)
- final/ unchanged (engine19f, ~2850). A/B batch 3 (engine19): all neutral or negative (RfpDepth 10: -73). Nothing adopted.
- NNUE: HL=256 net (val 0.0253) = -160 vs HCE (100 games); HL=128 = -260. Data (5.4M positions) insufficient; datagen round 3 continues at low priority (~50k positions/10 min). Training nn4 (HL=256, lambda 0.2) at low priority.
- Time management A/B at 10+0.1: TmDiv 18 (more time/move) = -92 → current allocation (t/24 + 0.75 inc) is not too stingy. TmDiv 32, TmIncPct 50/100 running; batch 4 (IIR depth, NMP min depth, LMR pv/improving/cutnode terms, check ext, probcut/razor depth) queued.
- Plan: continue HCE A/B (main line); retrain NNUE when data reaches ~10M; PGO build and final verification reserved for the last 2 hours.
### 22:40 — time management bug found (big)
- At 10+0.1 self-play: TmDiv 32 = +130, TmDiv 45 = +264, TmDiv 60 = +366 (89%) vs the old t/24. Cause: hard limit t/3 × stacked extension factors (score-drop 1.6 × node-fraction 1.35) let the engine spend 1-2 s on a few moves, then it played 211 moves at depth 1 in time trouble (new side: 35). No time forfeits either way.
- final/ = engine23f with TmDiv 60 (commit 937add3). Batch running vs this base: TmHardPct 12, TmDiv 45/32 + TmHardPct 12, TmDiv 80, TmHardMul 2.
## Hour 9 — 2026-09-01 23:10 (elapsed 6:53)
- Time-management ladder at 10+0.1 (120 games each, self-play): TmDiv 32 +130, 45 +264, 60 +366 (vs 24); 80 +64 vs 60; 100 +89 vs 80. Hard-limit variants neutral (TmHardMul 2: -12; TmHardPct 12: -29). TmDiv 32 + HardPct 12 = -250 vs 60 (181 depth-1 moves: time trouble). Cause is the stacked soft-limit factors (score-drop ×1.6, node-fraction ×1.35) plus finishing the running iteration.
- final/ = engine25f: TmDiv 100 (t/100 + 0.75·inc ≈ 175 ms/move at start). Added TmMaxFactor option (engine25) to test capping the factors so the base allocation can grow again.
- NNUE: nn4 (HL 256, lambda 0.2) training slowly under load (epoch ~60/100). Datagen round 3 finishing (2.9k/3k games per proc); nn5 (all data, ~8M) queued.
- Next: TmDiv 120, NodeTm interaction, TmMaxFactor 120/100 tests; search-knob batch; re-gauntlet vs Stash with the new TM.
## Hour 10 — 2026-09-01 23:55 (elapsed 7:38)
- TM ladder continued: TmDiv 100 vs 80 +89; 120 vs 80 +76; 140 vs 100 +89. Factor caps: TmMaxFactor 120 +26, 100 -26; TmDiv 60+cap -92; TmHardMul 2 +12. NodeTm=0 -111 (keep). Fixed movetime 0.2s beats 0.1s by ~50 → no deep-search bug; the allocation logic (iteration overshoot × stacked factors) is what wastes time. Added TmStopPct (don't start an iteration late) — testing 60/40.
- "depth 1" moves were forced single-move positions, not time trouble (earlier diagnosis corrected).
- final/ = engine25f (TmDiv 100).
- NNUE: with a fair opponent (same source/TM) nn4 (HL256, lambda 0.2) = -95, nn5 (lambda 0.5, 8.2M pos) = -230. Score-weighted targets matter: nn6 (lambda 0.0, 8.2M) training. Datagen round 4 running (low priority).
- Search-knob batch (engine25, 5+0.05) running.
## Hour 11 — 2026-09-02 00:20 (elapsed 8:03)
- final/ = engine27f (TmDiv 100 + TmStopPct 60). Gauntlet 10+0.1 (100 games each, same openings as before): 45% vs stash-25 (was 31%), 20% vs stash-30 (was 11.5%) → est. ~2910 Elo.
- TmStopPct 40 +58, 60 +101 (adopted); TmDiv 60+Stop 50 +58. Search-knob batch (10 tests): nothing adopted (LmrImproving=0 -85, LmrPv=0 -28, IirDepth 6 -37, rest neutral).
- NNUE nn6 (HL256, lambda 0.0, 8.2M positions) trained (val 0.00159 on score-only target); fair match vs HCE running. nn4 (lambda 0.2) was -95, nn5 (0.5) -230 → lambda toward search scores helps.
- Next: TM refinements (TmDiv 70/140, StopPct 75 with base Stop 60), NNUE decision, PGO build test when CPU frees.
## Hour 12 — 2026-09-02 00:45 (elapsed 8:28)
- final/ = engine27f (~2910). TM plateau: TmDiv 140 = 100 (both Stop 60); Stop 75 neutral; TmDiv 70 -111.
- Diagnostics of "less time = better": fixed depth 14 beats depth 10 by +117 (no depth pathology). Fixed movetime 0.3 s vs 0.15 s gave -297 (150 games, engine27, heavy load) but +61/+114/+168 with engine30/31 (NoDrawTT / CorrHist off / Hash 512 variants, 60-80 games). Inconclusive; a clean rerun is scheduled when the machine is quiet.
- Added options NoDrawTT (skip TT store of repetition draw scores; default on in engine30+) and CorrHist (on/off). Neither adopted into final/ yet.
- NNUE: nn6 (HL256, lambda 0) = 50% vs HCE and 45.5% vs stash-25 (HCE: 45%) — parity. nn7 (HL384, 9.4M positions) training (epoch 22/120).
- 300-game TmDiv 50 vs 100 (10+0.1) running.
## Hour 13 — 2026-09-02 01:15 (elapsed 8:58)
- Root cause hunt for "more time = worse": fixed depth 17 lost 0-60 to depth 13 (!); 300-game TmDiv 50 vs 100 = -160; clean 0.3s vs 0.15s = -104 (engine31), -53 (engine27). Logged games show the deeper side stuck at ±0.02 scores while losing: it treats a return to the ROOT position (4-ply shuffle) as an in-tree 2-fold draw, giving a false draw floor that the opponent's refutations (LMR-reduced at high depth) fail to break.
- Fix (engine33): root position counts as game history (needs a second repetition), only strictly-in-tree repetitions are 2-fold draws (Stockfish rule). Testing depth 17 vs 13 and 0.3s vs 0.15s now. If confirmed, the whole TM ladder (which was compensating for this) must be re-tuned.
- Also added options NoDrawTT, CorrHist, RepTwofold for diagnostics.
- NNUE: nn6 = parity with HCE; nn7 (HL384) training (~epoch 70/120).
### 01:45 — ROOT CAUSE: 16-bit TT keys
- With the TT cleared every move, depth 17 beat depth 13 by 84%; with the persisted TT it lost 0-60. Strict threefold and repetition-aware TT stores changed nothing. Widening TT keys to 32 bits (2 entries per 32-byte bucket) fixed it completely: depth 17 vs 13 = 27-0-13 (84%). The 16-bit key false hits (3 per 65536 probes, growing as the table fills within a game) poisoned every long search; all the "less time is better" time-management results were compensating for this.
- Consequence: time management must be re-tuned (TmDiv 30/50/70 vs 100); NNUE (nn7 HL384 = +47 vs HCE) to be rebuilt on the fixed base.
## Hour 14 — 2026-09-02 01:50 (elapsed 9:33)
- TT key bug fixed (32-bit keys, 2-entry buckets, engine37). With it: depth 17 vs 13 = 84%; TmDiv 30 vs 100 = +168 at 10+0.1 (time is valuable again). Re-tuning TM: TmDiv 50/70 vs 100 running, then a ladder around 30 (20, 40, StopPct 80/100, MaxFactor 150).
- NNUE nn7 (HL384) vs fixed HCE: +16 (150 games) — parity. HCE remains the main line; NNUE stays as an option.
- final/ still engine27f (16-bit TT); engine37f (fixed TT, TmDiv 100) being verified vs stash-25 before install; then install the re-tuned TM.
- Remaining plan (14.4 h left): finish TM re-tune (~1 h), re-run A/B batches of search margins on the fixed base (they were tuned under the TT bug), NNUE retrain with dg4 data, PGO build, final verification 2 h before the deadline.
