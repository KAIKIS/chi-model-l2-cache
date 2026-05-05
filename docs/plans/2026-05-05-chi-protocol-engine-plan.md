# ChiProtocolEngine Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the project into a clean three-layer architecture: Middleware (gem5 message translation) → Cache = ChiProtocolEngine (protocol layer) + L2Cache (micro-architecture layer). Delete all dead code from Phase 1.

**Architecture:** ChiProtocolEngine is a synchronous protocol state machine with three entry points (recvRequest/recvData/recvResponse) each returning `vector<ProtocolAction>`. The Middleware becomes a thin executor that translates gem5 messages ↔ ChiTransaction and executes ProtocolActions as gem5 sends.

**Tech Stack:** C++17, CMake, gem5 (SCons)

**Design doc:** docs/design/2026-05-05-chi-protocol-engine-design.md

**Amendment to design doc:** ChiProtocolEngine lives in `cache_model/` (not `chi_model/`), because the Engine is part of the Cache and depends on L2Cache. `chi_model/` becomes header-only (type definitions only).

---

## File Responsibility Map

| File | Purpose | Depends on |
|------|---------|------------|
| `chi_model/include/chi_types.hh` | NodeID, TxnID, Addr, RespStatus type aliases | nothing |
| `chi_model/include/chi_opcode.hh` | Opcode enum (ReadShared, ReadUnique, ...) | nothing |
| `chi_model/include/chi_transaction.hh` | ChiTransaction data struct | chi_types, chi_opcode |
| `chi_model/include/chi_log.hh` | Logging macros (header-only) | nothing |
| `cache_model/include/cache_line.hh` | CacheLine, CacheSet, LineState, LRU | chi_types |
| `cache_model/include/l2_cache.hh` | L2Cache class (lookup/fill/evict/sharers) | cache_line |
| `cache_model/include/chi_protocol_engine.hh` | ChiProtocolEngine + ProtocolAction + PendingTxn | l2_cache, chi_transaction |
| `cache_model/src/l2_cache.cc` | L2Cache implementation | cache_line, chi_log |
| `cache_model/src/chi_protocol_engine.cc` | Protocol engine implementation (from middleware) | chi_protocol_engine, chi_log |
| `gem5/src/mem/my_l2/our_l2_middleware.hh/.cc` | Thin gem5 translator + action executor | chi_protocol_engine, chi_opcode |
| `test/test_protocol_engine.cc` | Independent Engine unit tests | chi_protocol_engine, cache_model |
| `test/test_l2_cache.cc` | L2Cache unit tests (unchanged) | cache_model |

---

### Task 1: Clean up chi_transaction (make chi_model header-only)

**Files:**
- Modify: `chi_model/include/chi_transaction.hh`
- Delete: `chi_model/src/chi_transaction.cc`

- [ ] **Step 1: Remove unused method declarations**

In `chi_model/include/chi_transaction.hh`, remove the three method declarations on lines 21-23:

```cpp
// REMOVE these three lines:
    bool isRequest() const;
    bool isResponse() const;
    bool needsSNForward() const;
```

The file should become:

```cpp
#pragma once

#include "chi_types.hh"
#include "chi_opcode.hh"

#include <cstdint>
#include <vector>

namespace chi {

struct ChiTransaction {
    TxnID       txnID       = 0;
    Opcode      opcode      = Opcode::ReadShared;
    Addr        addr        = 0;
    uint32_t    size        = 0;
    NodeID      srcNodeID   = 0;
    TxnID       returnTxnID = 0;
    std::vector<uint8_t> data;
    RespStatus  respStatus  = RespStatus::OK;
};

} // namespace chi
```

- [ ] **Step 2: Delete empty .cc file**

```bash
rm chi_model/src/chi_transaction.cc
```

- [ ] **Step 3: Commit**

```bash
git add chi_model/include/chi_transaction.hh
git rm chi_model/src/chi_transaction.cc
git commit -m "refactor: remove unused ChiTransaction methods, chi_model is now header-only"
```

---

### Task 2: Delete all dead code files

- [ ] **Step 1: Delete files**

```bash
rm chi_model/include/chi_channel.hh
rm chi_model/include/chi_node.hh
rm chi_model/include/chi_hn_node.hh
rm chi_model/src/chi_hn_node.cc
rm cache_model/include/simple_l2_cache.hh
rm cache_model/src/simple_l2_cache.cc
rm -rf middleware/
rm test/test_channel.cc
rm test/test_hn_node.cc
```

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "refactor: delete dead code (HnNode, Channel, SimpleL2Cache, middleware, dead tests)"
```

---

### Task 3: Update all CMakeLists.txt files

**Files:**
- Modify: `chi_model/CMakeLists.txt`
- Modify: `cache_model/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: chi_model/CMakeLists.txt — make header-only**

```cmake
add_library(chi_model INTERFACE)
target_include_directories(chi_model INTERFACE include)
```

- [ ] **Step 2: cache_model/CMakeLists.txt — remove simple_l2_cache**

```cmake
add_library(cache_model STATIC
    src/l2_cache.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
```

