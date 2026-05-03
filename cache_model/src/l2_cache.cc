#include "l2_cache.hh"
#include <cassert>
#include <cstring>

namespace chi {

// Address decomposition for 256KB cache, 64B lines, 512 sets
// offset: bits [5:0]  (6 bits for 64B)
// set:    bits [14:6]  (9 bits for 512 sets)
// tag:    bits [63:15] (remaining bits)

L2Cache::L2Cache() {
    // CacheSet default-constructs all lines to state I
}

uint64_t L2Cache::getTag(Addr addr) {
    return addr >> 15;
}

int L2Cache::getSetIndex(Addr addr) {
    return (addr >> 6) & 0x1FF;  // 9 bits
}

Addr L2Cache::makeAddr(uint64_t tag, int setIndex) {
    return (tag << 15) | (setIndex << 6);
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
        return resp;
    }

    // Miss — find a free way or choose victim
    int victim = set.findInvalid();
    if (victim >= 0) {
        // Free way available
        resp.result = LookupResult::Miss;
        resp.state = LineState::I;
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
    } else {
        resp.result = LookupResult::Miss;
    }

    return resp;
}

void L2Cache::fill(Addr addr, LineState state, const uint8_t* data) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    assert(set.lookup(tag) < 0 && "fill() called on address already in cache");

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
}

void L2Cache::addSharer(Addr addr, NodeID node) {
    uint64_t tag = getTag(addr);
    int setIdx = getSetIndex(addr);
    CacheSet& set = sets_[setIdx];

    int way = set.lookup(tag);
    if (way >= 0) {
        set.lines[way].sharers.insert(node);
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
        set.lines[way].invalidate();
    }
}

} // namespace chi
