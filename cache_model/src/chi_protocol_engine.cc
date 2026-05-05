#include "chi_protocol_engine.hh"
#include "chi_log.hh"

#include <cassert>
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
        case Opcode::ReadShared:    readSharedReqs_++;  return handleReadShared(txn);
        case Opcode::ReadUnique:    readUniqueReqs_++;  return handleReadUnique(txn);
        case Opcode::CleanUnique:   cleanUniqueReqs_++; return handleCleanUnique(txn);
        case Opcode::WriteBackFull: writeBackReqs_++;   return handleWriteBackFull(txn);
        default:
            // Unknown opcode — pass-through to memory
            {
                TxnID memId = nextInternalTxnId_++;
                memToOrig_[memId] = txn.txnID;
                PendingTxn pt;
                pt.origReq = txn;
                pt.state = PendingState::WaitMemData;
                pt.targetState = LineState::SC;
                pending_[memId] = pt;
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
    if (resp.result == LookupResult::Hit) hitCount_++; else missCount_++;

    if (resp.result == LookupResult::Hit) {
        LineState curState = resp.state;
        int reqNum = static_cast<int>(txn.srcNodeID);

        // Dirty states: snoop other sharers for latest data
        if (curState == LineState::UD || curState == LineState::SD) {
            const auto& allSharers = cache_->getSharers(addr);
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

    // Miss — fetch from memory.
    // TODO: MissEvictDirty is not yet handled. When the LRU victim is a dirty
    // line, the dirty data would be silently lost by fill(). For workloads
    // larger than the L2 cache with dirty evictions, implement writeback-on-evict:
    //   1. Send WriteNoSnp(evictAddr, evictData) before ReadNoSnp
    //   2. Wait for WriteAck from memory
    //   3. Then fill() and proceed with ReadNoSnp
    assert(resp.result != LookupResult::MissEvictDirty
           && "MissEvictDirty: dirty eviction not yet implemented");
    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::SC;
    pending_[memId] = pt;

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
    if (resp.result == LookupResult::Hit) hitCount_++; else missCount_++;

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

    // Miss — see TODO in handleReadShared::Miss block.
    assert(resp.result != LookupResult::MissEvictDirty);

    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::UD;
    pending_[memId] = pt;

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
    if (resp.result == LookupResult::Hit) hitCount_++; else missCount_++;

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
            pending_[memId] = pt;

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
            cache_->addSharer(addr, reqNum);
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

    // Miss — see TODO in handleReadShared::Miss block.
    assert(resp.result != LookupResult::MissEvictDirty);

    TxnID memId = nextInternalTxnId_++;
    memToOrig_[memId] = txn.txnID;

    PendingTxn pt;
    pt.origReq = txn;
    pt.state = PendingState::WaitMemData;
    pt.targetState = LineState::UC;
    pending_[memId] = pt;

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
    TxnID txnId, Addr addr, const uint8_t* data)
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
        // Fallback: gem5 may deliver L1 writeback data with txnId=0.
        // Search by address + WaitL1Data state.
        if (it == pending_.end()) {
            for (auto pi = pending_.begin(); pi != pending_.end(); ++pi) {
                if (pi->second.state == PendingState::WaitL1Data
                    && pi->second.origReq.addr == addr) {
                    it = pi;
                    break;
                }
            }
        }
        if (it != pending_.end() && it->second.state == PendingState::WaitL1Data) {
            // WriteBackFull: L1 evicts dirty data back to L2.
            // Keep data in L2 (UD state) instead of writing to memory via
            // WriteNoSnp+NCBWrData, which has a timing issue where NCBWrData
            // arrives at the Memory Controller before the WriteNoSnp request.
            cache_->fill(it->second.origReq.addr, LineState::UD, data);
            cache_->addSharer(it->second.origReq.addr, it->second.origReq.srcNodeID);
            pending_.erase(it);
            return {};
        }
    }

    // Memory data
    {
        auto it = pending_.find(txnId);
        if (it == pending_.end()) return {};  // Unsolicited or already processed

        PendingTxn& pt = it->second;
        if (pt.state != PendingState::WaitMemData) return {};

        Addr lineAddr = pt.origReq.addr;
        cache_->fill(lineAddr, pt.targetState, data);
        cache_->addSharer(lineAddr, pt.origReq.srcNodeID);

        std::vector<ProtocolAction> actions;

        if (pt.origReq.opcode == Opcode::CleanUnique) {
            auto a = makeAction(ProtocolAction::SendComp, lineAddr, pt.origReq.txnID);
            a.destNode = pt.origReq.srcNodeID;
            actions.push_back(a);
        } else {
            auto a = makeAction(ProtocolAction::SendCompData, lineAddr, pt.origReq.txnID);
            a.destNode = pt.origReq.srcNodeID;
            a.respState = cache_->getState(lineAddr);
            std::memcpy(a.data, cache_->getData(lineAddr), CACHE_LINE_SIZE);
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
        cache_->addSharer(addr, pt.origReq.srcNodeID);
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

// ============================================================
// printStats
// ============================================================

void ChiProtocolEngine::printStats() const {
    uint64_t total = readSharedReqs_ + readUniqueReqs_ + cleanUniqueReqs_
                     + writeBackReqs_ + evictCount_;
    uint64_t accesses = hitCount_ + missCount_;

    printf("[L2 Stats] ============================================\n");
    printf("[L2 Stats] Total requests:          %lu\n", total);
    printf("[L2 Stats]   ReadShared:            %lu\n", readSharedReqs_);
    printf("[L2 Stats]   ReadUnique:            %lu\n", readUniqueReqs_);
    printf("[L2 Stats]   CleanUnique:           %lu\n", cleanUniqueReqs_);
    printf("[L2 Stats]   WriteBackFull:         %lu\n", writeBackReqs_);
    printf("[L2 Stats]   Evict:                 %lu\n", evictCount_);
    printf("[L2 Stats] ----------------------------------------\n");
    printf("[L2 Stats] Cache lookups:           %lu\n", accesses);
    printf("[L2 Stats]   Hit:                   %lu\n", hitCount_);
    printf("[L2 Stats]   Miss:                  %lu\n", missCount_);
    if (accesses > 0) {
        printf("[L2 Stats]   Hit rate:              %.1f%%\n",
               100.0 * hitCount_ / accesses);
    }
    printf("[L2 Stats] ============================================\n");
}

} // namespace chi
