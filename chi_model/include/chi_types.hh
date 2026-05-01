#pragma once

#include <cstdint>

namespace chi {

using NodeID = uint16_t;
using TxnID  = uint32_t;
using Addr   = uint64_t;

enum class RespStatus : uint8_t {
    OK       = 0,
    Error    = 1,
    Unique   = 2,
    Shared   = 3,
    Invalid  = 4,
};

} // namespace chi
