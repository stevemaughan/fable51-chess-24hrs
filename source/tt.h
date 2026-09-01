#pragma once
#include "types.h"
#include <cstdlib>
#include <vector>

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

#pragma pack(push, 1)
struct TTEntry {
    uint16_t key16;
    Move move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t genBound;  // gen (6 bits) << 2 | bound
    int bound() const { return genBound & 3; }
    int gen() const { return genBound >> 2; }
};
#pragma pack(pop)

struct TTBucket {
    TTEntry e[3];
    uint16_t pad;
};
static_assert(sizeof(TTBucket) == 32, "bucket size");

class TranspositionTable {
public:
    TTBucket* table = nullptr;
    size_t numBuckets = 0;
    uint8_t generation = 0;

    ~TranspositionTable() { if (table) std::free(table); }

    void resize(size_t mb) {
        if (table) std::free(table);
        numBuckets = (mb * 1024 * 1024) / sizeof(TTBucket);
        if (numBuckets < 1024) numBuckets = 1024;
        table = (TTBucket*)std::malloc(numBuckets * sizeof(TTBucket));
        clear();
    }
    void clear() {
        memset(table, 0, numBuckets * sizeof(TTBucket));
        generation = 0;
    }
    void new_search() { generation = (generation + 1) & 63; }

    inline size_t index(U64 key) const {
        return (size_t)(((unsigned __int128)key * (unsigned __int128)numBuckets) >> 64);
    }
    inline void prefetch(U64 key) const { _mm_prefetch((const char*)&table[index(key)], _MM_HINT_T0); }

    TTEntry* probe(U64 key, bool& found) {
        TTBucket& b = table[index(key)];
        uint16_t k16 = (uint16_t)(key >> 48);
        for (int i = 0; i < 3; i++)
            if (b.e[i].key16 == k16 && b.e[i].genBound) { found = true; return &b.e[i]; }
        found = false;
        // choose replacement: lowest depth adjusted by age
        TTEntry* r = &b.e[0];
        for (int i = 1; i < 3; i++) {
            int rs = r->depth - ((generation - r->gen()) & 63) * 2;
            int is = b.e[i].depth - ((generation - b.e[i].gen()) & 63) * 2;
            if (is < rs) r = &b.e[i];
        }
        return r;
    }

    void store(TTEntry* e, U64 key, Move move, int score, int eval, int depth, int bound) {
        uint16_t k16 = (uint16_t)(key >> 48);
        if (move != MOVE_NONE || e->key16 != k16) e->move = move;
        if (bound == BOUND_EXACT || e->key16 != k16 || depth + 4 > e->depth || e->gen() != generation) {
            e->key16 = k16;
            e->score = (int16_t)score;
            e->eval = (int16_t)eval;
            e->depth = (uint8_t)depth;
            e->genBound = (uint8_t)((generation << 2) | bound);
        }
    }

    int hashfull() const {
        int cnt = 0;
        for (int i = 0; i < 1000; i++)
            for (int j = 0; j < 3; j++)
                if (table[i].e[j].genBound && table[i].e[j].gen() == generation) cnt++;
        return cnt / 3;
    }
};