- [ ] **Step 3: Root CMakeLists.txt — remove middleware subdir**

```cmake
cmake_minimum_required(VERSION 3.14)
project(CHI-new LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(chi_model)
add_subdirectory(cache_model)
add_subdirectory(test)
```

- [ ] **Step 4: test/CMakeLists.txt — remove dead tests**

```cmake
add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)

add_executable(test_l2_cache test_l2_cache.cc)
target_link_libraries(test_l2_cache PRIVATE cache_model)

add_executable(test_dual_core test_dual_core.cc)
target_link_libraries(test_dual_core PRIVATE cache_model)
```

- [ ] **Step 5: Build and verify existing tests still pass**

```bash
cd build && cmake .. && cmake --build . -j$(nproc)
./test/test_l2_cache
```

Expected: CMake configures OK. test_l2_cache builds and passes all 13 tests.

- [ ] **Step 6: Commit**

```bash
git add chi_model/CMakeLists.txt cache_model/CMakeLists.txt CMakeLists.txt test/CMakeLists.txt
git commit -m "build: update CMakeLists for header-only chi_model, remove dead targets"
```

---

### Task 4: Create ChiProtocolEngine header

**File:**
- Create: `cache_model/include/chi_protocol_engine.hh`

- [ ] **Step 1: Write the complete header**

```cpp
#pragma once

#include "l2_cache.hh"
#include "chi_transaction.hh"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace chi {

// Instruction from Engine to Middleware.
// Middleware translates each ProtocolAction into gem5 message sends.
struct ProtocolAction {
    enum Type : uint8_t {
        SendReadNoSnp,        // Send ReadNoSnp to memory
        SendWriteNoSnp,       // Send WriteNoSnp + NCBWrData to memory
        SendCompData,         // Send CompData to RN-F (with data + state)
        SendComp,             // Send Comp to RN-F (no data)
        SendCompDBIDResp,     // Ask RN-F to send writeback data
        SendSnpCleanInvalid,  // Send SnpCleanInvalid to an RN-F
    };

    Type      type;
    Addr      addr        = 0;
    TxnID     txnId       = 0;        // txnId for the gem5 message
    NodeID    destNode    = 0;        // Target RN-F (for CompData/Comp/Snoop)
    LineState respState   = LineState::I;  // Cache line state for CompData
    bool      retToSrc    = false;    // Snoop: request data return from RN
    uint8_t   data[CACHE_LINE_SIZE] = {};  // Payload
};

// Internal pending transaction state
enum class PendingState : uint8_t {
    WaitMemData,       // Waiting for memory CompData
    WaitMemWriteAck,   // Waiting for memory WriteAck (after writeback)
    WaitSnoopResp,     // Waiting for SnpCleanInvalid responses
    WaitL1Data,        // Waiting for L1 data (after CompDBIDResp)
};

struct PendingTxn {
    ChiTransaction origReq;
    PendingState  state;
    LineState     targetState;
    int           pendingSnoopCount     = 0;
    int           pendingSnoopDataCount = 0;
};

class ChiProtocolEngine {
public:
    explicit ChiProtocolEngine(L2Cache* cache);

    // === Three entry points (synchronous) ===
    // Each returns actions the Middleware must execute.
    // Empty vector = transaction completed or waiting for external event.

    std::vector<ProtocolAction> recvRequest(const ChiTransaction& txn);
    std::vector<ProtocolAction> recvData(TxnID txnId, const uint8_t* data);
    std::vector<ProtocolAction> recvResponse(TxnID txnId, RespStatus status);

    // Direct cache line invalidation (for gem5 Evict — not a protocol txn)
    void invalidate(Addr addr) { cache_->invalidate(addr); }

private:
    L2Cache* cache_;

    // Pending transactions keyed by original L1 txnId
    std::unordered_map<TxnID, PendingTxn> pending_;

    // snoop/memory txnId → original L1 txnId
    std::unordered_map<TxnID, TxnID> snoopToOrig_;
    std::unordered_map<TxnID, TxnID> memToOrig_;

    TxnID nextSnoopTxnId_    = 10000;
    TxnID nextInternalTxnId_ = 20000;

    // Per-opcode request handlers
    std::vector<ProtocolAction> handleReadShared(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleReadUnique(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleCleanUnique(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleWriteBackFull(const ChiTransaction& txn);

    // Complete a pending transaction after all snoops/data received
    std::vector<ProtocolAction> completePending(TxnID origTxnId);

    // Utility: create a basic action with type/addr/txnId filled
    ProtocolAction makeAction(ProtocolAction::Type type, Addr addr, TxnID txnId);
};

} // namespace chi
```

- [ ] **Step 2: Commit**

```bash
git add cache_model/include/chi_protocol_engine.hh
git commit -m "feat: add ChiProtocolEngine header with ProtocolAction and PendingTxn types"
```

---

### Task 5: Create ChiProtocolEngine implementation

**File:**
- Create: `cache_model/src/chi_protocol_engine.cc`

This extracts all protocol logic from the existing `gem5/src/mem/my_l2/our_l2_middleware.cc`.

