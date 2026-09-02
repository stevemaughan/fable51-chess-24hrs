// NNUE trainer: 768 -> HL x2 (perspective) -> 1, CReLU, Adam, MSE on sigmoid(eval) vs blended target.
// Usage: nnue_train.exe <out_header> <epochs> <threads> <lambda> <lr> <datafile>...
#include "position.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <random>
#include <algorithm>
#include <chrono>

#ifndef HL_SIZE
#define HL_SIZE 128
#endif
static const int HL = HL_SIZE;
static const int NIN = 768;
static const int QA = 127, QB = 64;
static double K_SIG = 0.31 * std::log(10.0) / 400.0;   // matches Texel K used for the HCE

struct Sample {
    uint16_t f[32];   // white-perspective feature indices
    uint8_t n;
    uint8_t stm;
    float target;
};
static std::vector<Sample> data;
int PSQ_MG[12][64];
int PSQ_EG[12][64];
const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};

static inline int flip_idx(int idx) {
    int c = idx / 384, rest = idx % 384, pt = rest / 64, s = rest % 64;
    return ((c ^ 1) * 6 + pt) * 64 + (s ^ 56);
}

// network (float)
static std::vector<float> W1(NIN * HL), NB1(HL), W2(2 * HL);
static float NB2 = 0;
// Adam state
static std::vector<float> mW1(NIN * HL), vW1(NIN * HL), mB1(HL), vB1(HL), mW2(2 * HL), vW2(2 * HL);
static float mB2 = 0, vB2 = 0;

struct Grad {
    std::vector<float> W1, NB1, W2;
    float NB2;
    double loss;
    Grad() : W1(NIN * HL, 0.f), NB1(HL, 0.f), W2(2 * HL, 0.f), NB2(0), loss(0) {}
    void zero() { std::fill(W1.begin(), W1.end(), 0.f); std::fill(NB1.begin(), NB1.end(), 0.f); std::fill(W2.begin(), W2.end(), 0.f); NB2 = 0; loss = 0; }
};

static void forward_backward(const Sample& s, Grad* g, double* lossOut) {
    float accW[HL], accB[HL];
    for (int i = 0; i < HL; i++) { accW[i] = NB1[i]; accB[i] = NB1[i]; }
    int fb[32];
    for (int k = 0; k < s.n; k++) {
        const float* rw = &W1[s.f[k] * HL];
        fb[k] = flip_idx(s.f[k]);
        const float* rb = &W1[fb[k] * HL];
        for (int i = 0; i < HL; i++) { accW[i] += rw[i]; accB[i] += rb[i]; }
    }
    float* accUs = s.stm == WHITE ? accW : accB;
    float* accThem = s.stm == WHITE ? accB : accW;
    float hUs[HL], hThem[HL];
    float out = NB2;
    for (int i = 0; i < HL; i++) {
        hUs[i] = std::min(1.f, std::max(0.f, accUs[i]));
        hThem[i] = std::min(1.f, std::max(0.f, accThem[i]));
        out += W2[i] * hUs[i] + W2[HL + i] * hThem[i];
    }
    double pred = 1.0 / (1.0 + std::exp(-K_SIG * out));
    double diff = pred - s.target;
    *lossOut += diff * diff;
    if (!g) return;
    float dout = (float)(2.0 * diff * pred * (1.0 - pred) * K_SIG);
    g->NB2 += dout;
    float dUs[HL], dThem[HL];
    for (int i = 0; i < HL; i++) {
        g->W2[i] += dout * hUs[i];
        g->W2[HL + i] += dout * hThem[i];
        dUs[i] = (accUs[i] > 0.f && accUs[i] < 1.f) ? dout * W2[i] : 0.f;
        dThem[i] = (accThem[i] > 0.f && accThem[i] < 1.f) ? dout * W2[HL + i] : 0.f;
        g->NB1[i] += dUs[i] + dThem[i];
    }
    float* dW = s.stm == WHITE ? dUs : dThem;   // gradient for white-perspective accumulator
    float* dB = s.stm == WHITE ? dThem : dUs;
    for (int k = 0; k < s.n; k++) {
        float* gw = &g->W1[s.f[k] * HL];
        float* gb = &g->W1[fb[k] * HL];
        for (int i = 0; i < HL; i++) { gw[i] += dW[i]; gb[i] += dB[i]; }
    }
}

static void adam(std::vector<float>& w, std::vector<float>& m, std::vector<float>& v, const std::vector<float>& g, float lr, float b1t, float b2t, float scale) {
    const float beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;
    for (size_t i = 0; i < w.size(); i++) {
        float gi = g[i] * scale;
        m[i] = beta1 * m[i] + (1 - beta1) * gi;
        v[i] = beta2 * v[i] + (1 - beta2) * gi * gi;
        float mh = m[i] / (1 - b1t), vh = v[i] / (1 - b2t);
        w[i] -= lr * mh / (std::sqrt(vh) + eps);
    }
}

