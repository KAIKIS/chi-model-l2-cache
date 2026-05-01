#include "chi_middleware.hh"

namespace chi {
namespace middleware {

ChiTransaction convertToTransaction(
    int gem5Opcode,
    uint64_t addr,
    uint32_t size,
    uint16_t srcNodeID,
    uint32_t txnID,
    const uint8_t* data,
    uint32_t dataLen)
{
    ChiTransaction txn;
    txn.txnID = txnID;
    txn.addr = addr;
    txn.size = size;
    txn.srcNodeID = srcNodeID;

    switch (gem5Opcode) {
        case Gem5Opcode::ReadShared:    txn.opcode = Opcode::ReadShared; break;
        case Gem5Opcode::ReadUnique:    txn.opcode = Opcode::ReadUnique; break;
        case Gem5Opcode::CleanUnique:   txn.opcode = Opcode::CleanUnique; break;
        case Gem5Opcode::WriteBackFull: txn.opcode = Opcode::WriteBackFull; break;
        case Gem5Opcode::ReadNoSnp:     txn.opcode = Opcode::ReadNoSnp; break;
        case Gem5Opcode::WriteNoSnp:    txn.opcode = Opcode::WriteNoSnp; break;
        default: break;
    }

    if (data && dataLen > 0) {
        txn.data.assign(data, data + dataLen);
    }

    return txn;
}

Gem5Response convertFromTransaction(const ChiTransaction& txn) {
    Gem5Response resp;
    resp.addr = txn.addr;
    resp.size = txn.size;
    resp.destNodeID = txn.srcNodeID;
    resp.txnID = txn.txnID;
    resp.data = txn.data;

    switch (txn.opcode) {
        case Opcode::CompData:
            resp.gem5Opcode = Gem5Opcode::CompData;
            break;
        case Opcode::WriteAck:
            resp.gem5Opcode = Gem5Opcode::WriteAck;
            break;
        case Opcode::Comp:
            resp.gem5Opcode = Gem5Opcode::Comp;
            break;
        default:
            break;
    }

    resp.respStatus = static_cast<int>(txn.respStatus);
    return resp;
}

} // namespace middleware
} // namespace chi
