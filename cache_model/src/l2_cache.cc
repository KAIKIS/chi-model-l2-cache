#include "l2_cache.hh"
#include "chi_log.hh"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chi {

L2Cache::L2Cache(int numSets, int numWays)
    : numSets_(numSets), numWays_(numWays)
{
    // numSets must be a power of 2 for correct address decomposition
    assert((numSets & (numSets - 1)) == 0 && "numSets must be a power of 2");

    // Precompute address decomposition parameters
    setBits_  = static_cast<unsigned>(std::log2(numSets));
    setMask_  = (1u << setBits_) - 1;
    tagShift_ = OFFSET_BITS + setBits_;

    // Initialize all cache sets with the configured associativity
    sets_.reserve(numSets);
    for (int i = 0; i < numSets; i++) {
        sets_.emplace_back(numWays);
    }
}

uint64_t L2Cache::getTag(Addr addr) const {
    return addr >> tagShift_;
}

int L2Cache::getSetIndex(Addr addr) const {
    return (addr >> OFFSET_BITS) & setMask_;
}

Addr L2Cache::makeAddr(uint64_t tag, int setIndex) const {
    return (tag << tagShift_) | (setIndex << OFFSET_BITS);
}

LookupResponse L2Cache::lookup(Addr addr) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    LookupResponse resp{};
    std::memset(resp.data, 0, CACHE_LINE_SIZE);
    std::memset(resp.evictData, 0, CACHE_LINE_SIZE);

    int way = set.lookup(tag);
    if (way >= 0) {
        // Hit
        resp.result = LookupResult::Hit;
        resp.state = set.lines[way].state;
        std::memcpy(resp.data, set.lines[way].data, CACHE_LINE_SIZE);
        set.touch(way);
        CHI_LOG_DEBUG("lookup addr=%#x HIT state=%s", addr, lineStateToString(resp.state));
        return resp;
    }

    // Miss — find a free way or choose victim
    int victim = set.findInvalid();
    if (victim >= 0) {
        // Free way available
        resp.result = LookupResult::Miss;
        resp.state = LineState::I;
        CHI_LOG_DEBUG("lookup addr=%#x MISS (free way)", addr);
        return resp;
    }

    // All ways occupied — evict LRU
    victim = set.victimWay();
    CacheLine& victimLine = set.lines[victim];

    if (victimLine.isDirty()) {
        resp.result = LookupResult::MissEvictDirty;
        resp.evictTag = victimLine.tag;
        std::memcpy(resp.evictData, victimLine.data, CACHE_LINE_SIZE);
        if (victimLine.isShared() && !victimLine.sharers.empty()) {
            resp.evictSharer = *victimLine.sharers.begin();
        }
        CHI_LOG_DEBUG("lookup addr=%#x MISS evict dirty tag=%#x", addr, victimLine.tag);
    } else {
        resp.result = LookupResult::Miss;
        CHI_LOG_DEBUG("lookup addr=%#x MISS evict clean", addr);
    }

    return resp;
}

void L2Cache::fill(Addr addr, LineState state, const uint8_t* data) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    // If the line is already in cache, update it in place
    int existingWay = set.lookup(tag);
    if (existingWay >= 0) {
        CacheLine& line = set.lines[existingWay];
        CHI_LOG_DEBUG("fill addr=%#x update existing state=%s->%s",
                      addr, lineStateToString(line.state), lineStateToString(state));
        line.state = state;
        std::memcpy(line.data, data, CACHE_LINE_SIZE);
        set.touch(existingWay);
        return;
    }

    int way = set.findInvalid();
    if (way < 0) {
        way = set.victimWay();
    }

    CacheLine& line = set.lines[way];
    line.tag = tag;
    line.state = state;
    line.sharers.clear();
    std::memcpy(line.data, data, CACHE_LINE_SIZE);
    set.touch(way);
    CHI_LOG_DEBUG("fill addr=%#x way=%d state=%s", addr, way, lineStateToString(state));
}

void L2Cache::addSharer(Addr addr, NodeID node) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        set.lines[way].sharers.insert(node);
        CHI_LOG_DEBUG("addSharer addr=%#x node=%d sharers=%d",
                      addr, node, (int)set.lines[way].sharers.size());
    }
}

void L2Cache::removeSharer(Addr addr, NodeID node) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        set.lines[way].sharers.erase(node);
    }
}

void L2Cache::clearSharers(Addr addr) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        set.lines[way].sharers.clear();
    }
}

const std::set<NodeID>& L2Cache::getSharers(Addr addr) const {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    const CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    static const std::set<NodeID> empty;
    if (way >= 0) {
        return set.lines[way].sharers;
    }
    return empty;
}

void L2Cache::setState(Addr addr, LineState state) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        CHI_LOG_DEBUG("setState addr=%#x %s->%s",
                      addr, lineStateToString(set.lines[way].state), lineStateToString(state));
        set.lines[way].state = state;
    }
}

LineState L2Cache::getState(Addr addr) const {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    const CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        return set.lines[way].state;
    }
    return LineState::I;
}

bool L2Cache::needsWriteback(Addr addr) const {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    const CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        return set.lines[way].isDirty();
    }
    return false;
}

const uint8_t* L2Cache::getData(Addr addr) const {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    const CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        return set.lines[way].data;
    }
    return nullptr;
}

void L2Cache::writeData(Addr addr, const uint8_t* data) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        std::memcpy(set.lines[way].data, data, CACHE_LINE_SIZE);
    }
}

void L2Cache::invalidate(Addr addr) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        CHI_LOG_DEBUG("invalidate addr=%#x state=%s", addr, lineStateToString(set.lines[way].state));
        set.lines[way].invalidate();
    }
}

} // namespace chi
