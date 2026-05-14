#pragma once

#include "chi_types.hh"

#include <cstdint>
#include <set>
#include <cstring>
#include <vector>

namespace chi {

enum class LineState : uint8_t {
    I   = 0,  // Invalid
    UC  = 1,  // Unique Clean
    SC  = 2,  // Shared Clean
    UD  = 3,  // Unique Dirty
    SD  = 4,  // Shared Dirty
};

inline const char* lineStateToString(LineState s) {
    switch (s) {
        case LineState::I:   return "I";
        case LineState::UC:  return "UC";
        case LineState::SC:  return "SC";
        case LineState::UD:  return "UD";
        case LineState::SD:  return "SD";
        default:             return "?";
    }
}

constexpr int CACHE_LINE_SIZE = 64;

struct CacheLine {
    uint64_t        tag   = 0;
    LineState       state = LineState::I;
    uint8_t         data[CACHE_LINE_SIZE] = {};
    std::set<NodeID> sharers;

    bool isValid() const { return state != LineState::I; }
    bool isDirty() const { return state == LineState::UD || state == LineState::SD; }
    bool isShared() const { return state == LineState::SC || state == LineState::SD; }
    bool isUnique() const { return state == LineState::UC || state == LineState::UD; }

    void invalidate() {
        state = LineState::I;
        tag = 0;
        sharers.clear();
        std::memset(data, 0, CACHE_LINE_SIZE);
    }
};

struct CacheSet {
    int numWays;
    std::vector<CacheLine> lines;
    std::vector<int> lru;

    explicit CacheSet(int ways) : numWays(ways), lines(ways) {
        lru.resize(ways);
        for (int i = 0; i < ways; i++) lru[i] = i;
    }

    // Returns way index of hit, or -1 on miss
    int lookup(uint64_t tag) const {
        for (int i = 0; i < numWays; i++) {
            if (lines[i].isValid() && lines[i].tag == tag) {
                return i;
            }
        }
        return -1;
    }

    // Update LRU after accessing way i:
    // Move accessed way to MRU position (numWays - 1), compress remaining positions.
    void touch(int way) {
        int old = lru[way];
        for (int j = 0; j < numWays; j++) {
            if (lru[j] > old) {
                lru[j]--;
            }
        }
        lru[way] = numWays - 1;
    }

    // Find way with lowest LRU value (least recently used).
    int victimWay() const {
        int best = 0;
        for (int i = 1; i < numWays; i++) {
            if (lru[i] <= lru[best]) {
                best = i;
            }
        }
        return best;
    }

    // Find an invalid (empty) way, or -1 if full
    int findInvalid() const {
        for (int i = 0; i < numWays; i++) {
            if (!lines[i].isValid()) {
                return i;
            }
        }
        return -1;
    }
};

} // namespace chi