- [ ] **Step 1: Write implementation**

```cpp
#include "chi_protocol_engine.hh"
#include "chi_log.hh"

#include <cstring>

namespace chi {

ChiProtocolEngine::ChiProtocolEngine(L2Cache* cache)
    : cache_(cache) {}

ProtocolAction ChiProtocolEngine::makeAction(
    ProtocolAction::Type type, Addr addr, TxnID txnId)
{
    ProtocolAction a{};
    a.type  = type;
    a.addr  = addr;
    a.txnId = txnId;
    return a;
}

// ============================================================
// recvRequest — dispatch by opcode
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::recvRequest(
    const ChiTransaction& txn)
{
    switch (txn.opcode) {
        case Opcode::ReadShared:    return handleReadShared(txn);
        case Opcode::ReadUnique:    return handleReadUnique(txn);
        case Opcode::CleanUnique:   return handleCleanUnique(txn);
        case Opcode::WriteBackFull: return handleWriteBackFull(txn);
        default:
            // Unknown opcode — pass-through to memory
            {
                TxnID memId = nextInternalTxnId_++;
                memToOrig_[memId] = txn.txnID;
                PendingTxn pt;
                pt.origReq = txn;
                pt.state = PendingState::WaitMemData;
                pt.targetState = LineState::SC;
                pending_[txn.txnID] = pt;
                auto a = makeAction(ProtocolAction::SendReadNoSnp, txn.addr, memId);
                return {a};
            }
    }
}

// ============================================================
// handleReadShared
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::handleReadShared(
    const ChiTransaction& txn)
{
    Addr addr = txn.addr;
    auto resp = cache_->lookup(addr);

    if (resp.result == LookupResult::Hit) {
        LineState curState = resp.state;

        // Dirty states: snoop other sharers for latest data
        if (curState == LineState::UD || curState == LineState::SD) {
            const auto& allSharers = cache_->getSharers(addr);
            int reqNum = static_cast<int>(txn.srcNodeID);
            int snoopCount = 0;
            for (NodeID s : allSharers) { if (s != reqNum) snoopCount++; }

            if (snoopCount > 0) {
                PendingTxn pt;
                pt.origReq = txn;
                pt.state = PendingState::WaitSnoopResp;
                pt.targetState = LineState::SC;
                pt.pendingSnoopCount = snoopCount;
                pt.pendingSnoopDataCount = snoopCount;
                pending_[txn.txnID] = pt;

                std::vector<ProtocolAction> actions;
                for (NodeID s : allSharers) {
                    if (s == reqNum) continue;
                    TxnID snoopId = nextSnoopTxnId_++;
                    snoopToOrig_[snoopId] = txn.txnID;
                    auto a = makeAction(ProtocolAction::SendSnpCleanInvalid, addr, snoopId);
                    a.destNode = s;
                    a.retToSrc = true;
                    actions.push_back(a);
                }
                return actions;
            }
            // No other sharers — fall through to serve directly
        }

        // Serve directly
        cache_->addSharer(addr, reqNum);
        if (curState == LineState::UC || curState == LineState::UD) {
            cache_->setState(addr,
                curState == LineState::UD ? LineState::SD : LineState::SC);
        }

        auto a = makeAction(ProtocolAction::SendCompData, addr, txn.txnID);
        a.destNode = txn.srcNodeID;
        a.respState = cache_->getState(addr);
        std::memcpy(a.data, cache_->getData(addr), CACHE_LINE_SIZE);
        return {a};
    }

    // Miss — fetch from memory
    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::SC;
    pending_[txn.txnID] = pt;

    auto a = makeAction(ProtocolAction::SendReadNoSnp, addr, memId);
    return {a};
}

// ============================================================
// handleReadUnique
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::handleReadUnique(
    const ChiTransaction& txn)
{
    Addr addr = txn.addr;
    auto resp = cache_->lookup(addr);

    if (resp.result == LookupResult::Hit) {
        const auto& allSharers = cache_->getSharers(addr);
        int reqNum = static_cast<int>(txn.srcNodeID);
        int snoopCount = 0;
        for (NodeID s : allSharers) { if (s != reqNum) snoopCount++; }

        if (snoopCount == 0) {
            cache_->setState(addr, LineState::UD);
            cache_->clearSharers(addr);
            cache_->addSharer(addr, reqNum);

            auto a = makeAction(ProtocolAction::SendCompData, addr, txn.txnID);
            a.destNode = txn.srcNodeID;
            a.respState = LineState::UD;
            std::memcpy(a.data, cache_->getData(addr), CACHE_LINE_SIZE);
            return {a};
        }

        bool needData = (resp.state == LineState::UD || resp.state == LineState::SD);

        PendingTxn pt;
        pt.origReq = txn;
        pt.state = PendingState::WaitSnoopResp;
        pt.targetState = LineState::UD;
        pt.pendingSnoopCount = snoopCount;
        pt.pendingSnoopDataCount = needData ? snoopCount : 0;
        pending_[txn.txnID] = pt;

        std::vector<ProtocolAction> actions;
        for (NodeID s : allSharers) {
            if (s == reqNum) continue;
            TxnID snoopId = nextSnoopTxnId_++;
            snoopToOrig_[snoopId] = txn.txnID;
            auto a = makeAction(ProtocolAction::SendSnpCleanInvalid, addr, snoopId);
            a.destNode = s;
            a.retToSrc = needData;
            actions.push_back(a);
        }
        return actions;
    }

    // Miss
    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::UD;
    pending_[txn.txnID] = pt;

    auto a = makeAction(ProtocolAction::SendReadNoSnp, addr, memId);
    return {a};
}

// ============================================================
// handleCleanUnique
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::handleCleanUnique(
    const ChiTransaction& txn)
{
    Addr addr = txn.addr;
    auto resp = cache_->lookup(addr);

    if (resp.result == LookupResult::Hit) {
        LineState curState = resp.state;

        if (curState == LineState::UC) {
            auto a = makeAction(ProtocolAction::SendComp, addr, txn.txnID);
            a.destNode = txn.srcNodeID;
            return {a};
        }

        if (curState == LineState::UD) {
            TxnID memId = nextInternalTxnId_++;
            memToOrig_[memId] = txn.txnID;

            PendingTxn pt;
            pt.origReq = txn;
            pt.state = PendingState::WaitMemWriteAck;
            pt.targetState = LineState::UC;
            pending_[txn.txnID] = pt;

            cache_->setState(addr, LineState::UC);

            auto wb = makeAction(ProtocolAction::SendWriteNoSnp, addr, memId);
            std::memcpy(wb.data, cache_->getData(addr), CACHE_LINE_SIZE);

            auto comp = makeAction(ProtocolAction::SendComp, addr, txn.txnID);
            comp.destNode = txn.srcNodeID;

            return {wb, comp};
        }

        // SC or SD — snoop other sharers
        const auto& allSharers = cache_->getSharers(addr);
        int reqNum = static_cast<int>(txn.srcNodeID);
        int snoopCount = 0;
        for (NodeID s : allSharers) { if (s != reqNum) snoopCount++; }

        if (snoopCount == 0) {
            cache_->clearSharers(addr);
            cache_->setState(addr, LineState::UC);
            auto a = makeAction(ProtocolAction::SendComp, addr, txn.txnID);
            a.destNode = txn.srcNodeID;
            return {a};
        }

        bool needsWB = (curState == LineState::SD);
        PendingTxn pt;
        pt.origReq = txn;
        pt.state = PendingState::WaitSnoopResp;
        pt.targetState = LineState::UC;
        pt.pendingSnoopCount = snoopCount;
        pt.pendingSnoopDataCount = needsWB ? snoopCount : 0;
        pending_[txn.txnID] = pt;

        std::vector<ProtocolAction> actions;
        for (NodeID s : allSharers) {
            if (s == reqNum) continue;
            TxnID snoopId = nextSnoopTxnId_++;
            snoopToOrig_[snoopId] = txn.txnID;
            auto a = makeAction(ProtocolAction::SendSnpCleanInvalid, addr, snoopId);
            a.destNode = s;
            a.retToSrc = needsWB;
            actions.push_back(a);
        }
        return actions;
    }

    // Miss
    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::UC;
    pending_[txn.txnID] = pt;

    auto a = makeAction(ProtocolAction::SendReadNoSnp, addr, memId);
    return {a};
}

// ============================================================
// handleWriteBackFull
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::handleWriteBackFull(
    const ChiTransaction& txn)
{
    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitL1Data;
    pending_[txn.txnID] = pt;

    auto a = makeAction(ProtocolAction::SendCompDBIDResp, txn.addr, txn.txnID);
    a.destNode = txn.srcNodeID;
    return {a};
}

// ============================================================
// recvData — data from memory, L1 writeback, or snoop writeback
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::recvData(
    TxnID txnId, const uint8_t* data)
{
    // Snoop writeback data?
    {
        auto it = snoopToOrig_.find(txnId);
        if (it != snoopToOrig_.end()) {
            TxnID origId = it->second;
            auto pit = pending_.find(origId);
            if (pit != pending_.end()) {
                cache_->fill(pit->second.origReq.addr, pit->second.targetState, data);
                pit->second.pendingSnoopDataCount--;
                if (pit->second.pendingSnoopCount <= 0 &&
                    pit->second.pendingSnoopDataCount <= 0) {
                    return completePending(origId);
                }
            }
            return {};
        }
    }

    // L1 writeback data (WriteBackFull)?
    {
        auto it = pending_.find(txnId);
        if (it != pending_.end() && it->second.state == PendingState::WaitL1Data) {
            Addr addr = it->second.origReq.addr;
            TxnID memId = nextInternalTxnId_++;
            memToOrig_[memId] = txnId;

            PendingTxn memPt;
            memPt.origReq = it->second.origReq;
            memPt.state = PendingState::WaitMemWriteAck;
            pending_[memId] = memPt;
            pending_.erase(it);

            cache_->invalidate(addr);

            auto a = makeAction(ProtocolAction::SendWriteNoSnp, addr, memId);
            std::memcpy(a.data, data, CACHE_LINE_SIZE);
            return {a};
        }
    }

    // Memory data
    {
        auto it = pending_.find(txnId);
        if (it == pending_.end()) return {};  // Unsolicited or already processed

        PendingTxn& pt = it->second;
        if (pt.state != PendingState::WaitMemData) return {};

        Addr addr = pt.origReq.addr;
        cache_->fill(addr, pt.targetState, data);
        cache_->addSharer(addr, pt.origReq.srcNodeID);

        std::vector<ProtocolAction> actions;

        if (pt.origReq.opcode == Opcode::CleanUnique) {
            auto a = makeAction(ProtocolAction::SendComp, addr, pt.origReq.txnID);
            a.destNode = pt.origReq.srcNodeID;
            actions.push_back(a);
        } else {
            auto a = makeAction(ProtocolAction::SendCompData, addr, pt.origReq.txnID);
            a.destNode = pt.origReq.srcNodeID;
            a.respState = cache_->getState(addr);
            std::memcpy(a.data, cache_->getData(addr), CACHE_LINE_SIZE);
            actions.push_back(a);
        }

        TxnID origTxnId = pt.origReq.txnID;
        pending_.erase(it);
        // Clean up memToOrig_ entries pointing to this txn
        for (auto mi = memToOrig_.begin(); mi != memToOrig_.end(); ) {
            if (mi->second == origTxnId) mi = memToOrig_.erase(mi);
            else ++mi;
        }
        return actions;
    }
}

// ============================================================
// recvResponse — snoop response, CompAck, WriteAck, Comp
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::recvResponse(
    TxnID txnId, RespStatus /*status*/)
{
    // Snoop response?
    {
        auto it = snoopToOrig_.find(txnId);
        if (it != snoopToOrig_.end()) {
            TxnID origId = it->second;
            auto pit = pending_.find(origId);
            if (pit != pending_.end()) {
                pit->second.pendingSnoopCount--;
                if (pit->second.pendingSnoopCount <= 0 &&
                    pit->second.pendingSnoopDataCount <= 0) {
                    // Clean up all snoopToOrig_ entries for this origin txn
                    for (auto si = snoopToOrig_.begin(); si != snoopToOrig_.end(); ) {
                        if (si->second == origId) si = snoopToOrig_.erase(si);
                        else ++si;
                    }
                    return completePending(origId);
                }
            }
            return {};
        }
    }

    // Memory WriteAck or CompAck — just erase pending
    {
        auto it = pending_.find(txnId);
        if (it != pending_.end()) {
            pending_.erase(it);
            for (auto mi = memToOrig_.begin(); mi != memToOrig_.end(); ) {
                if (mi->second == txnId || mi->first == txnId)
                    mi = memToOrig_.erase(mi);
                else ++mi;
            }
        }
    }

    return {};
}

// ============================================================
// completePending — all snoops received, finish the transaction
// ============================================================

std::vector<ProtocolAction> ChiProtocolEngine::completePending(TxnID origTxnId)
{
    auto it = pending_.find(origTxnId);
    if (it == pending_.end()) return {};

    PendingTxn& pt = it->second;
    Addr addr = pt.origReq.addr;

    cache_->clearSharers(addr);
    cache_->setState(addr, pt.targetState);

    std::vector<ProtocolAction> actions;

    if (pt.origReq.opcode == Opcode::CleanUnique) {
        auto a = makeAction(ProtocolAction::SendComp, addr, pt.origReq.txnID);
        a.destNode = pt.origReq.srcNodeID;
        actions.push_back(a);
    } else {
        cache_->addSharer(addr, pt.origReq.srcNodeID);
        auto a = makeAction(ProtocolAction::SendCompData, addr, pt.origReq.txnID);
        a.destNode = pt.origReq.srcNodeID;
        a.respState = cache_->getState(addr);
        std::memcpy(a.data, cache_->getData(addr), CACHE_LINE_SIZE);
        actions.push_back(a);
    }

    pending_.erase(it);
    return actions;
}

} // namespace chi
```

