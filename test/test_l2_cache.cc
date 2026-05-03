#include "cache_line.hh"
#include "l2_cache.hh"
#include <cassert>
#include <iostream>

using namespace chi;

void testCacheLineInitialState() {
    CacheLine line;
    assert(!line.isValid());
    assert(!line.isDirty());
    assert(line.state == LineState::I);
    assert(line.tag == 0);
    assert(line.sharers.empty());
    std::cout << "  PASS: CacheLine initial state\n";
}

void testCacheLineStateQueries() {
    CacheLine line;
    line.tag = 0x1000;

    line.state = LineState::UC;
    assert(line.isValid());
    assert(!line.isDirty());
    assert(!line.isShared());
    assert(line.isUnique());

    line.state = LineState::SC;
    assert(line.isValid());
    assert(!line.isDirty());
    assert(line.isShared());
    assert(!line.isUnique());

    line.state = LineState::UD;
    assert(line.isValid());
    assert(line.isDirty());
    assert(!line.isShared());
    assert(line.isUnique());

    line.state = LineState::SD;
    assert(line.isValid());
    assert(line.isDirty());
    assert(line.isShared());
    assert(!line.isUnique());

    std::cout << "  PASS: CacheLine state queries\n";
}

void testCacheLineInvalidate() {
    CacheLine line;
    line.tag = 0x2000;
    line.state = LineState::UD;
    line.data[0] = 0xFF;
    line.sharers.insert(1);

    line.invalidate();
    assert(!line.isValid());
    assert(line.tag == 0);
    assert(line.sharers.empty());
    assert(line.data[0] == 0);
    std::cout << "  PASS: CacheLine invalidate\n";
}

void testCacheSetLookupHit() {
    CacheSet set;
    set.lines[3].tag = 0x5000;
    set.lines[3].state = LineState::SC;

    int way = set.lookup(0x5000);
    assert(way == 3);

    int miss = set.lookup(0x9999);
    assert(miss == -1);
    std::cout << "  PASS: CacheSet lookup hit/miss\n";
}

void testCacheSetLRU() {
    CacheSet set;
    // Mark all ways as valid with different tags
    for (int i = 0; i < CACHE_WAYS; i++) {
        set.lines[i].tag = 0x1000 * i;
        set.lines[i].state = LineState::SC;
        set.lru[i] = i;  // way 0 is LRU (lru=0), way 7 is MRU (lru=7)
    }

    // way 0 should be victim (lowest LRU)
    assert(set.victimWay() == 0);

    // Touch way 0 -> it becomes MRU
    set.touch(0);
    assert(set.lru[0] == CACHE_WAYS - 1);  // way 0 is now MRU
    assert(set.victimWay() == 1);  // way 1 is now LRU

    // Test touching a non-zero way (way 5)
    {
        CacheSet set2;
        for (int i = 0; i < CACHE_WAYS; i++) {
            set2.lines[i].tag = 0x1000 * i;
            set2.lines[i].state = LineState::SC;
            set2.lru[i] = i;  // way 0 = LRU, way 7 = MRU
        }
        set2.touch(5);
        assert(set2.lru[5] == CACHE_WAYS - 1);  // way 5 is now MRU
        assert(set2.victimWay() != 5);           // touched way should NOT be victim
        assert(set2.victimWay() == 0);           // way 0 is still LRU
    }

    std::cout << "  PASS: CacheSet LRU\n";
}

void testCacheSetFindInvalid() {
    CacheSet set;
    set.lines[0].state = LineState::SC;
    set.lines[1].state = LineState::I;
    set.lines[2].state = LineState::UD;

    int inv = set.findInvalid();
    assert(inv == 1);

    // Fill all ways
    for (int i = 0; i < CACHE_WAYS; i++) {
        set.lines[i].state = LineState::SC;
    }
    assert(set.findInvalid() == -1);

    std::cout << "  PASS: CacheSet findInvalid\n";
}

void testL2CacheAddressDecomposition() {
    Addr addr = 0x12345678;
    assert(L2Cache::getTag(addr) == (0x12345678ULL >> 15));
    assert(L2Cache::getSetIndex(addr) == ((0x12345678 >> 6) & 0x1FF));

    // Round-trip
    uint64_t tag = L2Cache::getTag(addr);
    int set = L2Cache::getSetIndex(addr);
    Addr reconstructed = L2Cache::makeAddr(tag, set);
    assert((addr >> 6) == (reconstructed >> 6));  // Same line address

    std::cout << "  PASS: L2Cache address decomposition\n";
}

void testL2CacheFillAndLookup() {
    L2Cache cache;
    Addr addr = 0x4000;  // set 16, tag = 0x4000 >> 15

    // Miss
    auto resp = cache.lookup(addr);
    assert(resp.result == LookupResult::Miss);

    // Fill
    uint8_t data[64];
    for (int i = 0; i < 64; i++) data[i] = i;
    cache.fill(addr, LineState::SC, data);

    // Hit
    resp = cache.lookup(addr);
    assert(resp.result == LookupResult::Hit);
    assert(resp.state == LineState::SC);
    assert(resp.data[0] == 0);
    assert(resp.data[63] == 63);

    std::cout << "  PASS: L2Cache fill and lookup\n";
}

