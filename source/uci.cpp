#include "search.h"
#include "eval.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <chrono>

std::mutex outMutex;
void datagen(int games, int nodes, U64 seed, const char* outfile);
static Searcher searcher;
static std::thread searchThread;
static bool searching = false;

static void out(const std::string& s) {
    std::lock_guard<std::mutex> lk(outMutex);
    printf("%s\n", s.c_str());
    fflush(stdout);
}

static void wait_search() {
    if (searching) {
        searchThread.join();
        searching = false;
    }
}

static void stop_search() {
    searcher.stopFlag = true;
    wait_search();
}

static U64 perft(const Position& pos, int depth) {
    MoveList ml;
    gen_moves(pos, ml, GEN_ALL);
    if (depth == 1) return ml.size();
    U64 n = 0;
    Position next;
    for (int i = 0; i < ml.size(); i++) {
        pos.make_move(ml[i].move, next);
        n += perft(next, depth - 1);
    }
    return n;
}

static Move parse_move(const Position& pos, const std::string& s) {
    MoveList ml;
    gen_moves(pos, ml, GEN_ALL);
    for (int i = 0; i < ml.size(); i++)
        if (move_str(ml[i].move) == s) return ml[i].move;
    return MOVE_NONE;
}

static Move parse_san(const Position& pos, std::string san) {
    while (!san.empty() && (san.back() == '+' || san.back() == '#' || san.back() == '!' || san.back() == '?')) san.pop_back();
    MoveList ml; gen_moves(pos, ml, GEN_ALL);
    if (san == "O-O" || san == "O-O-O") {
        for (int i = 0; i < ml.size(); i++) if (is_castle(ml[i].move) && ((san == "O-O") == (move_to(ml[i].move) > move_from(ml[i].move)))) return ml[i].move;
        return MOVE_NONE;
    }
    int pt = PAWN; size_t i = 0;
    if (san[0] >= 'A' && san[0] <= 'Z') { pt = std::string("PNBRQK").find(san[0]); i = 1; }
    int promo = -1; size_t eq = san.find('=');
    if (eq != std::string::npos) { promo = std::string("PNBRQK").find(san[eq + 1]); san = san.substr(0, eq); }
    std::string rest = san.substr(i);
    std::string clean; for (char c : rest) if (c != 'x') clean += c;
    if (clean.size() < 2) return MOVE_NONE;
    int to = make_sq(clean[clean.size() - 2] - 'a', clean[clean.size() - 1] - '1');
    std::string dis = clean.substr(0, clean.size() - 2);
    for (int k = 0; k < ml.size(); k++) {
        Move m = ml[k].move;
        if (move_to(m) != to || is_castle(m)) continue;
        if (piece_type(pos.board[move_from(m)]) != pt) continue;
        if (is_promo(m) != (promo >= 0)) continue;
        if (promo >= 0 && promo_type(m) != promo) continue;
        bool ok = true;
        for (char c : dis) { if (c >= 'a' && c <= 'h' && file_of(move_from(m)) != c - 'a') ok = false; if (c >= '1' && c <= '8' && rank_of(move_from(m)) != c - '1') ok = false; }
        if (ok) return m;
    }
    return MOVE_NONE;
}

static void cmd_position(std::istringstream& ss) {
    std::string tok;
    ss >> tok;
    Position pos;
    if (tok == "startpos") {
        pos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        ss >> tok;
    } else if (tok == "fen") {
        std::string fen;
        while (ss >> tok && tok != "moves") fen += tok + " ";
        pos.set_fen(fen);
    } else return;
    searcher.gameKeys.clear();
    if (tok == "moves") {
        while (ss >> tok) {
            Move m = parse_move(pos, tok);
            if (m == MOVE_NONE) break;
            searcher.gameKeys.push_back(pos.key);
            Position next;
            pos.make_move(m, next);
            pos = next;
            if (pos.halfmove == 0) searcher.gameKeys.clear();  // irreversible: earlier keys irrelevant
        }
    }
    searcher.rootPos = pos;
}