- [ ] **Step 2: Update cache_model/CMakeLists.txt to add engine source**

```cmake
add_library(cache_model STATIC
    src/l2_cache.cc
    src/chi_protocol_engine.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
```

- [ ] **Step 3: Build cache_model**

```bash
cd build && cmake .. && cmake --build . -j$(nproc)
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add cache_model/src/chi_protocol_engine.cc cache_model/CMakeLists.txt
git commit -m "feat: add ChiProtocolEngine implementation — extract protocol logic"
```

---

### Task 6: Update gem5 OurL2Middleware

**Files:**
- Modify: `gem5/src/mem/my_l2/our_l2_middleware.hh`
- Modify: `gem5/src/mem/my_l2/our_l2_middleware.cc`

- [ ] **Step 1: Rewrite header — use Engine instead of direct L2Cache**

```cpp
#ifndef __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
#define __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__

#include <memory>
#include <vector>

#include "mem/ruby/protocol/chi/generic/CHIGenericController.hh"
#include "params/OurL2Middleware.hh"

#include "chi_protocol_engine.hh"
#include "chi_opcode.hh"

namespace gem5
{
namespace ruby
{

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
    chi::L2Cache                        cacheStorage_;
    std::unique_ptr<chi::ChiProtocolEngine> engine_;

    // Translate gem5 message types to our types
    chi::Opcode     gem5ToOpcode(CHI::CHIRequestType type);

    // Execute ProtocolAction list — translate each action to gem5 sends
    void executeActions(const std::vector<chi::ProtocolAction>& actions);

    // Send helpers (thin wrappers — no protocol logic)
    void sendReadNoSnp(Addr addr, chi::TxnID txnId);
    void sendWriteNoSnp(Addr addr, const uint8_t* data, chi::TxnID txnId);
    void sendComp(const MachineID& dest, Addr addr,
                  CHI::CHIResponseType compType, chi::TxnID txnId);
    void sendCompData(const MachineID& dest, Addr addr,
                      const uint8_t* data, chi::LineState state,
                      chi::TxnID txnId);
    void sendCompDBIDResp(const MachineID& dest, Addr addr, chi::TxnID txnId);
    void sendSnpCleanInvalid(Addr addr, const MachineID& target,
                             chi::TxnID txnId, bool retToSrc);

    uint64_t requestCount;
};

} // namespace ruby
} // namespace gem5

#endif // __MEM_MY_L2_OUR_L2_MIDDLEWARE_HH__
```

