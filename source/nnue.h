#pragma once
#include "types.h"
#include <immintrin.h>

#ifdef USE_NNUE
#include "nnue_weights.h"
#else
#define NN_HL 128
#define NN_QA 127
#define NN_QB 64
#endif

// Accumulator: two perspectives (white view, black view), int16 scaled by NN_QA
struct Accumulator {
    alignas(32) int16_t v[2][NN_HL];
};

namespace NNUE {
extern bool Enabled;

// feature index from white's perspective: piece (0..11) * 64 + square
inline int feat_w(int pc, int s) { return pc * 64 + s; }
// from black's perspective: colour flipped, square mirrored
inline int feat_b(int pc, int s) { return ((pc >= 6 ? pc - 6 : pc + 6)) * 64 + (s ^ 56); }

#ifdef USE_NNUE
inline void init_acc(Accumulator& a) {
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < NN_HL; i += 16)
            _mm256_store_si256((__m256i*)&a.v[p][i], _mm256_load_si256((const __m256i*)&NN_B1[i]));
}
inline void add_piece(Accumulator& a, int pc, int s) {
    const int16_t* rw = NN_W1[feat_w(pc, s)];
    const int16_t* rb = NN_W1[feat_b(pc, s)];
    for (int i = 0; i < NN_HL; i += 16) {
        _mm256_store_si256((__m256i*)&a.v[0][i], _mm256_add_epi16(_mm256_load_si256((const __m256i*)&a.v[0][i]), _mm256_load_si256((const __m256i*)&rw[i])));
        _mm256_store_si256((__m256i*)&a.v[1][i], _mm256_add_epi16(_mm256_load_si256((const __m256i*)&a.v[1][i]), _mm256_load_si256((const __m256i*)&rb[i])));
    }
}
inline void remove_piece(Accumulator& a, int pc, int s) {
    const int16_t* rw = NN_W1[feat_w(pc, s)];
    const int16_t* rb = NN_W1[feat_b(pc, s)];
    for (int i = 0; i < NN_HL; i += 16) {
        _mm256_store_si256((__m256i*)&a.v[0][i], _mm256_sub_epi16(_mm256_load_si256((const __m256i*)&a.v[0][i]), _mm256_load_si256((const __m256i*)&rw[i])));
        _mm256_store_si256((__m256i*)&a.v[1][i], _mm256_sub_epi16(_mm256_load_si256((const __m256i*)&a.v[1][i]), _mm256_load_si256((const __m256i*)&rb[i])));
    }
}
inline void move_piece(Accumulator& a, int pc, int from, int to) {
    const int16_t* rwf = NN_W1[feat_w(pc, from)];
    const int16_t* rbf = NN_W1[feat_b(pc, from)];
    const int16_t* rwt = NN_W1[feat_w(pc, to)];
    const int16_t* rbt = NN_W1[feat_b(pc, to)];
    for (int i = 0; i < NN_HL; i += 16) {
        __m256i w = _mm256_load_si256((const __m256i*)&a.v[0][i]);
        w = _mm256_add_epi16(_mm256_sub_epi16(w, _mm256_load_si256((const __m256i*)&rwf[i])), _mm256_load_si256((const __m256i*)&rwt[i]));
        _mm256_store_si256((__m256i*)&a.v[0][i], w);
        __m256i b = _mm256_load_si256((const __m256i*)&a.v[1][i]);
        b = _mm256_add_epi16(_mm256_sub_epi16(b, _mm256_load_si256((const __m256i*)&rbf[i])), _mm256_load_si256((const __m256i*)&rbt[i]));
        _mm256_store_si256((__m256i*)&a.v[1][i], b);
    }
}
// evaluate from side to move's perspective, in centipawns
inline int evaluate(const Accumulator& a, int stm) {
    const int16_t* us = a.v[stm];
    const int16_t* them = a.v[stm ^ 1];
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(NN_QA);
    __m256i sum = zero;
    for (int i = 0; i < NN_HL; i += 16) {
        __m256i hu = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)&us[i]), zero), qa);
        __m256i ht = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)&them[i]), zero), qa);
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(hu, _mm256_load_si256((const __m256i*)&NN_W2[i])));
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(ht, _mm256_load_si256((const __m256i*)&NN_W2[NN_HL + i])));
    }
    __m128i lo = _mm256_castsi256_si128(sum), hi = _mm256_extracti128_si256(sum, 1);
    __m128i s = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0x4E));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0xB1));
    int dot = _mm_cvtsi128_si32(s);
    return (dot + NN_B2) / (NN_QA * NN_QB);
}
#else
inline void init_acc(Accumulator&) {}
inline void add_piece(Accumulator&, int, int) {}
inline void remove_piece(Accumulator&, int, int) {}
inline void move_piece(Accumulator&, int, int, int) {}
inline int evaluate(const Accumulator&, int) { return 0; }
#endif
}
