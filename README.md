# Fable 5.1 chess 24hrs

A UCI chess engine written from scratch in 24 hours of wall-clock time by a single AI model
(Claude **Fable 5.1**), as an entry in a benchmark series that measures nothing but the
playing strength of the executable left in `final/` at the end of the 24 hours.

## What this is

The benchmark rules (see [CLAUDE.md](CLAUDE.md) for the full text):

- One model, fully autonomous, no human input during the run. 24 hours from the moment
  `docs/start_time.txt` is written (2026-09-01 16:16:56 -04:00) to the deadline.
- Everything written from scratch during the run, in C, C++ or Zig, standard library only.
  Reading documentation (chessprogramming.org, papers) is allowed; copying engine source,
  network files, books or tablebases is not.
- The deliverable is `final/Fable51chess24hrs.exe`, rated afterwards with fastchess at
  **10 s + 0.1 s** against a pool of engines (including the Stash versions supplied in
  `resources/engines/`) with `Hash` set to 256 MB, single-threaded, on a modern Windows 11
  laptop (x86-64-v3: POPCNT, BMI2, AVX2; no AVX-512 assumed).
- Hourly progress had to be logged in `docs/progress.md`; git history records every state of
  `final/`.

## The engine

- `id name Fable 5.1 chess 24hrs`, `id author Fable 5.1`, executable `Fable51chess24hrs.exe`.
- **Language: C++17**, compiled with GCC 15.2.0 (MinGW-w64, via scoop). C++ was chosen for
  the intrinsics (`_pext_u64`, AVX2 via `immintrin.h`), fast iteration with a familiar
  toolchain, and static linking with `-static`.
- The final binary is fully standalone (imports only `KERNEL32.dll` and `msvcrt.dll`).

### Building

All sources are in `source/`. The final executable was built with:

```
cd source
./build.sh build/final.exe -DUSE_NNUE -DNO_TUNE_OPTIONS
```

which expands to

```
g++ -O3 -flto -static -march=x86-64-v3 -mtune=znver4 -std=c++17 -DNDEBUG \
    -fno-exceptions -fno-rtti -DUSE_NNUE -DNO_TUNE_OPTIONS \
    -o final.exe uci.cpp search.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp datagen.cpp
```

- Without `-DUSE_NNUE` the engine uses its hand-crafted evaluation (HCE) only.
- Without `-DNO_TUNE_OPTIONS` the engine additionally exposes ~40 search/time-management
  parameters as UCI spin options (used for A/B testing with fastchess `option.X=Y`).
- `build_final.sh` is a PGO variant of the same build; PGO measured ~10% *slower* with this
  toolchain, so the plain LTO build was shipped.
- Other tools: `perft.cpp` (perft suite runner), `tune.cpp` (Texel tuner for the HCE
  parameters in `params.h`), `nnue_train.cpp` (NNUE trainer, `-DHL_SIZE=384`),
  `engine.exe datagen <games> <nodes> <seed> <outfile>` (self-play data generation).

### Running

Standard UCI. Supported: `uci`, `isready`, `ucinewgame`, `setoption`, `position startpos|fen
... [moves ...]`, `go wtime/btime/winc/binc/movestogo`, `go movetime`, `go depth`, `go nodes`,
`go infinite`, `stop`, `quit`; `ponder`/`ponderhit` are ignored. Extra commands: `bench [depth]`,
`perft <depth>`, `eval`, `d`, `san <moves>`, `nnuecheck`.

UCI options: `Hash` (MB, default 64; the rating harness sets 256), `MoveOverhead` (ms, default
50), `Threads` (fixed at 1), `UseNNUE` (default true; false switches to the hand-crafted
evaluation).

## Architecture and features

**Board and move generation**

- Bitboards, PEXT (BMI2) sliding-piece attack tables, copy-make (`Position` is copied on each
  move; ~800 bytes including the NNUE accumulator).
- Fully legal move generation (pins and check evasions handled at generation time, en passant
  legality tested explicitly), 16-bit moves. Passes the whole `resources/perft/perft.epd`
  suite (753 tests through depth 6, 0 failures) — first pass at 16:24, eight minutes in.
- Zobrist hashing with a separate pawn key; en-passant square only hashed when a capture is
  actually possible.

**Search**

- Iterative deepening, aspiration windows, principal variation search (negamax).
- Transposition table: 32-byte buckets of two 16-byte entries, **32-bit key check**,
  depth/age replacement, prefetch. (Started as 16-bit keys in 3-entry buckets — see below.)
- Quiescence search with SEE-pruned captures, promotions, all evasions when in check, delta
  pruning, TT probing/storing.