- [ ] **Step 2: Rewrite implementation — constructor**

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
    engine_ = std::make_unique<chi::ChiProtocolEngine>(&cacheStorage_);
    std::cout << "[OurL2] Middleware initialized with ChiProtocolEngine"
              << " machineID=" << m_machineID << std::endl;
}

OurL2Middleware::~OurL2Middleware() {}
```

- [ ] **Step 3: opcode mapping (unchanged from current)**

```cpp
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
        return chi::Opcode::WriteBackFull;
      default:
        panic("[OurL2] Unknown request type: %s",
              CHIRequestType_to_string(type));
    }
}
```

- [ ] **Step 4: recvRequestMsg — delegate to Engine**

```cpp
bool OurL2Middleware::recvRequestMsg(const CHIRequestMsg *msg)
{
    requestCount++;
    CHIRequestType inType = msg->gettype();
    Addr addr = msg->getaddr();

    cprintf("%s: [OurL2] Req #%d %s addr=%#x req=%s txnId=%d\n",
            name(), requestCount,
            CHIRequestType_to_string(inType).c_str(), addr,
            msg->getrequestor(), msg->gettxnId());

    // Evict: gem5-specific, not a CHI protocol transaction
    if (inType == CHIRequestType_Evict) {
        engine_->invalidate(addr);
        return true;
    }

    chi::ChiTransaction txn;
    txn.opcode    = gem5ToOpcode(inType);
    txn.addr      = addr;
    txn.size      = msg->getaccSize();
    txn.srcNodeID = static_cast<chi::NodeID>(msg->getrequestor().getNum());
    txn.txnID     = msg->gettxnId();

    auto actions = engine_->recvRequest(txn);
    executeActions(actions);
    return true;
}
```

- [ ] **Step 5: recvDataMsg — delegate to Engine**

Each data message beat carries the full cache line (gem5 DataBlock is always 64 bytes).
No beat assembly needed — deliver directly to Engine.

```cpp
bool OurL2Middleware::recvDataMsg(const CHIDataMsg *msg)
{
    Addr addr = msg->getaddr();
    chi::TxnID txnId = msg->gettxnId();

    cprintf("%s: [OurL2] Data %s addr=%#x txnId=%d\n",
            name(), CHIDataType_to_string(msg->gettype()).c_str(),
            addr, txnId);

    const uint8_t* data = msg->getdataBlk().getData(0, cacheLineSize);
    auto actions = engine_->recvData(txnId, data);
    executeActions(actions);
    return true;
}
```

- [ ] **Step 6: recvResponseMsg — delegate to Engine**

```cpp
bool OurL2Middleware::recvResponseMsg(const CHIResponseMsg *msg)
{
    CHIResponseType type = msg->gettype();
    chi::TxnID txnId = msg->gettxnId();

    cprintf("%s: [OurL2] Resp %s addr=%#x txnId=%d\n",
            name(), CHIResponseType_to_string(type).c_str(),
            msg->getaddr(), txnId);

    // Map gem5 response to RespStatus (simplified for now)
    chi::RespStatus status = chi::RespStatus::OK;

    auto actions = engine_->recvResponse(txnId, status);
    executeActions(actions);
    return true;
}
```

- [ ] **Step 7: executeActions — translate ProtocolAction → gem5 sends**

```cpp
void OurL2Middleware::executeActions(
    const std::vector<chi::ProtocolAction>& actions)
{
    for (const auto& a : actions) {
        switch (a.type) {
          case chi::ProtocolAction::SendReadNoSnp:
            sendReadNoSnp(a.addr, a.txnId);
            break;
          case chi::ProtocolAction::SendWriteNoSnp:
            sendWriteNoSnp(a.addr, a.data, a.txnId);
            break;
          case chi::ProtocolAction::SendCompData: {
            MachineID dest(MachineType_Cache, a.destNode);
            sendCompData(dest, a.addr, a.data, a.respState, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendComp: {
            MachineID dest(MachineType_Cache, a.destNode);
            sendComp(dest, a.addr, CHIResponseType_Comp, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendCompDBIDResp: {
            MachineID dest(MachineType_Cache, a.destNode);
            sendCompDBIDResp(dest, a.addr, a.txnId);
            break;
          }
          case chi::ProtocolAction::SendSnpCleanInvalid: {
            MachineID target(MachineType_Cache, a.destNode);
            sendSnpCleanInvalid(a.addr, target, a.txnId, a.retToSrc);
            break;
          }
        }
    }
}
```

- [ ] **Step 8: Send helpers (moved from current middleware, unchanged logic)**

The sendReadNoSnp, sendWriteNoSnp, sendCompData, sendComp, sendCompDBIDResp, and sendSnpCleanInvalid methods are moved from the current middleware without changes to their internal logic. These are thin wrappers that construct gem5 message objects and call the parent's sendRequestMsg/sendResponseMsg/sendDataMsg/sendSnoopMsg.

(These methods are identical to the existing ones in the current our_l2_middleware.cc lines 63-195. Copy them as-is.)

- [ ] **Step 9: recvSnoopMsg — unchanged (no-op in single-HN-F config)**

```cpp
bool OurL2Middleware::recvSnoopMsg(const CHIRequestMsg *msg)
{
    return true;  // No external snoops in single-HN-F config
}
```

- [ ] **Step 10: Commit**

```bash
git add gem5/src/mem/my_l2/our_l2_middleware.hh gem5/src/mem/my_l2/our_l2_middleware.cc
git commit -m "refactor: simplify OurL2Middleware — delegate all protocol decisions to ChiProtocolEngine"
```

---

### Task 7: Create test_protocol_engine.cc

**File:**
- Create: `test/test_protocol_engine.cc`

- [ ] **Step 1: Write the test file with these test cases**

```cpp
#include "chi_protocol_engine.hh"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace chi;

// Helper: create a request txn
static ChiTransaction makeReq(Opcode op, Addr addr, NodeID src, TxnID id) {
    ChiTransaction t;
    t.opcode = op;
    t.addr = addr;
    t.size = 64;
    t.srcNodeID = src;
    t.txnID = id;
    return t;
}

// Helper: create test data filled with a pattern
static void fillPattern(uint8_t* data, uint8_t pattern) {
    for (int i = 0; i < 64; i++) data[i] = pattern;
}

// --- Test Cases ---

static void testReadSharedMiss() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendReadNoSnp);
    assert(actions[0].addr == addr);
    std::cout << "  PASS: ReadShared miss → SendReadNoSnp\n";
}

static void testReadSharedHitSC() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    fillPattern(data, 0xAB);
    cache.fill(addr, LineState::SC, data);

    auto actions = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendCompData);
    assert(actions[0].respState == LineState::SC);
    assert(actions[0].data[0] == 0xAB);
    std::cout << "  PASS: ReadShared SC hit → SendCompData\n";
}