static void write_header(const char* file) {
    FILE* f = fopen(file, "w");
    fprintf(f, "// Generated NNUE weights: 768->%dx2->1, QA=%d QB=%d\n#pragma once\n#include <cstdint>\n", HL, QA, QB);
    fprintf(f, "#define NN_HL %d\n#define NN_QA %d\n#define NN_QB %d\n", HL, QA, QB);
    fprintf(f, "alignas(64) static const int16_t NN_W1[%d][%d] = {\n", NIN, HL);
    for (int i = 0; i < NIN; i++) {
        fprintf(f, "{");
        for (int j = 0; j < HL; j++) fprintf(f, "%d,", (int)std::lround(std::max(-32000.f, std::min(32000.f, W1[i * HL + j] * QA))));
        fprintf(f, "},\n");
    }
    fprintf(f, "};\nalignas(64) static const int16_t NN_B1[%d] = {", HL);
    for (int j = 0; j < HL; j++) fprintf(f, "%d,", (int)std::lround(NB1[j] * QA));
    fprintf(f, "};\nalignas(64) static const int16_t NN_W2[%d] = {", 2 * HL);
    for (int j = 0; j < 2 * HL; j++) fprintf(f, "%d,", (int)std::lround(std::max(-32000.f, std::min(32000.f, W2[j] * QB))));
    fprintf(f, "};\nstatic const int32_t NN_B2 = %d;\n", (int)std::lround(NB2 * QA * QB));
    fclose(f);
}

