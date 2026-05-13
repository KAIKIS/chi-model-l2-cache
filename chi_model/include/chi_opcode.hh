#pragma once

#include <cstdint>
#include <string>

namespace chi {

enum class Opcode : uint8_t {
    // RN 请求
    ReadShared         = 0x00,
    ReadUnique         = 0x01,
    CleanUnique        = 0x02,
    WriteBackFull      = 0x03,
    ReadNotSharedDirty = 0x04,
    WriteUniqueFull    = 0x05,
    WriteEvictFull     = 0x06,

    // SN 请求
    ReadNoSnp     = 0x10,
    WriteNoSnp    = 0x11,

    // 响应
    CompData      = 0x20,
    WriteAck      = 0x21,
    Comp          = 0x22,

    // Snoop 请求
    SnpCleanInvalid    = 0x30,
    SnpUnique          = 0x31,
    SnpNotSharedDirty  = 0x32,

    // Snoop 响应
    SnpCleanInvalidResp    = 0x40,
    SnpUniqueResp          = 0x41,
    SnpNotSharedDirtyResp  = 0x42,
};

inline const char* opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::ReadShared:         return "ReadShared";
        case Opcode::ReadUnique:         return "ReadUnique";
        case Opcode::CleanUnique:        return "CleanUnique";
        case Opcode::WriteBackFull:      return "WriteBackFull";
        case Opcode::ReadNotSharedDirty: return "ReadNotSharedDirty";
        case Opcode::WriteUniqueFull:    return "WriteUniqueFull";
        case Opcode::WriteEvictFull:     return "WriteEvictFull";
        case Opcode::ReadNoSnp:          return "ReadNoSnp";
        case Opcode::WriteNoSnp:         return "WriteNoSnp";
        case Opcode::CompData:           return "CompData";
        case Opcode::WriteAck:           return "WriteAck";
        case Opcode::Comp:               return "Comp";
        case Opcode::SnpCleanInvalid:    return "SnpCleanInvalid";
        case Opcode::SnpUnique:          return "SnpUnique";
        case Opcode::SnpNotSharedDirty:  return "SnpNotSharedDirty";
        case Opcode::SnpCleanInvalidResp:    return "SnpCleanInvalidResp";
        case Opcode::SnpUniqueResp:          return "SnpUniqueResp";
        case Opcode::SnpNotSharedDirtyResp:  return "SnpNotSharedDirtyResp";
        default:                         return "Unknown";
    }
}

} // namespace chi
