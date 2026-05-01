#pragma once

#include "chi_transaction.hh"

namespace chi {
namespace middleware {

ChiTransaction convertToTransaction(
    int gem5Opcode,
    uint64_t addr,
    uint32_t size,
    uint16_t srcNodeID,
    uint32_t txnID,
    const uint8_t* data = nullptr,
    uint32_t dataLen = 0
);

struct Gem5Response {
    int gem5Opcode;
    uint64_t addr;
    uint32_t size;
    uint16_t destNodeID;
    uint32_t txnID;
    std::vector<uint8_t> data;
    int respStatus;
};

Gem5Response convertFromTransaction(const ChiTransaction& txn);

namespace Gem5Opcode {
    constexpr int ReadShared    = 0;
    constexpr int ReadUnique    = 1;
    constexpr int CleanUnique   = 2;
    constexpr int WriteBackFull = 3;
    constexpr int ReadNoSnp     = 4;
    constexpr int WriteNoSnp    = 5;
    constexpr int CompData      = 6;
    constexpr int WriteAck      = 7;
    constexpr int Comp          = 8;
}

} // namespace middleware
} // namespace chi