static void testReadSharedMissThenData() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    // Step 1: request → miss
    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    assert(actions1.size() == 1);
    assert(actions1[0].type == ProtocolAction::SendReadNoSnp);
    TxnID memId = actions1[0].txnId;

    // Step 2: memory data arrives
    uint8_t data[64];
    fillPattern(data, 0xCD);
    auto actions2 = engine.recvData(memId, data);
    assert(actions2.size() == 1);
    assert(actions2[0].type == ProtocolAction::SendCompData);
    assert(actions2[0].respState == LineState::SC);
    // Cache should now have the data
    assert(cache.getState(addr) == LineState::SC);
    std::cout << "  PASS: ReadShared miss → recvData → SendCompData\n";
}

static void testReadUniqueHitSoleSharer() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    fillPattern(data, 0xEF);
    cache.fill(addr, LineState::UC, data);
    cache.addSharer(addr, 0);

    auto actions = engine.recvRequest(makeReq(Opcode::ReadUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendCompData);
    assert(actions[0].respState == LineState::UD);
    assert(cache.getState(addr) == LineState::UD);
    std::cout << "  PASS: ReadUnique sole sharer → SendCompData UD\n";
}

static void testReadUniqueHitWithOtherSharers() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::SC, data);
    cache.addSharer(addr, 0);  // Requestor
    cache.addSharer(addr, 1);  // Other sharer

    auto actions = engine.recvRequest(makeReq(Opcode::ReadUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendSnpCleanInvalid);
    assert(actions[0].destNode == 1);
    assert(actions[0].retToSrc == false);  // SC state, L2 has data
    std::cout << "  PASS: ReadUnique with other sharer → SendSnpCleanInvalid\n";
}