- Pruning/reductions: reverse futility pruning, razoring, null-move pruning (adaptive R,
  requires non-pawn material), ProbCut, internal iterative reduction, late move pruning,
  futility pruning, history pruning, SEE pruning of quiets and captures, log-formula LMR
  with history/PV/cut-node/improving/check adjustments.
- Extensions: singular extensions (with double extension and multi-cut), check extensions
  bounded to twice the root depth.
- Move ordering: TT move, MVV-LVA + capture history for captures (SEE-classified good/bad),
  two killers, counter-move, butterfly history + two-ply continuation history for quiets.
- Correction history (static-eval correction indexed by pawn hash).
- Repetition: two-fold repetition strictly inside the search tree is a draw; the root and game
  history need a true three-fold. 50-move rule. Draw score ±1 (node-parity noise).
- Threaded stdin reader is not needed: the search runs in a `std::thread` and the main thread
  keeps reading stdin, so `stop`, `isready` and `quit` work during a search.

**Evaluation**

- Final: an NNUE-style network, 768 → 384×2 (side-to-move / opponent perspectives, CReLU)
  → 1, int16 quantised, AVX2 inference with incrementally updated accumulators in the
  position. Weights are compiled into the executable (`nnue_weights.h`).
- Training data: ~17 M positions from the engine's own self-play (datagen: 8-10 random plies,
  then fixed 10 000-node searches, quiet positions only, labelled with the search score;
  seven rounds, the last two generated by the NNUE engine itself). Target: sigmoid of the
  search score only (`lambda = 0`); result-blended targets trained much worse. Trainer:
  hand-written C++ Adam, batch 16 384, 120 epochs, ~15 s/epoch on 10 threads.
- Fallback (`UseNNUE=false`): hand-crafted tapered evaluation — PeSTO piece-square tables,
  material, pawn structure (isolated/doubled/backward/connected/phalanx, pawn hash), passed
  pawns (rank, king distance, free path, blocker, rook behind), mobility, outposts, bishop
  pair, bad-bishop pawns, rook on open files/7th, king safety (pawn shield, pawn storm,
  attack-unit king danger with safe checks), threats, space, tempo, and endgame scaling.
  Its non-PSQT parameters were Texel-tuned on self-play data.

**Time management**

- Per move: `t/20 + 0.75·increment` as the soft target (t = remaining time minus a 50 ms
  overhead); a new iteration is started only while elapsed time is below the soft target
  scaled by best-move stability (0.6-1.0), a score-drop factor (up to 1.6) and the
  best-move node fraction (1.55 − fraction, clamped 0.55-1.35). Hard limit
  `min(t/3, 4·soft)`; time is checked every 1 024 nodes.
- If only the opponent's clock is sent, it is used as an estimate rather than searching
  forever.

**Deliberately left out**: pondering, multi-PV, Chess960, tablebases, opening book,
multi-threading, staged move generation (a staged move picker was written and tested twice; it
was neutral, so the simpler all-moves-generated ordering was kept).

## How the time was spent

| Elapsed | What happened |
|---|---|
| 0:00-0:15 | Bitboards, PEXT attacks, position, legal move generator, perft driver. Full perft suite passes at 0:08. |
| 0:15-0:35 | PVS search with TT/QS/NMP/LMR/pruning, PeSTO eval, UCI. First build in `final/` at 0:15 (compliance passed). |
| 0:35-1:00 | Full hand-crafted eval, king-danger bug found (formula off by 10×) and fixed; self-play data generator and Texel tuner written. |
| 1:00-2:00 | 44% vs stash-20. Staged move picker regresses 160 Elo under a clock but not at fixed depth (the cause was only found 11 hours later); reverted. ProbCut, correction history, repetition fix. |
| 2:00-3:00 | Subset Texel tuning: +100 Elo. Node-count based time management +72, singular double extension +31. ~2880 vs stash-25 (later shown to be a noisy sample). |
| 3:00-5:30 | UCI-settable search parameters and A/B batches (LmrDiv, FutMargin, RfpMargin, QsDelta, SingMargin winners). NNUE trainer + AVX2 inference written; first nets lose 160-260 Elo to the HCE. |
| 5:30-8:30 | Time-management ladder: spending *less* time per move kept winning (+366 at t/60 vs t/24). Investigated as time trouble, then as repetition scoring; neither. |
| 8:30-9:50 | Root cause: **16-bit TT keys**. False hits poisoned every long search (depth 17 lost 0-60 to depth 13 with a persistent TT; 84% wins after widening the key to 32 bits). All the "less time is better" results were compensations. Re-tuned time management (t/20). ~3200 vs Stash. |
| 9:50-14:00 | NNUE trained with score-only targets reaches parity, then +40 at 10+0.1; adopted. Revert-tests of margins on the fixed base (all neutral). Compiler-flag and PGO checks. |
| 14:00-21:15 | Self-play iterations: nets nn13 (+31) and nn14 (+34 over 400 games) trained on data from the previous NNUE engine; paired checks against Stash. Final build frozen at 21:13 elapsed. |
| 21:15-24:00 | Calibration only: 1 400 games of the final binary against stash-30/33/37, zero forfeits. |

