#pragma once

#include "cache_line.hh"

#include <cstdint>
#include <set>
#include <vector>

namespace chi {

// Result of a cache lookup
enum class LookupResult {
    Hit,           // Data available in cache
    Miss,          // Need to fetch from memory
    MissEvictDirty,// Miss, need to evict dirty line first
};

struct LookupResponse {
    LookupResult result;
    LineState    state;                        // Current state of the line (valid on hit)
    uint8_t      data[CACHE_LINE_SIZE];        // Copy of cache line data (valid on hit)
    uint64_t     evictTag;                     // Tag of evicted line (valid on MissEvictDirty)
    uint8_t      evictData[CACHE_LINE_SIZE];   // Copy of evicted line data (valid on MissEvictDirty)
    NodeID       evictSharer;                  // Single sharer of evicted SD line (if applicable)
};

class L2Cache {
public:
    // numSets: number of cache sets (e.g. 512 for 256KB, 2048 for 2MB)
    // numWays: associativity (e.g. 8 for L2, 16 for L3)
    L2Cache(int numSets = 512, int numWays = 8);

    // Decompose address into tag, set index, offset
    uint64_t getTag(Addr addr) const;
    int      getSetIndex(Addr addr) const;
    Addr     makeAddr(uint64_t tag, int setIndex) const;

    // Lookup a cache line
    LookupResponse lookup(Addr addr);

    // Fill a cache line from memory (after miss)
    void fill(Addr addr, LineState state, const uint8_t* data);

    // Add a sharer to a cache line
    void addSharer(Addr addr, NodeID node);

    // Remove a sharer from a cache line
    void removeSharer(Addr addr, NodeID node);

    // Clear all sharers from a cache line
    void clearSharers(Addr addr);

    // Get the sharers set for a cache line
    const std::set<NodeID>& getSharers(Addr addr) const;

    // Change the state of a cache line
    void setState(Addr addr, LineState state);

    // Get the current state of a cache line
    LineState getState(Addr addr) const;

    // Check if a cache line is dirty and needs writeback
    bool needsWriteback(Addr addr) const;

    // Get pointer to cache line data (for writeback)
    const uint8_t* getData(Addr addr) const;

    // Write data into an existing cache line (from WriteBackFull)
    void writeData(Addr addr, const uint8_t* data);

    // Invalidate a cache line
    void invalidate(Addr addr);

    int numSets() const { return numSets_; }
    int numWays() const { return numWays_; }

private:
    int numSets_;
    int numWays_;
    unsigned setBits_;     // log2(numSets_)
    unsigned setMask_;     // (1 << setBits_) - 1
    unsigned tagShift_;    // offsetBits + setBits_

    std::vector<CacheSet> sets_;

    static constexpr unsigned OFFSET_BITS = 6;  // log2(CACHE_LINE_SIZE)
};

} // namespace chi
