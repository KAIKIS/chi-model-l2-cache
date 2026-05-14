#pragma once

#include "l2_cache.hh"
#include "chi_transaction.hh"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chi {

// Instruction from Engine to Middleware.
// Middleware translates each ProtocolAction into gem5 message sends.
struct ProtocolAction {
    enum Type : uint8_t {
        SendReadNoSnp,           // Send ReadNoSnp to memory
        SendWriteNoSnp,          // Send WriteNoSnp + NCBWrData to memory
        SendCompData,            // Send CompData to RN-F (with data + state)
        SendComp,                // Send Comp to RN-F (no data)
        SendCompDBIDResp,        // Ask RN-F to send writeback data
        SendSnpCleanInvalid,     // Send SnpCleanInvalid to an RN-F
        SendSnpUnique,           // Send SnpUnique to an RN-F (for ReadUnique)
        SendSnpNotSharedDirty,   // Send SnpNotSharedDirty to an RN-F (for CleanUnique SD)
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
    ChiProtocolEngine(L2Cache* cache, const char* label = "L2");

    // === Three entry points (synchronous) ===
    // Each returns actions the Middleware must execute.
    // Empty vector = transaction completed or waiting for external event.

    std::vector<ProtocolAction> recvRequest(const ChiTransaction& txn);
    std::vector<ProtocolAction> recvData(TxnID txnId, Addr addr, const uint8_t* data);
    std::vector<ProtocolAction> recvResponse(TxnID txnId, RespStatus status);

    // Direct cache line invalidation (for gem5 Evict -- not a protocol txn)
    void invalidate(Addr addr) { cache_->invalidate(addr); evictCount_++; }

    // Print statistics
    void printStats() const;

private:
    L2Cache* cache_;
    const char* label_;

    // Pending transactions keyed by original L1 txnId
    std::unordered_map<TxnID, PendingTxn> pending_;

    // snoop/memory txnId to original L1 txnId
    std::unordered_map<TxnID, TxnID> snoopToOrig_;
    std::unordered_map<TxnID, TxnID> memToOrig_;

    // SnpRespData_* on datIn carries both data and response. Track which
    // snoop txnIds have already had their response counted so we don't
    // double-count across multiple data beats.
    std::unordered_set<TxnID> snoopRespCounted_;

    // txnId ranges to avoid collision with gem5-assigned txnIds (0–63):
    TxnID nextSnoopTxnId_    = 10000;  // txnIds for SnpCleanInvalid messages
    TxnID nextInternalTxnId_ = 20000;  // txnIds for ReadNoSnp/WriteNoSnp to memory

    // Stats
    uint64_t readSharedReqs_         = 0;
    uint64_t readUniqueReqs_         = 0;
    uint64_t cleanUniqueReqs_        = 0;
    uint64_t writeBackReqs_          = 0;
    uint64_t readNotSharedDirtyReqs_ = 0;
    uint64_t writeUniqueFullReqs_    = 0;
    uint64_t writeEvictFullReqs_     = 0;
    uint64_t evictCount_             = 0;
    uint64_t hitCount_               = 0;
    uint64_t missCount_              = 0;

    // Per-opcode request handlers
    std::vector<ProtocolAction> handleReadShared(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleReadUnique(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleCleanUnique(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleWriteBackFull(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleReadNotSharedDirty(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleWriteUniqueFull(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleWriteEvictFull(const ChiTransaction& txn);
    std::vector<ProtocolAction> handleWriteNoSnp(const ChiTransaction& txn);

    // Complete a pending transaction after all snoops/data received
    std::vector<ProtocolAction> completePending(TxnID origTxnId);

    // Utility: create a basic action with type/addr/txnId filled
    ProtocolAction makeAction(ProtocolAction::Type type, Addr addr, TxnID txnId);
};

} // namespace chi