static void testReadUniqueSnoopThenComplete() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::SC, data);
    cache.addSharer(addr, 0);
    cache.addSharer(addr, 1);
    cache.addSharer(addr, 2);

    // Step 1: ReadUnique → snoop sharers 1 and 2
    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadUnique, addr, 0, 1));
    assert(actions1.size() == 2);
    assert(actions1[0].type == ProtocolAction::SendSnpCleanInvalid);
    assert(actions1[1].type == ProtocolAction::SendSnpCleanInvalid);

    // Step 2: snoop responses arrive
    auto actions2 = engine.recvResponse(actions1[0].txnId, RespStatus::OK);
    assert(actions2.empty());  // Still waiting for one more snoop

    auto actions3 = engine.recvResponse(actions1[1].txnId, RespStatus::OK);
    assert(actions3.size() == 1);
    assert(actions3[0].type == ProtocolAction::SendCompData);
    assert(actions3[0].respState == LineState::UD);
    std::cout << "  PASS: ReadUnique snoop complete → SendCompData UD\n";
}

static void testCleanUniqueUCHit() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::UC, data);
    cache.addSharer(addr, 0);

    auto actions = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendComp);
    std::cout << "  PASS: CleanUnique UC hit → SendComp\n";
}

static void testCleanUniqueMiss() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendReadNoSnp);
    std::cout << "  PASS: CleanUnique miss → SendReadNoSnp\n";
}

