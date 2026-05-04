# Real L2 Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the pass-through L2 cache with a real 256KB cache implementing CHI state model (I/UC/SC/UD/SD), LRU replacement, snoop operations, and dual-core support.

**Architecture:** OurL2Middleware (gem5 CHIGenericController) owns an L2Cache instance. L2Cache is a pure C++ data structure (no gem5 dependency) that manages tag arrays, data arrays, and CHI state. OurL2Middleware translates gem5 messages into L2Cache operations and constructs responses. SnpCleanInvalid is used for multi-core coherence.

**Tech Stack:** C++17, gem5 Ruby CHI, aarch64 cross-compilation

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `cache_model/include/cache_line.hh` | Create | CacheLine, CacheSet, LineState enum |
| `cache_model/include/l2_cache.hh` | Create | L2Cache class declaration |
| `cache_model/src/l2_cache.cc` | Create | L2Cache implementation |
| `chi_model/include/chi_opcode.hh` | Modify | Add SnpCleanInvalid, SnpCleanInvalidResp opcodes |
| `gem5/src/mem/my_l2/our_l2_middleware.hh` | Modify | Remove HnNode, add L2Cache + PendingTxn |
| `gem5/src/mem/my_l2/our_l2_middleware.cc` | Modify | Rewrite for sync model |
| `gem5/src/mem/my_l2/SConscript` | Modify | Add l2_cache.cc to build |
| `gem5/configs/example/arm/our_l2_hierarchy.py` | Modify | num_cores=2, l1_size=32KB |
| `test/test_dual_core.cc` | Create | Dual-core test (single binary, shared memory coordination) |
| `test/test_l2_cache.cc` | Create | L2Cache unit tests |
| `test/CMakeLists.txt` | Modify | Add test_l2_cache target |

---

### Task 1: CacheLine and CacheSet Data Structures

**Files:**
- Create: `cache_model/include/cache_line.hh`
- Test: `test/test_l2_cache.cc`

- [ ] **Step 1: Create cache_line.hh with LineState enum and CacheLine struct**

```cpp
#pragma once

#include "chi_types.hh"

#include <array>
#include <cstdint>
#include <set>
#include <cstring>

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
constexpr int CACHE_WAYS = 8;

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
    std::array<CacheLine, CACHE_WAYS> lines;
    std::array<int, CACHE_WAYS> lru = {};

    // Returns way index of hit, or -1 on miss
    int lookup(uint64_t tag) const {
        for (int i = 0; i < CACHE_WAYS; i++) {
            if (lines[i].isValid() && lines[i].tag == tag) {
                return i;
            }
        }
        return -1;
    }

    // Update LRU after accessing way i
    void touch(int way) {
        int old = lru[way];
        for (int j = 0; j < CACHE_WAYS; j++) {
            if (lru[j] < old) {
                lru[j]++;
            }
        }
        lru[way] = 0;
    }

    // Find way with lowest LRU value (least recently used)
    int victimWay() const {
        int best = 0;
        for (int i = 1; i < CACHE_WAYS; i++) {
            if (lru[i] > lru[best]) {
                best = i;
            }
        }
        return best;
    }

    // Find an invalid (empty) way, or -1 if full
    int findInvalid() const {
        for (int i = 0; i < CACHE_WAYS; i++) {
            if (!lines[i].isValid()) {
                return i;
            }
        }
        return -1;
    }
};

} // namespace chi
```

- [ ] **Step 2: Create test_l2_cache.cc with CacheLine and CacheSet tests**

```cpp
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

    // Touch way 0 → it becomes MRU
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
```

- [ ] **Step 3: Update test/CMakeLists.txt to add test_l2_cache target**

Add to `test/CMakeLists.txt`:
```cmake
add_executable(test_l2_cache test_l2_cache.cc)
target_link_libraries(test_l2_cache PRIVATE chi_model)
```

- [ ] **Step 4: Build and run tests**

