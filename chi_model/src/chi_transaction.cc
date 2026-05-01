#include "chi_transaction.hh"

namespace chi {

bool ChiTransaction::isRequest() const {
    return opcode == Opcode::ReadShared ||
           opcode == Opcode::ReadUnique ||
           opcode == Opcode::CleanUnique ||
           opcode == Opcode::WriteBackFull;
}

bool ChiTransaction::isResponse() const {
    return opcode == Opcode::CompData ||
           opcode == Opcode::WriteAck ||
           opcode == Opcode::Comp;
}

bool ChiTransaction::needsSNForward() const {
    return opcode == Opcode::ReadShared ||
           opcode == Opcode::ReadUnique ||
           opcode == Opcode::WriteBackFull;
}

} // namespace chi