void testL2CacheSharers() {
    L2Cache cache;
    Addr addr = 0x8000;

    uint8_t data[64] = {};
    cache.fill(addr, LineState::SC, data);

    cache.addSharer(addr, 1);
    cache.addSharer(addr, 2);
    assert(cache.getSharers(addr).size() == 2);

    cache.removeSharer(addr, 1);
    assert(cache.getSharers(addr).size() == 1);
    assert(cache.getSharers(addr).count(2) == 1);

    cache.clearSharers(addr);
    assert(cache.getSharers(addr).empty());

    std::cout << "  PASS: L2Cache sharers\n";
}

void testL2CacheStateTransitions() {
    L2Cache cache;
    Addr addr = 0xC000;

    uint8_t data[64] = {};
    cache.fill(addr, LineState::SC, data);
    assert(cache.getState(addr) == LineState::SC);

    cache.setState(addr, LineState::UD);
    assert(cache.getState(addr) == LineState::UD);
    assert(cache.needsWriteback(addr));

    cache.setState(addr, LineState::UC);
    assert(!cache.needsWriteback(addr));

    cache.invalidate(addr);
    assert(cache.getState(addr) == LineState::I);

    std::cout << "  PASS: L2Cache state transitions\n";
}

void testL2CacheEviction() {
    L2Cache cache;
    Addr baseAddr = 0x10000;

    // Fill 8 ways in the same set
    uint8_t data[64];
    for (int i = 0; i < 8; i++) {
        Addr addr = baseAddr + (i << 15);  // Same set, different tags
        for (int j = 0; j < 64; j++) data[j] = i;
        cache.fill(addr, LineState::SC, data);
    }

    // All ways occupied, next fill should evict LRU
    Addr newAddr = baseAddr + (8 << 15);
    auto resp = cache.lookup(newAddr);
    assert(resp.result == LookupResult::Miss);  // Clean, no writeback needed

    std::cout << "  PASS: L2Cache eviction\n";
}

void testL2CacheDirtyEviction() {
    L2Cache cache;
    Addr baseAddr = 0x20000;

    // Fill 8 ways with dirty data (UD state)
    uint8_t data[64];
    for (int i = 0; i < 8; i++) {
        Addr addr = baseAddr + (i << 15);  // Same set, different tags
        for (int j = 0; j < 64; j++) data[j] = i;
        cache.fill(addr, LineState::UD, data);
    }

    // Lookup 9th address — should trigger MissEvictDirty
    Addr newAddr = baseAddr + (8 << 15);
    auto resp = cache.lookup(newAddr);
    assert(resp.result == LookupResult::MissEvictDirty);
    assert(resp.evictData[0] == 0);  // Victim was first filled with all 0s
    // evictTag should be the tag of the LRU victim (baseAddr, first filled)
    assert(resp.evictTag == L2Cache::getTag(baseAddr));

    std::cout << "  PASS: L2Cache dirty eviction\n";
}

void testL2CacheEvictionCycle() {
    L2Cache cache;
    Addr baseAddr = 0x30000;

    // Fill 8 ways
    uint8_t data[64];
    for (int i = 0; i < 8; i++) {
        Addr addr = baseAddr + (i << 15);
        for (int j = 0; j < 64; j++) data[j] = i;
        cache.fill(addr, LineState::SC, data);
    }

    // Lookup 9th address — miss
    Addr newAddr = baseAddr + (8 << 15);
    auto resp = cache.lookup(newAddr);
    assert(resp.result == LookupResult::Miss);

    // Fill the new address (evicts LRU victim)
    uint8_t newData[64];
    for (int j = 0; j < 64; j++) newData[j] = 0xAB;
    cache.fill(newAddr, LineState::SC, newData);

    // New address should hit
    resp = cache.lookup(newAddr);
    assert(resp.result == LookupResult::Hit);
    assert(resp.data[0] == 0xAB);

    // Old LRU address should miss (was evicted)
    resp = cache.lookup(baseAddr);
    assert(resp.result == LookupResult::Miss);

    std::cout << "  PASS: L2Cache eviction cycle\n";
}

int main() {
    std::cout << "L2 Cache tests:\n";
    testCacheLineInitialState();
    testCacheLineStateQueries();
    testCacheLineInvalidate();
    testCacheSetLookupHit();
    testCacheSetLRU();
    testCacheSetFindInvalid();
    testL2CacheAddressDecomposition();
    testL2CacheFillAndLookup();
    testL2CacheSharers();
    testL2CacheStateTransitions();
    testL2CacheEviction();
    testL2CacheDirtyEviction();
    testL2CacheEvictionCycle();
    std::cout << "All L2 Cache basic tests passed!\n";
    return 0;
}