static void cmd_go(std::istringstream& ss) {
    Limits lim;
    std::string tok;
    while (ss >> tok) {
        if (tok == "wtime") ss >> lim.wtime;
        else if (tok == "btime") ss >> lim.btime;
        else if (tok == "winc") ss >> lim.winc;
        else if (tok == "binc") ss >> lim.binc;
        else if (tok == "movestogo") ss >> lim.movestogo;
        else if (tok == "movetime") ss >> lim.movetime;
        else if (tok == "depth") ss >> lim.depth;
        else if (tok == "nodes") ss >> lim.nodes;
        else if (tok == "infinite") lim.infinite = true;
    }
    if (lim.wtime == 0 && lim.btime == 0 && lim.movetime == 0 && lim.depth == 0 && lim.nodes == 0) lim.infinite = true;
    searcher.limits = lim;
    searcher.stopFlag = false;
    searchThread = std::thread(&Searcher::start, &searcher);
    searching = true;
}

static void bench(int depth) {
    static const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "2rq1rk1/pp1bppbp/2np1np1/8/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 0 12",
        "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 0 8",
        "8/8/1p1k4/p1p2p2/P1P2P2/1P1K4/8/8 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    };
    U64 total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char* f : fens) {
        searcher.rootPos.set_fen(f);
        searcher.gameKeys.clear();
        searcher.limits = Limits();
        searcher.limits.depth = depth;
        searcher.stopFlag = false;
        searcher.start();
        total += searcher.nodes;
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out("bench nodes " + std::to_string(total) + " time " + std::to_string((int)(s * 1000)) + " nps " + std::to_string((U64)(total / s)));
}

int main(int argc, char** argv) {
    BB::init();
    Zobrist::init();
    Eval::init();
    TT.resize(64);
    searcher.rootPos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc > 5 && std::string(argv[1]) == "datagen") {
        datagen(atoi(argv[2]), atoi(argv[3]), strtoull(argv[4], 0, 10), argv[5]);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "bench") {
        bench(argc > 2 ? atoi(argv[2]) : 12);
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd)) continue;
        if (cmd == "uci") {
            out("id name Fable 5.1 chess 24hrs");
            out("id author Fable 5.1");
            out("option name Hash type spin default 64 min 1 max 4096");
            out("option name MoveOverhead type spin default 40 min 0 max 5000");
            out("option name Threads type spin default 1 min 1 max 1");
            out("uciok");
        } else if (cmd == "isready") {
            out("readyok");
        } else if (cmd == "setoption") {
            wait_search();
            std::string tok, name, value;
            ss >> tok;  // name
            while (ss >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
            while (ss >> tok) value += (value.empty() ? "" : " ") + tok;
            if (name == "Hash") { int mb = std::max(1, std::min(4096, atoi(value.c_str()))); TT.resize(mb); }
            else if (name == "MoveOverhead") MoveOverhead = std::max(0, std::min(5000, atoi(value.c_str())));
        } else if (cmd == "ucinewgame") {
            wait_search();
            TT.clear();
            searcher.clear_history();
        } else if (cmd == "position") {
            wait_search();
            cmd_position(ss);
        } else if (cmd == "go") {
            wait_search();
            cmd_go(ss);
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "ponderhit") {
            // ignored
        } else if (cmd == "quit") {
            stop_search();
            break;
        } else if (cmd == "perft") {
            wait_search();
            int d; ss >> d;
            auto t0 = std::chrono::steady_clock::now();
            U64 n = perft(searcher.rootPos, d);
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            out("nodes " + std::to_string(n) + " time " + std::to_string(s) + " nps " + std::to_string((U64)(n / s)));
        } else if (cmd == "bench") {
            wait_search();
            int d = 12; ss >> d;
            bench(d);
        } else if (cmd == "d") {
            out(searcher.rootPos.fen());
            out("eval " + std::to_string(Eval::evaluate(searcher.rootPos)));
        } else if (cmd == "san") {
            std::string tok, ucis; Position pos = searcher.rootPos;
            while (ss >> tok) {
                if (tok.find('.') != std::string::npos) { size_t d = tok.rfind('.'); tok = tok.substr(d + 1); if (tok.empty()) continue; }
                Move m = parse_san(pos, tok);
                if (m == MOVE_NONE) { out("bad san " + tok); break; }
                ucis += " " + move_str(m); Position n; pos.make_move(m, n); pos = n;
            }
            out("moves" + ucis);
            out(pos.fen());
        } else if (cmd == "eval") {
            out("eval " + std::to_string(Eval::evaluate(searcher.rootPos)));
        }
    }
    stop_search();
    return 0;
}