Run: `cd /home/zhangkai/work/CHI-new/build && cmake .. && make test_l2_cache && ./test/test_l2_cache`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add cache_model/include/cache_line.hh test/test_l2_cache.cc test/CMakeLists.txt
git commit -m "feat(cache): add CacheLine and CacheSet data structures with LRU"
```

---

### Task 2: L2Cache Class

**Files:**
- Create: `cache_model/include/l2_cache.hh`
- Create: `cache_model/src/l2_cache.cc`
- Modify: `cache_model/CMakeLists.txt`
- Test: `test/test_l2_cache.cc`

- [ ] **Step 1: Create l2_cache.hh with L2Cache class**

```cpp
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
    // Sets state, copies data, updates LRU
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
```

- [ ] **Step 2: Create l2_cache.cc with implementation**

```cpp
#include "l2_cache.hh"
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
    resp.data = nullptr;
    resp.evictData = nullptr;

    int way = set.lookup(tag);
    if (way >= 0) {
        // Hit
        resp.result = LookupResult::Hit;
        resp.state = set.lines[way].state;
        resp.data = set.lines[way].data;
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
        resp.evictData = victimLine.data;
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
```

- [ ] **Step 3: Update cache_model/CMakeLists.txt**

```cmake
add_library(cache_model STATIC
    src/simple_l2_cache.cc
    src/l2_cache.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
```

- [ ] **Step 4: Add L2Cache tests to test_l2_cache.cc**

Append to `test/test_l2_cache.cc`:
```cpp
#include "l2_cache.hh"

void testL2CacheAddressDecomposition() {
    // addr = 0x12345678
    // offset: bits [5:0] = 0x78 & 0x3F = 0x38
    // set:    bits [14:6] = (0x12345678 >> 6) & 0x1FF = 0x1A5
    // tag:    bits [63:15] = 0x12345678 >> 15 = 0x2468AC

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
```

And add these to `main()`:
```cpp
    testL2CacheAddressDecomposition();
    testL2CacheFillAndLookup();
    testL2CacheSharers();
    testL2CacheStateTransitions();
    testL2CacheEviction();
```

- [ ] **Step 5: Build and run tests**

Run: `cd /home/zhangkai/work/CHI-new/build && cmake .. && make test_l2_cache && ./test/test_l2_cache`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add cache_model/include/l2_cache.hh cache_model/src/l2_cache.cc cache_model/CMakeLists.txt test/test_l2_cache.cc
git commit -m "feat(cache): add L2Cache class with fill/lookup/sharers/eviction"
```

---

### Task 3: Add Snoop Opcodes

**Files:**
- Modify: `chi_model/include/chi_opcode.hh`

- [ ] **Step 1: Add SnpCleanInvalid and SnpCleanInvalidResp to Opcode enum**

In `chi_model/include/chi_opcode.hh`, add after `Comp = 0x22`:
```cpp
    // Snoop 请求
    SnpCleanInvalid     = 0x30,

    // Snoop 响应
    SnpCleanInvalidResp = 0x40,
```

And add to `opcodeToString()`:
```cpp
        case Opcode::SnpCleanInvalid:     return "SnpCleanInvalid";
        case Opcode::SnpCleanInvalidResp: return "SnpCleanInvalidResp";
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd /home/zhangkai/work/CHI-new/build && cmake .. && make chi_model`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add chi_model/include/chi_opcode.hh
git commit -m "feat(chi): add SnpCleanInvalid and SnpCleanInvalidResp opcodes"
```

---

### Task 4: OurL2Middleware Rewrite - Header and PendingTxn

**Files:**
- Modify: `gem5/src/mem/my_l2/our_l2_middleware.hh`

- [ ] **Step 1: Rewrite our_l2_middleware.hh**

Replace the entire file:
```cpp
#ifndef __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
#define __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "mem/ruby/protocol/chi/generic/CHIGenericController.hh"
#include "params/OurL2Middleware.hh"

// CHI-new model (no gem5 dependency)
#include "l2_cache.hh"

namespace gem5
{

namespace ruby
{

// Pending transaction states
enum class PendingState {
    WaitMemData,     // Waiting for memory CompData
    WaitMemWriteAck, // Waiting for memory WriteAck (writeback)
    WaitSnoopResp,   // Waiting for SnpCleanInvalid responses
    WaitSnoopThenMem,// Got all snoops, now waiting for memory
};

struct PendingTxn {
    CHI::CHIRequestType origType;   // Original L1 request type
    Addr                addr;
    MachineID           requestor;  // Original L1 that sent the request
    TxnID               origTxnId;  // Original transaction ID from L1
    PendingState        state;
    int                 pendingSnoopCount; // Number of snoop responses expected
    bool                needsWriteback;    // Dirty eviction in progress
    LineState           targetState;       // Target CHI state after completion
};

class OurL2Middleware : public CHIGenericController
{
  public:
    PARAMS(OurL2Middleware);
    OurL2Middleware(const Params &p);
    ~OurL2Middleware();

  protected:
    bool recvRequestMsg(const CHI::CHIRequestMsg *msg) override;
    bool recvSnoopMsg(const CHI::CHIRequestMsg *msg) override;
    bool recvResponseMsg(const CHI::CHIResponseMsg *msg) override;
    bool recvDataMsg(const CHI::CHIDataMsg *msg) override;

  private:
    // L2 Cache instance (pure C++, no gem5 dependency)
    std::unique_ptr<chi::L2Cache> l2Cache;

    // Pending transactions indexed by txnId
    std::unordered_map<TxnID, PendingTxn> pendingTxns;

    // Next snoop transaction ID counter
    TxnID nextSnoopTxnId = 10000;

    // Map snoop txnId → original txnId
    std::unordered_map<TxnID, TxnID> snoopToOrigTxn;

    // Helper: gem5 CHIRequestType → chi::Opcode
    chi::Opcode gem5ToOpcode(CHI::CHIRequestType type);

    // Helper: get string name for CHI request type
    std::string getChIRequestTypeName(CHI::CHIRequestType type);

    // Helper: send CompData to L1
    void sendCompData(const MachineID& dest, Addr addr,
                      const uint8_t* data, chi::LineState state,
                      TxnID txnId);

    // Helper: send Comp to L1
    void sendComp(const MachineID& dest, Addr addr,
                  CHI::CHIResponseType compType, TxnID txnId);

    // Helper: send ReadNoSnp to memory (non-DMT)
    void sendReadNoSnp(Addr addr, TxnID txnId);

    // Helper: send WriteNoSnp to memory
    void sendWriteNoSnp(Addr addr, const uint8_t* data, TxnID txnId);

    // Helper: send SnpCleanInvalid to an RN-F
    void sendSnpCleanInvalid(Addr addr, const MachineID& target, TxnID txnId);

    // Helper: complete a pending transaction (send response to L1, cleanup)
    void completePendingTxn(TxnID txnId);

    // Helper: process a request that has been looked up
    void processRequest(const CHI::CHIRequestMsg *msg);

    // Request counter for logging
    uint64_t requestCount;
};

} // namespace ruby
} // namespace gem5

#endif // __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
```

- [ ] **Step 2: Update SConscript to include l2_cache.cc**

Replace `gem5/src/mem/my_l2/SConscript`:
```python
Import('*')

# Only build when CHI protocol is selected
if env['CONF'].get('PROTOCOL', '') != 'CHI' and \
   not env['CONF'].get('RUBY_PROTOCOL_CHI', False):
    Return()

SimObject('OurL2Middleware.py', sim_objects=['OurL2Middleware'])

Source('our_l2_middleware.cc')

# Add CHI-new model include paths (relative to gem5 root)
chi_new_root = Dir('#..').abspath
env.Append(CPPPATH=[chi_new_root + '/chi_model/include'])
env.Append(CPPPATH=[chi_new_root + '/cache_model/include'])

# Add CHI-new model source files
Source(chi_new_root + '/chi_model/src/chi_hn_node.cc')
Source(chi_new_root + '/chi_model/src/chi_transaction.cc')
Source(chi_new_root + '/cache_model/src/l2_cache.cc')
```

- [ ] **Step 3: Commit**

```bash
git add gem5/src/mem/my_l2/our_l2_middleware.hh gem5/src/mem/my_l2/SConscript
git commit -m "feat(gem5): rewrite OurL2Middleware header for sync cache model"
```

---

### Task 5: OurL2Middleware Rewrite - Implementation

**Files:**
- Modify: `gem5/src/mem/my_l2/our_l2_middleware.cc`

- [ ] **Step 1: Rewrite our_l2_middleware.cc**

Replace the entire file with the new sync model implementation. This is the largest single file change. Key sections:

```cpp
#include "mem/my_l2/our_l2_middleware.hh"

#include "base/cprintf.hh"
#include "base/trace.hh"
#include "mem/ruby/protocol/CHI/CHIRequestType.hh"
#include "mem/ruby/protocol/CHI/CHIResponseType.hh"
#include "mem/ruby/protocol/CHI/CHIDataType.hh"
#include "mem/ruby/system/RubySystem.hh"

namespace gem5 {
namespace ruby {

using namespace CHI;

OurL2Middleware::OurL2Middleware(const OurL2MiddlewareParams &p)
    : CHIGenericController(p), requestCount(0)
{
    l2Cache = std::make_unique<chi::L2Cache>();
    std::cout << "[OurL2] Middleware initialized with 256KB L2 cache" << std::endl;
}

OurL2Middleware::~OurL2Middleware() {}

// gem5 CHIRequestType → chi::Opcode mapping
chi::Opcode OurL2Middleware::gem5ToOpcode(CHIRequestType type)
{
    switch (type) {
      case CHIRequestType_ReadShared:
      case CHIRequestType_StashOnceShared:
      case CHIRequestType_StashOnceUnique:
        return chi::Opcode::ReadShared;
      case CHIRequestType_ReadUnique:
      case CHIRequestType_MakeReadUnique:
        return chi::Opcode::ReadUnique;
      case CHIRequestType_CleanUnique:
        return chi::Opcode::CleanUnique;
      case CHIRequestType_WriteBackFull:
      case CHIRequestType_WriteUniqueFull:
      case CHIRequestType_WriteUniquePtl:
      case CHIRequestType_WriteUniqueZero:
      case CHIRequestType_WriteEvictFull:
      case CHIRequestType_WriteCleanFull:
      case CHIRequestType_Evict:
        return chi::Opcode::WriteBackFull;
      default:
        panic("[OurL2] Unknown request type: %s",
              CHIRequestType_to_string(type));
    }
}

std::string OurL2Middleware::getChIRequestTypeName(CHIRequestType type)
{
    return CHIRequestType_to_string(type);
}

// --- Helper: send CompData to L1 ---
void OurL2Middleware::sendCompData(
    const MachineID& dest, Addr addr,
    const uint8_t* data, chi::LineState state, TxnID txnId)
{
    auto dat = std::make_shared<CHIDataMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    dat->setaddr(addr);

    // Map CHI LineState to gem5 CHIDataType
    switch (state) {
      case chi::LineState::SC: dat->settype(CHIDataType_CompData_SC); break;
      case chi::LineState::UC: dat->settype(CHIDataType_CompData_UC); break;
      case chi::LineState::UD: dat->settype(CHIDataType_CompData_UD_PD); break;
      default: dat->settype(CHIDataType_CompData_SC); break;
    }

    dat->setresponder(m_machineID);
    NetDest destNet(m_ruby_system);
    destNet.add(dest);
    dat->setDestination(destNet);

    dat->m_dataBlk.setData(data, 0, cacheLineSize);
    WriteMask bitmask(cacheLineSize);
    bitmask.fillMask();
    dat->setbitMask(bitmask);

    dat->setusesTxnId(false);
    dat->settxnId(txnId);

    sendDataMsg(dat);
}

// --- Helper: send Comp to L1 ---
void OurL2Middleware::sendComp(
    const MachineID& dest, Addr addr,
    CHIResponseType compType, TxnID txnId)
{
    auto rsp = std::make_shared<CHIResponseMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    rsp->setaddr(addr);
    rsp->settype(compType);
    rsp->setresponder(m_machineID);
    NetDest destNet(m_ruby_system);
    destNet.add(dest);
    rsp->setDestination(destNet);
    rsp->setusesTxnId(false);
    rsp->settxnId(txnId);
    sendResponseMsg(rsp);
}

// --- Helper: send ReadNoSnp to memory (non-DMT) ---
void OurL2Middleware::sendReadNoSnp(Addr addr, TxnID txnId)
{
    auto req = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    req->setaddr(addr);
    req->settype(CHIRequestType_ReadNoSnp);
    req->setrequestor(m_machineID);
    req->setfwdRequestor(m_machineID);       // data comes back to us
    req->setdataToFwdRequestor(false);        // non-DMT
    req->setallowRetry(true);
    req->setDestination(allDownstreamDest());
    req->setusesTxnId(false);
    req->settxnId(txnId);
    req->setaccAddr(addr);
    req->setaccSize(cacheLineSize);
    sendRequestMsg(req);
}

// --- Helper: send WriteNoSnp to memory ---
void OurL2Middleware::sendWriteNoSnp(Addr addr, const uint8_t* data, TxnID txnId)
{
    auto req = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    req->setaddr(addr);
    req->settype(CHIRequestType_WriteNoSnp);
    req->setrequestor(m_machineID);
    req->setfwdRequestor(m_machineID);
    req->setdataToFwdRequestor(false);
    req->setallowRetry(true);
    req->setDestination(allDownstreamDest());
    req->setusesTxnId(false);
    req->settxnId(txnId);
    req->setaccAddr(addr);
    req->setaccSize(cacheLineSize);

    // WriteNoSnp also needs data - send via separate data message
    sendRequestMsg(req);

    // Send write data on data channel
    auto dat = std::make_shared<CHIDataMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    dat->setaddr(addr);
    dat->settype(CHIDataType_NCBWrDataCompAck);
    dat->setresponder(m_machineID);
    dat->setDestination(allDownstreamDest());
    dat->m_dataBlk.setData(data, 0, cacheLineSize);
    WriteMask bitmask(cacheLineSize);
    bitmask.fillMask();
    dat->setbitMask(bitmask);
    dat->setusesTxnId(false);
    dat->settxnId(txnId);
    sendDataMsg(dat);
}

// --- Helper: send SnpCleanInvalid to RN-F ---
void OurL2Middleware::sendSnpCleanInvalid(
    Addr addr, const MachineID& target, TxnID txnId)
{
    auto snp = std::make_shared<CHIRequestMsg>(
        curTick(), cacheLineSize, m_ruby_system);
    snp->setaddr(addr);
    snp->settype(CHIRequestType_SnpCleanInvalid);
    snp->setrequestor(m_machineID);
    NetDest dest(m_ruby_system);
    dest.add(target);
    snp->setDestination(dest);
    snp->setallowRetry(false);
    snp->setusesTxnId(false);
    snp->settxnId(txnId);
    snp->setretToSrc(false);
    snp->setaccAddr(addr);
    snp->setaccSize(cacheLineSize);
    sendSnoopMsg(snp);
}

// --- recvRequestMsg: main entry point ---
bool OurL2Middleware::recvRequestMsg(const CHIRequestMsg *msg)
{
    requestCount++;
    CHIRequestType inType = msg->gettype();
    Addr addr = msg->getaddr();
    chi::Opcode opcode = gem5ToOpcode(inType);

    cprintf("%s: [OurL2] Req #%d %s addr=%#x req=%s\n",
            name(), requestCount,
            getChIRequestTypeName(inType).c_str(), addr,
            msg->getrequestor());

    // Evict: just consume
    if (inType == CHIRequestType_Evict) {
        l2Cache->invalidate(addr);
        return true;
    }

    // WriteBackFull: L1 sends dirty data back
    if (opcode == chi::Opcode::WriteBackFull) {
        // Send CompDBIDResp to tell L1 to send data
        sendComp(msg->getrequestor(), addr,
                 CHIResponseType_CompDBIDResp, msg->gettxnId());

        // Record pending for when data arrives
        PendingTxn txn;
        txn.origType = inType;
        txn.addr = addr;
        txn.requestor = msg->getrequestor();
        txn.origTxnId = msg->gettxnId();
        txn.state = PendingState::WaitMemData; // Actually wait for L1 data
        txn.pendingSnoopCount = 0;
        txn.needsWriteback = false;
        txn.targetState = chi::LineState::I;
        pendingTxns[msg->gettxnId()] = txn;
        return true;
    }

    // CleanUnique: may need snoop + writeback
    if (opcode == chi::Opcode::CleanUnique) {
        auto lookupResp = l2Cache->lookup(addr);

        if (lookupResp.result == chi::LookupResult::Hit) {
            chi::LineState curState = lookupResp.state;

            if (curState == chi::LineState::UC) {
                // Already unique clean, just return Comp
                l2Cache->setState(addr, chi::LineState::UC);
                sendComp(msg->getrequestor(), addr,
                         CHIResponseType_Comp_UC, msg->gettxnId());
                return true;
            }

            if (curState == chi::LineState::UD) {
                // Need writeback, then Comp
                l2Cache->setState(addr, chi::LineState::UC);
                sendComp(msg->getrequestor(), addr,
                         CHIResponseType_Comp_UC, msg->gettxnId());
                return true;
            }

            if (curState == chi::LineState::SC || curState == chi::LineState::SD) {
                // Need to snoop sharers
                const auto& sharers = l2Cache->getSharers(addr);
                if (sharers.empty()) {
                    l2Cache->setState(addr, chi::LineState::UC);
                    sendComp(msg->getrequestor(), addr,
                             CHIResponseType_Comp_UC, msg->gettxnId());
                    return true;
                }

                // Send SnpCleanInvalid to all sharers
                PendingTxn txn;
                txn.origType = inType;
                txn.addr = addr;
                txn.requestor = msg->getrequestor();
                txn.origTxnId = msg->gettxnId();
                txn.state = PendingState::WaitSnoopResp;
                txn.pendingSnoopCount = sharers.size();
                txn.needsWriteback = (curState == chi::LineState::SD);
                txn.targetState = chi::LineState::UC;
                pendingTxns[msg->gettxnId()] = txn;

                for (NodeID sharer : sharers) {
                    TxnID snoopId = nextSnoopTxnId++;
                    snoopToOrigTxn[snoopId] = msg->gettxnId();
                    MachineID sharerMachine(MachineType_Cache, sharer);
                    sendSnpCleanInvalid(addr, sharerMachine, snoopId);
                }
                return true;
            }
        }

        // Miss: fill from memory, state UC
        PendingTxn txn;
        txn.origType = inType;
        txn.addr = addr;
        txn.requestor = msg->getrequestor();
        txn.origTxnId = msg->gettxnId();
        txn.state = PendingState::WaitMemData;
        txn.pendingSnoopCount = 0;
        txn.needsWriteback = false;
        txn.targetState = chi::LineState::UC;
        pendingTxns[msg->gettxnId()] = txn;
        sendReadNoSnp(addr, msg->gettxnId());
        return true;
    }

    // ReadShared: share data
    if (opcode == chi::Opcode::ReadShared) {
        auto lookupResp = l2Cache->lookup(addr);

        if (lookupResp.result == chi::LookupResult::Hit) {
            chi::LineState curState = lookupResp.state;

            // Add sharer
            l2Cache->addSharer(addr, msg->getrequestor().getNum());

            // Transition to shared if unique
            if (curState == chi::LineState::UC || curState == chi::LineState::UD) {
                l2Cache->setState(addr, curState == chi::LineState::UD
                                  ? chi::LineState::SD : chi::LineState::SC);
            }

            chi::LineState newState = l2Cache->getState(addr);
            sendCompData(msg->getrequestor(), addr,
                         lookupResp.data, newState, msg->gettxnId());
            return true;
        }

        // Miss: fetch from memory
        PendingTxn txn;
        txn.origType = inType;
        txn.addr = addr;
        txn.requestor = msg->getrequestor();
        txn.origTxnId = msg->gettxnId();
        txn.state = PendingState::WaitMemData;
        txn.pendingSnoopCount = 0;
        txn.needsWriteback = false;
        txn.targetState = chi::LineState::SC;
        pendingTxns[msg->gettxnId()] = txn;
        sendReadNoSnp(addr, msg->gettxnId());
        return true;
    }

    // ReadUnique: exclusive access
    if (opcode == chi::Opcode::ReadUnique) {
        auto lookupResp = l2Cache->lookup(addr);

        if (lookupResp.result == chi::LookupResult::Hit) {
            chi::LineState curState = lookupResp.state;

            if (curState == chi::LineState::UC || curState == chi::LineState::UD) {
                // Already unique, just return data
                l2Cache->setState(addr, chi::LineState::UD);
                sendCompData(msg->getrequestor(), addr,
                             lookupResp.data, chi::LineState::UD, msg->gettxnId());
                return true;
            }

            // SC or SD: need to snoop sharers
            const auto& sharers = l2Cache->getSharers(addr);
            if (sharers.empty()) {
                l2Cache->setState(addr, chi::LineState::UD);
                l2Cache->clearSharers(addr);
                sendCompData(msg->getrequestor(), addr,
                             lookupResp.data, chi::LineState::UD, msg->gettxnId());
                return true;
            }

            PendingTxn txn;
            txn.origType = inType;
            txn.addr = addr;
            txn.requestor = msg->getrequestor();
            txn.origTxnId = msg->gettxnId();
            txn.state = PendingState::WaitSnoopResp;
            txn.pendingSnoopCount = sharers.size();
            txn.needsWriteback = false;
            txn.targetState = chi::LineState::UD;
            pendingTxns[msg->gettxnId()] = txn;

            for (NodeID sharer : sharers) {
                TxnID snoopId = nextSnoopTxnId++;
                snoopToOrigTxn[snoopId] = msg->gettxnId();
                MachineID sharerMachine(MachineType_Cache, sharer);
                sendSnpCleanInvalid(addr, sharerMachine, snoopId);
            }
            return true;
        }

        // Miss: fetch from memory
        PendingTxn txn;
        txn.origType = inType;
        txn.addr = addr;
        txn.requestor = msg->getrequestor();
        txn.origTxnId = msg->gettxnId();
        txn.state = PendingState::WaitMemData;
        txn.pendingSnoopCount = 0;
        txn.needsWriteback = false;
        txn.targetState = chi::LineState::UD;
        pendingTxns[msg->gettxnId()] = txn;
        sendReadNoSnp(addr, msg->gettxnId());
        return true;
    }

    // Default: forward to memory
    sendReadNoSnp(addr, msg->gettxnId());
    return true;
}

// --- recvDataMsg: data from memory or L1 writeback ---
bool OurL2Middleware::recvDataMsg(const CHIDataMsg *msg)
{
    Addr addr = msg->getaddr();
    TxnID txnId = msg->gettxnId();

    cprintf("%s: [OurL2] Data %s addr=%#x txnId=%d\n",
            name(),
            CHIDataType_to_string(msg->gettype()).c_str(),
            addr, txnId);

    auto it = pendingTxns.find(txnId);
    if (it == pendingTxns.end()) {
        // Unsolicited data or writeback data from L1
        // Check if this is L1 writeback data (WriteBackFull flow)
        // For now, consume it
        return true;
    }

    PendingTxn& txn = it->second;

    if (txn.state == PendingState::WaitMemData) {
        const uint8_t* data = msg->getdataBlk().getData(0);

        if (txn.origType == CHIRequestType_WriteBackFull) {
            // WriteBackFull data from L1 — forward to memory and invalidate
            TxnID memTxnId = nextSnoopTxnId++;  // Reuse counter for unique ID
            PendingTxn memTxn;
            memTxn.origType = CHIRequestType_WriteNoSnp;
            memTxn.addr = addr;
            memTxn.requestor = txn.requestor;
            memTxn.origTxnId = memTxnId;
            memTxn.state = PendingState::WaitMemWriteAck;
            memTxn.pendingSnoopCount = 0;
            memTxn.needsWriteback = false;
            memTxn.targetState = chi::LineState::I;
            pendingTxns[memTxnId] = memTxn;

            sendWriteNoSnp(addr, data, memTxnId);
            l2Cache->invalidate(addr);
            pendingTxns.erase(it);
            return true;
        }

        // Data from memory — fill cache
        l2Cache->fill(addr, txn.targetState, data);

        // Add requester as sharer if shared state
        if (txn.targetState == chi::LineState::SC) {
            l2Cache->addSharer(addr, txn.requestor.getNum());
        }

        // Send CompData to original requester
        sendCompData(txn.requestor, addr,
                     l2Cache->getData(addr), txn.targetState, txn.origTxnId);

        pendingTxns.erase(it);
        return true;
    }

    return true;
}

// --- recvResponseMsg: response from memory ---
bool OurL2Middleware::recvResponseMsg(const CHIResponseMsg *msg)
{
    CHIResponseType type = msg->gettype();
    TxnID txnId = msg->gettxnId();

    cprintf("%s: [OurL2] Response %s addr=%#x txnId=%d\n",
            name(),
            CHIResponseType_to_string(type).c_str(),
            msg->getaddr(), txnId);

    // CompAck from L1: consume
    if (type == CHIResponseType_CompAck) {
        return true;
    }

    // WriteAck from memory: complete writeback
    if (type == CHIResponseType_WriteAck) {
        auto it = pendingTxns.find(txnId);
        if (it != pendingTxns.end()) {
            cprintf("%s: [OurL2] WriteAck for writeback addr=%#x\n",
                    name(), it->second.addr);
            pendingTxns.erase(it);
        }
        return true;
    }

    // Comp/Comp_UC from memory (response-only, data follows separately)
    auto it = pendingTxns.find(txnId);
    if (it != pendingTxns.end()) {
        // Response arrived before data — data will come in recvDataMsg
        return true;
    }

    return true;
}

// --- recvSnoopMsg: snoop from another HN-F (no-op, single HN-F) ---
bool OurL2Middleware::recvSnoopMsg(const CHIRequestMsg *msg)
{
    // In single-HN-F config, we never receive snoops
    return true;
}

} // namespace ruby
} // namespace gem5
```

Note: The snoop response handling is missing from this implementation. When an RN-F responds to SnpCleanInvalid, the response arrives via `recvResponseMsg`. We need to check if the txnId is a snoop response and decrement the pending snoop count. Let me add that logic.

Actually, looking at this more carefully, the snoop response from RN-F comes back as a `CHIResponseMsg` on the `rspIn` channel. The response type would be something like `Comp` or `Comp_UC`. We need to handle this in `recvResponseMsg`.

The implementation above needs the snoop response handling added to `recvResponseMsg`. This will be addressed in the next task.

- [ ] **Step 2: Build gem5 to verify compilation**

Run: `cd /home/zhangkai/work/CHI-new/gem5 && scons build/ARM/gem5.opt -j$(nproc)`
Expected: Build succeeds (may take several minutes)

- [ ] **Step 3: Commit**

```bash
git add gem5/src/mem/my_l2/our_l2_middleware.cc
git commit -m "feat(gem5): rewrite OurL2Middleware with sync L2 cache model"
```

---

### Task 6: Snoop Response Handling

**Files:**
- Modify: `gem5/src/mem/my_l2/our_l2_middleware.cc`

- [ ] **Step 1: Add snoop response handling to recvResponseMsg**

In `recvResponseMsg`, add handling for snoop responses before the generic response handler. Insert after the `CompAck` check:

```cpp
    // Check if this is a snoop response (txnId in snoopToOrigTxn)
    auto snoopIt = snoopToOrigTxn.find(txnId);
    if (snoopIt != snoopToOrigTxn.end()) {
        TxnID origTxnId = snoopIt->second;
        snoopToOrigTxn.erase(snoopIt);

        auto origIt = pendingTxns.find(origTxnId);
        if (origIt != pendingTxns.end()) {
            origIt->second.pendingSnoopCount--;
            if (origIt->second.pendingSnoopCount <= 0) {
                // All snoops done — complete the original request
                completePendingTxn(origTxnId);
            }
        }
        return true;
    }
```

- [ ] **Step 2: Implement completePendingTxn**

Add this method to `our_l2_middleware.cc`:

```cpp
void OurL2Middleware::completePendingTxn(TxnID txnId)
{
    auto it = pendingTxns.find(txnId);
    if (it == pendingTxns.end()) return;

    PendingTxn& txn = it->second;
    Addr addr = txn.addr;

    // Clear sharers and set target state
    l2Cache->clearSharers(addr);
    l2Cache->setState(addr, txn.targetState);

    // Send response based on original request type
    chi::Opcode opcode = gem5ToOpcode(txn.origType);

    if (opcode == chi::Opcode::ReadShared ||
        opcode == chi::Opcode::ReadUnique) {
        // Data request — send CompData
        sendCompData(txn.requestor, addr,
                     l2Cache->getData(addr), txn.targetState, txn.origTxnId);
    } else if (opcode == chi::Opcode::CleanUnique) {
        // No-data completion
        sendComp(txn.requestor, addr,
                 CHIResponseType_Comp_UC, txn.origTxnId);
    }

    pendingTxns.erase(it);
}
```

- [ ] **Step 3: Build and verify**

Run: `cd /home/zhangkai/work/CHI-new/gem5 && scons build/ARM/gem5.opt -j$(nproc)`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add gem5/src/mem/my_l2/our_l2_middleware.cc
git commit -m "feat(gem5): add snoop response handling and completePendingTxn"
```

---

### Task 7: gem5 2-Core Config

**Files:**
- Modify: `gem5/configs/example/arm/our_l2_hierarchy.py`

- [ ] **Step 1: Update L1 size and num_cores**

Change line 240-241:
```python
cache_hierarchy = OurL2CacheHierarchy(
    l1_size="32KiB",
    l1_assoc=8,
)
```

Change line 248-250:
```python
processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING,
    isa=ISA.ARM,
    num_cores=2,
)
```

- [ ] **Step 2: Update binary path for dual-core test**

Change the binary loading section (around line 260-268) to support dual-core:
```python
import os as _os
_script_dir = _os.path.dirname(_os.path.abspath(__file__))
_project_root = _os.path.dirname(_os.path.dirname(_os.path.dirname(_script_dir)))
binpath = _os.path.normpath(_os.path.join(_project_root, "..", "test", "test_dual_core_aarch64"))
from gem5.resources.resource import BinaryResource

board.set_se_binary_workload(
    binary=BinaryResource(binpath),
    # gem5 SE mode runs the same binary on all cores.
    # The test program uses shared memory atomic flags to coordinate.
)
```

Note: gem5 SE mode runs the same binary on all cores by default. The test programs need to use shared memory coordination where both cores run the same binary but take different code paths based on a shared flag. This is simpler than the two-binary approach.

- [ ] **Step 3: Verify NodeID assignment**

Run: `cd /home/zhangkai/work/CHI-new/gem5 && ./build/ARM/gem5.opt configs/example/arm/our_l2_hierarchy.py 2>&1 | head -50`
Expected: See "Middleware initialized with 256KB L2 cache" and request logs from both cores

- [ ] **Step 4: Commit**

```bash
git add gem5/configs/example/arm/our_l2_hierarchy.py
git commit -m "feat(gem5): 2-core config with 32KB L1 and dual-core test binary"
```

---

### Task 8: Dual-Core Test Program

**Files:**
- Create: `test/test_dual_core.cc`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Create test_dual_core.cc**

```cpp
#include <cstdint>
#include <atomic>
#include <unistd.h>

// Shared data between cores (volatile to prevent optimization)
static volatile uint64_t shared_data[1024 * 128] __attribute__((aligned(64)));

// Synchronization flags (atomic for cross-core visibility)
static std::atomic<uint64_t> sync_flags[16] = {};
static std::atomic<uint64_t> core_id_counter = {0};

// Raw syscall write for output (avoids libc printf which may hang gem5)
static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}

// Spin barrier: waits until both cores reach this point
static void barrier(int barrier_num) {
    sync_flags[barrier_num].fetch_add(1, std::memory_order_release);
    while (sync_flags[barrier_num].load(std::memory_order_acquire) < 2) {
        // spin
    }
}

// Get this core's ID (0 or 1)
// Each core calls this once at startup; the atomic counter ensures unique IDs.
static int get_core_id() {
    return core_id_counter.fetch_add(1, std::memory_order_relaxed);
}

// Test 1: Shared read — Core 0 writes, Core 1 reads
static bool test_shared_read(int my_id) {
    constexpr int NUM_LINES = 64;  // 64 cache lines = 4KB

    if (my_id == 0) {
        for (int i = 0; i < NUM_LINES; i++) {
            shared_data[i * 8] = static_cast<uint64_t>(i) * 0xAAAAAAAA;
        }
        barrier(0);
    } else {
        barrier(0);
        uint64_t checksum = 0;
        for (int i = 0; i < NUM_LINES; i++) {
            checksum += shared_data[i * 8];
        }
        uint64_t expected = 0;
        for (int i = 0; i < NUM_LINES; i++) {
            expected += static_cast<uint64_t>(i) * 0xAAAAAAAA;
        }
        if (checksum != expected) {
            raw_write("FAIL: shared_read checksum mismatch\n");
            return false;
        }
        raw_write("PASS: shared_read\n");
    }
    return true;
}

// Test 2: Write conflict — Core 0 writes, Core 1 overwrites, Core 0 reads
static bool test_write_conflict(int my_id) {
    if (my_id == 0) {
        shared_data[0] = 0xDEAD0000;
        barrier(1);
        barrier(2);
        uint64_t val = shared_data[0];
        if (val != 0xBEEF0000) {
            raw_write("FAIL: write_conflict expected 0xBEEF0000\n");
            return false;
        }
        raw_write("PASS: write_conflict\n");
    } else {
        barrier(1);
        shared_data[0] = 0xBEEF0000;
        barrier(2);
    }
    return true;
}

// Test 3: Ping-pong — alternating writes
static bool test_pingpong(int my_id) {
    constexpr int ROUNDS = 10;

    if (my_id == 0) {
        for (int i = 0; i < ROUNDS; i++) {
            shared_data[0] = static_cast<uint64_t>(i * 2);
            barrier(3 + i * 2);
            barrier(4 + i * 2);
            uint64_t val = shared_data[0];
            if (val != static_cast<uint64_t>(i * 2 + 1)) {
                raw_write("FAIL: pingpong\n");
                return false;
            }
        }
        raw_write("PASS: pingpong\n");
    } else {
        for (int i = 0; i < ROUNDS; i++) {
            barrier(3 + i * 2);
            uint64_t val = shared_data[0];
            if (val != static_cast<uint64_t>(i * 2)) {
                raw_write("FAIL: pingpong\n");
                return false;
            }
            shared_data[0] = static_cast<uint64_t>(i * 2 + 1);
            barrier(4 + i * 2);
        }
    }
    return true;
}

int main() {
    int my_id = get_core_id();

    bool ok = true;
    ok = test_shared_read(my_id) && ok;
    ok = test_write_conflict(my_id) && ok;
    ok = test_pingpong(my_id) && ok;

    if (my_id == 1) {
        if (ok) {
            raw_write("ALL TESTS PASSED\n");
        } else {
            raw_write("SOME TESTS FAILED\n");
        }
    }

    _exit(ok ? 0 : 1);
}
```

- [ ] **Step 2: Update test/CMakeLists.txt**

Add:
```cmake
add_executable(test_dual_core test_dual_core.cc)
target_link_libraries(test_dual_core PRIVATE chi_model)
```

- [ ] **Step 3: Cross-compile for aarch64**

Run: `cd /home/zhangkai/work/CHI-new/build-aarch64 && cmake .. -DCMAKE_TOOLCHAIN_FILE=../test/aarch64-toolchain.cmake && make test_dual_core`
Expected: Produces `test_dual_core_aarch64`

- [ ] **Step 4: Commit**

```bash
git add test/test_dual_core.cc test/CMakeLists.txt
git commit -m "feat(test): add dual-core coherence test program"
```

---

### Task 9: gem5 Integration Test

**Files:**
- None (test execution only)

- [ ] **Step 1: Build gem5 with new OurL2Middleware**

Run: `cd /home/zhangkai/work/CHI-new/gem5 && scons build/ARM/gem5.opt -j$(nproc)`
Expected: Build succeeds

- [ ] **Step 2: Run single-core regression test (1-core config)**

Temporarily set `num_cores=1` and use `test_128kb_aarch64`, then run:
```
./build/ARM/gem5.opt configs/example/arm/our_l2_hierarchy.py
```
Expected: Output includes "PASS" from test_128kb

- [ ] **Step 3: Run dual-core test**

Set `num_cores=2` and use `test_dual_core_aarch64`, then run:
```
./build/ARM/gem5.opt configs/example/arm/our_l2_hierarchy.py
```
Expected: Output includes "ALL TESTS PASSED"

- [ ] **Step 4: Verify gem5 stats**

Check `m5out/stats.txt` for:
- L2 cache hit/miss counts (should have both)
- SnpCleanInvalid counts (should be > 0 from write conflict test)

- [ ] **Step 5: Commit final state**

```bash
git add -A
git commit -m "feat: complete real L2 cache with CHI state model and 2-core support"
```
