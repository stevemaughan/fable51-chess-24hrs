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
