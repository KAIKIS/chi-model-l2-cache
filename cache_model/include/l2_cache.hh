#pragma once

#include "cache_line.hh"
#include "chi_opcode.hh"

#include <array>
#include <cstdint>
#include <vector>

namespace chi {

constexpr int CACHE_SETS = 512;  // 512 sets × 8 ways × 64B = 256KB

// Result of a cache lookup
enum class LookupResult {
    Hit,           // Data available in cache
    Miss,          // Need to fetch from memory
    MissEvictDirty,// Miss, need to evict dirty line first
};

struct LookupResponse {
    LookupResult result;
    LineState    state;       // Current state of the line (valid on hit)
    uint8_t*     data;        // Pointer to cache line data (valid on hit)
    uint64_t     evictTag;    // Tag of evicted line (valid on MissEvictDirty)
    uint8_t*     evictData;   // Data of evicted line (valid on MissEvictDirty)
    NodeID       evictSharer; // Single sharer of evicted SD line (if applicable)
};

class L2Cache {
public:
    L2Cache();

    // Decompose address into tag, set index, offset
    static uint64_t getTag(Addr addr);
    static int      getSetIndex(Addr addr);
    static Addr     makeAddr(uint64_t tag, int setIndex);

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

private:
    std::array<CacheSet, CACHE_SETS> sets_;
};

} // namespace chi
