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
