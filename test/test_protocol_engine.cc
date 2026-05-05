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

// Helper: fill data with pattern
static void fillPattern(uint8_t* data, uint8_t pattern) {
    for (int i = 0; i < 64; i++) data[i] = pattern;
}

// --- Test Cases ---

static void test_ReadShared_Miss() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendReadNoSnp);
    assert(actions[0].addr == addr);
    std::cout << "  PASS: ReadShared miss -> SendReadNoSnp\n";
}

static void test_ReadShared_Hit_SC() {
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
    std::cout << "  PASS: ReadShared SC hit -> SendCompData\n";
}

static void test_ReadShared_Miss_Then_Data() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    assert(actions1.size() == 1);
    assert(actions1[0].type == ProtocolAction::SendReadNoSnp);
    TxnID memId = actions1[0].txnId;

    uint8_t data[64];
    fillPattern(data, 0xCD);
    auto actions2 = engine.recvData(memId, addr, data);
    assert(actions2.size() == 1);
    assert(actions2[0].type == ProtocolAction::SendCompData);
    assert(actions2[0].respState == LineState::SC);
    assert(cache.getState(addr) == LineState::SC);
    std::cout << "  PASS: ReadShared miss -> recvData -> SendCompData\n";
}

static void test_ReadUnique_Hit_SoleSharer() {
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
    std::cout << "  PASS: ReadUnique sole sharer -> SendCompData UD\n";
}

static void test_ReadUnique_Hit_WithOtherSharers() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::SC, data);
    cache.addSharer(addr, 0);
    cache.addSharer(addr, 1);

    auto actions = engine.recvRequest(makeReq(Opcode::ReadUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendSnpCleanInvalid);
    assert(actions[0].destNode == 1);
    assert(actions[0].retToSrc == false);
    std::cout << "  PASS: ReadUnique with other sharer -> SendSnpCleanInvalid\n";
}

static void test_ReadUnique_Snoop_Then_Complete() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::SC, data);
    cache.addSharer(addr, 0);
    cache.addSharer(addr, 1);
    cache.addSharer(addr, 2);

    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadUnique, addr, 0, 1));
    assert(actions1.size() == 2);
    assert(actions1[0].type == ProtocolAction::SendSnpCleanInvalid);
    assert(actions1[1].type == ProtocolAction::SendSnpCleanInvalid);

    auto actions2 = engine.recvResponse(actions1[0].txnId, RespStatus::OK);
    assert(actions2.empty());

    auto actions3 = engine.recvResponse(actions1[1].txnId, RespStatus::OK);
    assert(actions3.size() == 1);
    assert(actions3[0].type == ProtocolAction::SendCompData);
    assert(actions3[0].respState == LineState::UD);
    std::cout << "  PASS: ReadUnique snoop complete -> SendCompData UD\n";
}

static void test_CleanUnique_UC_Hit() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::UC, data);
    cache.addSharer(addr, 0);

    auto actions = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendComp);
    std::cout << "  PASS: CleanUnique UC hit -> SendComp\n";
}

static void test_CleanUnique_Miss() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendReadNoSnp);
    std::cout << "  PASS: CleanUnique miss -> SendReadNoSnp\n";
}

static void test_CleanUnique_Miss_Then_Data() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions1 = engine.recvRequest(makeReq(Opcode::CleanUnique, addr, 0, 1));
    assert(actions1[0].type == ProtocolAction::SendReadNoSnp);
    TxnID memId = actions1[0].txnId;

    uint8_t data[64];
    fillPattern(data, 0x55);
    auto actions2 = engine.recvData(memId, addr, data);
    assert(actions2.size() == 1);
    assert(actions2[0].type == ProtocolAction::SendComp);
    assert(cache.getState(addr) == LineState::UC);
    std::cout << "  PASS: CleanUnique miss -> recvData -> SendComp\n";
}

static void test_WriteBackFull() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions = engine.recvRequest(makeReq(Opcode::WriteBackFull, addr, 0, 1));
    assert(actions.size() == 1);
    assert(actions[0].type == ProtocolAction::SendCompDBIDResp);
    assert(actions[0].destNode == 0);
    std::cout << "  PASS: WriteBackFull -> SendCompDBIDResp\n";
}

static void test_Evict() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    uint8_t data[64];
    cache.fill(addr, LineState::UD, data);
    assert(cache.getState(addr) == LineState::UD);

    engine.invalidate(addr);
    assert(cache.getState(addr) == LineState::I);
    std::cout << "  PASS: Evict -> invalidate\n";
}

static void test_WriteBackFull_Then_Data() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    // Step 1: WriteBackFull request → CompDBIDResp
    auto actions1 = engine.recvRequest(makeReq(Opcode::WriteBackFull, addr, 0, 1));
    assert(actions1.size() == 1);
    assert(actions1[0].type == ProtocolAction::SendCompDBIDResp);
    assert(actions1[0].destNode == 0);
    assert(actions1[0].txnId == 1);

    // Step 2: L1 sends dirty data (txnId=0, address-based match)
    uint8_t dirty[64];
    fillPattern(dirty, 0xDD);
    auto actions2 = engine.recvData(0, addr, dirty);
    // Dirty data absorbed into L2 as UD. No WriteNoSnp to memory.
    assert(actions2.empty());
    assert(cache.getState(addr) == LineState::UD);
    assert(cache.needsWriteback(addr));
    std::cout << "  PASS: WriteBackFull -> CompDBIDResp -> recvData -> UD\n";
}

static void test_DuplicateData_Ignored() {
    L2Cache cache;
    ChiProtocolEngine engine(&cache);
    Addr addr = 0x10000;

    auto actions1 = engine.recvRequest(makeReq(Opcode::ReadShared, addr, 0, 1));
    TxnID memId = actions1[0].txnId;

    uint8_t data[64];
    fillPattern(data, 0x42);
    engine.recvData(memId, addr, data);

    uint8_t data2[64];
    fillPattern(data2, 0xFF);
    auto actions3 = engine.recvData(memId, addr, data2);
    assert(actions3.empty());
    std::cout << "  PASS: Duplicate data -> ignored\n";
}

int main() {
    std::cout << "ChiProtocolEngine tests:\n";
    test_ReadShared_Miss();
    test_ReadShared_Hit_SC();
    test_ReadShared_Miss_Then_Data();
    test_ReadUnique_Hit_SoleSharer();
    test_ReadUnique_Hit_WithOtherSharers();
    test_ReadUnique_Snoop_Then_Complete();
    test_CleanUnique_UC_Hit();
    test_CleanUnique_Miss();
    test_CleanUnique_Miss_Then_Data();
    test_WriteBackFull();
    test_WriteBackFull_Then_Data();
    test_Evict();
    test_DuplicateData_Ignored();
    std::cout << "All ChiProtocolEngine tests passed!\n";
    return 0;
}
