#include "cache_line.hh"
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
    assert(set.lru[0] == 0);  // way 0 is now MRU
    assert(set.victimWay() == 1);  // way 1 is now LRU

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

int main() {
    std::cout << "L2 Cache tests:\n";
    testCacheLineInitialState();
    testCacheLineStateQueries();
    testCacheLineInvalidate();
    testCacheSetLookupHit();
    testCacheSetLRU();
    testCacheSetFindInvalid();
    std::cout << "All L2 Cache basic tests passed!\n";
    return 0;
}