static void testCleanUniqueMissThenData() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions1 = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions1[0].type == ProtocolAction::SendReadNoSnp);
    TxnID memId = actions1[0].txnId;

    uint8_t data[64];
    fillPattern(data, 0x55);
    auto actions2 = engine.recvData(memId, data);
    assert(actions2.size() == 1);
    assert(actions2[0].type == ProtocolAction::SendComp);  // No data for CleanUnique
    assert(cache.getState(addr) == LineState::UC);
    std::cout << "  PASS: CleanUnique miss → recvData → SendComp\n";
}

static void testWriteBackFull() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::WriteBackFull, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendCompDBIDResp);
    assert(actions[0].destNode == 0);
    std::cout << "  PASS: WriteBackFull → SendCompDBIDResp\n";
}

static void testEvict() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::UD, data);
    assert(cache.getState(addr) == LineState::UD);

    engine.invalidate(addr);
    assert(cache.getState(addr) == LineState::I);
    std::cout << "  PASS: Evict → invalidate\n";
}

static void testDuplicateDataIgnored() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    TxnID memId = actions1[0].txnId;

    uint8_t data[64];
    fillPattern(data, 0x42);
    engine.recvData(memId, data);

    // Second data message with same txnId should be ignored
    uint8_t data2[64];
    fillPattern(data2, 0xFF);
    auto actions3 = engine.recvData(memId, data2);
    assert(actions3.empty());  // Already completed, ignored
    std::cout << "  PASS: Duplicate data → ignored\n";
}

int main() {
    std::cout << "ChiProtocolEngine tests:\n";
    testReadSharedMiss();
    testReadSharedHitSC();
    testReadSharedMissThenData();
    testReadUniqueHitSoleSharer();
    testReadUniqueHitWithOtherSharers();
    testReadUniqueSnoopThenComplete();
    testCleanUniqueUCHit();
    testCleanUniqueMiss();
    testCleanUniqueMissThenData();
    testWriteBackFull();
    testEvict();
    testDuplicateDataIgnored();
    std::cout << "All ChiProtocolEngine tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Update test/CMakeLists.txt to add the new test**

```cmake
add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)

add_executable(test_l2_cache test_l2_cache.cc)
target_link_libraries(test_l2_cache PRIVATE cache_model)

add_executable(test_protocol_engine test_protocol_engine.cc)
target_link_libraries(test_protocol_engine PRIVATE cache_model)

add_executable(test_dual_core test_dual_core.cc)
target_link_libraries(test_dual_core PRIVATE cache_model)
```

- [ ] **Step 3: Build and run the test**

```bash
cd build && cmake .. && cmake --build . -j$(nproc)
./test/test_protocol_engine
```

Expected: All 12 tests pass.

- [ ] **Step 4: Commit**

```bash
git add test/test_protocol_engine.cc test/CMakeLists.txt
git commit -m "test: add ChiProtocolEngine unit tests (12 cases)"
```

---

### Task 8: Final verification

- [ ] **Step 1: Run all existing tests**

```bash
cd build && cmake --build . -j$(nproc)
./test/test_l2_cache
./test/test_protocol_engine
```

Expected: test_l2_cache passes 13 tests. test_protocol_engine passes 12 tests.

- [ ] **Step 2: Verify gem5 builds (incremental)**

```bash
cd ../gem5 && scons build/ARM/gem5.opt -j$(nproc) 2>&1 | tail -20
```

Expected: gem5 builds successfully. The OurL2Middleware should compile against the new ChiProtocolEngine.

- [ ] **Step 3: Commit any remaining changes**

```bash
git status
git add -A
git diff --staged
git commit -m "chore: final verification — all tests pass, gem5 builds"
```