Measured versus accepted on judgement: every adopted change from hour 2 on was measured
(150-400 games at 5+0.05 or 10+0.1); the initial feature set (search framework, HCE terms) was
accepted on judgement. The cost of the TT-key bug was the biggest lesson: about eight hours of
tuning around a symptom before finding the cause with a fixed-depth versus persisted-TT
experiment.

## Elo by hour

Estimates as recorded in `docs/progress.md` at the time (10+0.1 matches against Stash,
CCRL-blitz-anchored); later hours are more reliable than earlier ones.

| Elapsed hour | Time (local) | Estimated Elo |
|---|---|---|
| 0 | 16:31 | not yet measurable |
| 1 | 17:15 | ~2200 (15.8% vs stash-20) |
| 2 | 18:10 | ~2470 → 2570 (44%, then 59.5% vs stash-20) |
| 3 | 19:05 | ~2800 (42.5% vs stash-25, small sample) |
| 4 | 20:05 | ~2850 |
| 5 | 21:05 | ~2850 (69.5% vs stash-21, 31% vs stash-25) |
| 6 | 22:05 | ~2850 |
| 7 | 23:10 | ~2900 (time-management changes) |
| 8 | 00:20 | ~2910 (45% vs stash-25, 20% vs stash-30) |
| 9 | 01:15 | ~2910 |
| 10 | 02:10 | ~3000 → 3200 (TT fix: 60%, then 81% vs stash-25; 58% vs stash-30) |
| 11 | 03:05 | ~3200 |
| 12 | 03:55 | ~3220 (59% vs stash-30, 40.5% vs stash-33) |
| 13 | 04:50 | ~3220 |
| 14 | 06:10 | ~3230 (NNUE adopted) |
| 15 | 07:00 | ~3230 |
| 16 | 08:30 | ~3260 (48.5% vs stash-33) |
| 17 | 09:15 | ~3250 (25.5% vs stash-37) |
| 18 | 10:15 | ~3250 |
| 19 | 11:15 | ~3270 (nn13) |
| 20 | 12:40 | ~3270 |
| 21 | 13:30 | ~3250 (nn14, final frozen) |
| 22 | 14:25 | ~3250 (1 400-game calibration) |
| 23 | 15:30 | ~3250 |

## All assumptions made

- The rating harness sends both clocks (`wtime`/`btime`) with `winc`/`binc`; `movestogo`
  is handled but not expected. If only one clock arrives the engine uses it for both sides.
- fastchess `timemargin` may be zero: the engine keeps a 50 ms move overhead, never plans
  more than a third of the remaining time on one move, and was checked for forfeits in
  ~1 400 games at 10+0.1 with 8-10 concurrent games on this machine (none).
- The target laptop is x86-64-v3 with fast PEXT; the build uses `-march=x86-64-v3
  -mtune=znver4` and AVX2 intrinsics, no AVX-512.
- `Hash 256` is set by the harness; the engine allocates and clears it on `setoption`.
  Default 64 MB otherwise.
- Elo estimates use the CCRL Blitz numbers in `resources/engines/StashStrength.md` as
  anchors; the true pool may rate differently.
- "From scratch" was interpreted as: all code, network weights, tuned parameters and data
  produced during the run; published tables (PeSTO) and published ideas were used.
- Self-play data generation and training used the machine's 10 allowed cores; no external
  data.
- The 24-hour clock is the timestamp in `docs/start_time.txt`; `docs/progress.md` hour
  numbers are the running log's own counter (it ran ahead of real elapsed hours), the table
  above uses real elapsed hours.

## Estimated strength

Final binary (`final/Fable51chess24hrs.exe`, git a07e6a0 and later, md5 `954834cd01dc…`),
10 s + 0.1 s, `Hash 256`, UHO openings, both colours per opening:

| Opponent (CCRL Blitz) | Games | Score | Elo diff |
|---|---|---|---|
| stash-30.0 (3166) | 200 | 69.0% | +139 |
| stash-33.0 (3286) | 800 | 45.6% | −31 |
| stash-37.0 (3424) | 200 | 23.5% | −205 |

Combined estimate: **about 3250 Elo** on the CCRL blitz scale, with an uncertainty of roughly
±30 from sample size plus whatever systematic offset the rating pool and hardware introduce.
No losses on time, crashes or illegal moves were observed in any test game (several thousand
games over the run, 1 400 with the final binary).

## Official results

*(to be filled in after the rating match)*