static void save_float(const char* file) {
    FILE* f = fopen(file, "wb");
    fwrite(W1.data(), 4, W1.size(), f); fwrite(NB1.data(), 4, NB1.size(), f); fwrite(W2.data(), 4, W2.size(), f); fwrite(&NB2, 4, 1, f);
    fclose(f);
}
static bool load_float(const char* file) {
    FILE* f = fopen(file, "rb");
    if (!f) return false;
    size_t r = fread(W1.data(), 4, W1.size(), f); r += fread(NB1.data(), 4, NB1.size(), f); r += fread(W2.data(), 4, W2.size(), f); r += fread(&NB2, 4, 1, f);
    fclose(f);
    return r == W1.size() + NB1.size() + W2.size() + 1;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "check") {
        BB::init(); Zobrist::init();
        if (!load_float(argv[2])) { puts("no weights"); return 1; }
        std::string fen;
        while (std::getline(std::cin, fen)) {
            if (fen.empty()) continue;
            Position pos; pos.set_fen(fen);
            Sample s; s.n = 0; s.stm = (uint8_t)pos.stm; s.target = 0;
            U64 b = pos.pieces(); while (b) { int sq = pop_lsb(b); s.f[s.n++] = (uint16_t)(pos.board[sq] * 64 + sq); }
            float accW[HL], accB[HL];
            for (int i = 0; i < HL; i++) { accW[i] = NB1[i]; accB[i] = NB1[i]; }
            for (int k = 0; k < s.n; k++) { const float* rw = &W1[s.f[k] * HL]; const float* rb = &W1[flip_idx(s.f[k]) * HL]; for (int i = 0; i < HL; i++) { accW[i] += rw[i]; accB[i] += rb[i]; } }
            float* aU = s.stm == WHITE ? accW : accB; float* aT = s.stm == WHITE ? accB : accW;
            float out = NB2; for (int i = 0; i < HL; i++) out += W2[i] * std::min(1.f, std::max(0.f, aU[i])) + W2[HL + i] * std::min(1.f, std::max(0.f, aT[i]));
            printf("float eval %.1f%c", out, 10);
        }
        return 0;
    }
    if (argc < 7) { printf("usage: nnue_train <out.h> <epochs> <threads> <lambda> <lr> <data>...\n"); return 1; }
    BB::init(); Zobrist::init();
    const char* outfile = argv[1];
    int epochs = atoi(argv[2]);
    int nthreads = atoi(argv[3]);
    double lambda = atof(argv[4]);
    float lr = (float)atof(argv[5]);
    int decayEvery = 30;
    if (getenv("NN_DECAY")) decayEvery = atoi(getenv("NN_DECAY"));

    for (int a = 6; a < argc; a++) {
        std::ifstream in(argv[a]);
        std::string line;
        while (std::getline(in, line)) {
            size_t p1 = line.find('|'); if (p1 == std::string::npos) continue;
            size_t p2 = line.find('|', p1 + 1); if (p2 == std::string::npos) continue;
            Position pos; pos.set_fen(line.substr(0, p1));
            double score = atof(line.c_str() + p1 + 1);   // white POV cp
            double result = atof(line.c_str() + p2 + 1);
            Sample s; s.n = 0; s.stm = (uint8_t)pos.stm;
            U64 b = pos.pieces();
            while (b) { int sq = pop_lsb(b); int pc = pos.board[sq]; s.f[s.n++] = (uint16_t)(pc * 64 + sq); }
            double sigScore = 1.0 / (1.0 + std::exp(-K_SIG * score));
            double t = lambda * result + (1 - lambda) * sigScore;
            if (pos.stm == BLACK) t = 1.0 - t;   // target from side-to-move perspective
            s.target = (float)t;
            data.push_back(s);
        }
        printf("loaded %s -> %zu samples\n", argv[a], data.size()); fflush(stdout);
    }
    std::mt19937_64 rng(12345);
    std::shuffle(data.begin(), data.end(), rng);
    size_t nval = data.size() / 20;
    size_t ntrain = data.size() - nval;

    // init
    std::normal_distribution<float> nd(0.f, 1.f);
    for (auto& w : W1) w = nd(rng) * 0.1f;
    for (auto& w : NB1) w = 0.f;
    for (auto& w : W2) w = nd(rng) * 0.1f;
    NB2 = 0;
    bool resumed = load_float("source/build/nnue_float.bin");
    if (resumed) printf("resumed from float weights\n");

    const size_t BATCH = 16384;
    std::vector<Grad> grads(nthreads);
    float b1t = 1.f, b2t = 1.f;
    for (int ep = 0; ep < epochs; ep++) {
        auto t0 = std::chrono::steady_clock::now();
        std::shuffle(data.begin(), data.begin() + ntrain, rng);
        double trainLoss = 0;
        if (ep > 0 && ep % decayEvery == 0) lr *= 0.5f;
        for (size_t start = 0; start < ntrain; start += BATCH) {
            size_t end = std::min(ntrain, start + BATCH);
            std::vector<std::thread> th;
            for (int t = 0; t < nthreads; t++) {
                grads[t].zero();
                th.emplace_back([&, t]() {
                    size_t a = start + (end - start) * t / nthreads, b = start + (end - start) * (t + 1) / nthreads;
                    for (size_t i = a; i < b; i++) forward_backward(data[i], &grads[t], &grads[t].loss);
                });
            }
            for (auto& x : th) x.join();
            for (int t = 1; t < nthreads; t++) {
                for (size_t i = 0; i < grads[0].W1.size(); i++) grads[0].W1[i] += grads[t].W1[i];
                for (int i = 0; i < HL; i++) grads[0].NB1[i] += grads[t].NB1[i];
                for (int i = 0; i < 2 * HL; i++) grads[0].W2[i] += grads[t].W2[i];
                grads[0].NB2 += grads[t].NB2;
                grads[0].loss += grads[t].loss;
            }
            trainLoss += grads[0].loss;
            float scale = 1.f / (end - start);
            b1t *= 0.9f; b2t *= 0.999f;
            adam(W1, mW1, vW1, grads[0].W1, lr, b1t, b2t, scale);
            adam(NB1, mB1, vB1, grads[0].NB1, lr, b1t, b2t, scale);
            adam(W2, mW2, vW2, grads[0].W2, lr, b1t, b2t, scale);
            {
                float gi = grads[0].NB2 * scale;
                mB2 = 0.9f * mB2 + 0.1f * gi; vB2 = 0.999f * vB2 + 0.001f * gi * gi;
                NB2 -= lr * (mB2 / (1 - b1t)) / (std::sqrt(vB2 / (1 - b2t)) + 1e-8f);
            }
        }
        // validation
        std::vector<double> vl(nthreads, 0.0);
        std::vector<std::thread> th;
        for (int t = 0; t < nthreads; t++)
            th.emplace_back([&, t]() {
                size_t a = ntrain + nval * t / nthreads, b = ntrain + nval * (t + 1) / nthreads;
                for (size_t i = a; i < b; i++) forward_backward(data[i], nullptr, &vl[t]);
            });
        for (auto& x : th) x.join();
        double valLoss = 0; for (double v : vl) valLoss += v;
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("epoch %d lr %.5f train %.6f val %.6f (%.0fs)\n", ep, lr, trainLoss / ntrain, valLoss / nval, secs);
        fflush(stdout);
        write_header(outfile);
        save_float("source/build/nnue_float.bin");
    }
    printf("done\n");
    return 0;
}
