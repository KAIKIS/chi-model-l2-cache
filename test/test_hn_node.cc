#include "chi_hn_node.hh"

#include <cassert>
#include <iostream>
#include <thread>

using namespace chi;

void testReadSharedForwarding() {
    HnNode hn(1);
    auto& rnCh = hn.getRNChannel();
    auto& snCh = hn.getSNChannel();

    std::thread hnThread([&hn] { hn.process(); });

    ChiTransaction req;
    req.txnID = 1;
    req.opcode = Opcode::ReadShared;
    req.addr = 0x1000;
    req.size = 64;
    req.srcNodeID = 0;
    rnCh.push(req);

    auto snReq = snCh.pop();
    assert(snReq.txnID == 1);
    assert(snReq.opcode == Opcode::ReadNoSnp);
    assert(snReq.addr == 0x1000);
    assert(snReq.srcNodeID == 0);

    ChiTransaction resp;
    resp.txnID = 1;
    resp.opcode = Opcode::CompData;
    resp.addr = 0x1000;
    resp.data = std::vector<uint8_t>(64, 0xAB);
    resp.respStatus = RespStatus::OK;
    snCh.push(resp);

    auto rnResp = rnCh.pop();
    assert(rnResp.txnID == 1);
    assert(rnResp.opcode == Opcode::CompData);
    assert(rnResp.data.size() == 64);
    assert(rnResp.data[0] == 0xAB);

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: ReadShared -> ReadNoSnp -> CompData forwarding\n";
}

void testReadUniqueForwarding() {
    HnNode hn(1);
    auto& rnCh = hn.getRNChannel();
    auto& snCh = hn.getSNChannel();

    std::thread hnThread([&hn] { hn.process(); });

    ChiTransaction req;
    req.txnID = 2;
    req.opcode = Opcode::ReadUnique;
    req.addr = 0x2000;
    req.size = 64;
    req.srcNodeID = 0;
    rnCh.push(req);

    auto snReq = snCh.pop();
    assert(snReq.opcode == Opcode::ReadNoSnp);

    ChiTransaction resp;
    resp.txnID = 2;
    resp.opcode = Opcode::CompData;
    resp.addr = 0x2000;
    resp.respStatus = RespStatus::Unique;
    snCh.push(resp);

    auto rnResp = rnCh.pop();
    assert(rnResp.opcode == Opcode::CompData);

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: ReadUnique -> ReadNoSnp forwarding\n";
}

void testWriteBackFullForwarding() {
    HnNode hn(1);
    auto& rnCh = hn.getRNChannel();
    auto& snCh = hn.getSNChannel();

    std::thread hnThread([&hn] { hn.process(); });

    ChiTransaction req;
    req.txnID = 3;
    req.opcode = Opcode::WriteBackFull;
    req.addr = 0x3000;
    req.size = 64;
    req.srcNodeID = 0;
    req.data = std::vector<uint8_t>(64, 0xCD);
    rnCh.push(req);

    auto snReq = snCh.pop();
    assert(snReq.opcode == Opcode::WriteNoSnp);
    assert(snReq.data[0] == 0xCD);

    ChiTransaction resp;
    resp.txnID = 3;
    resp.opcode = Opcode::WriteAck;
    resp.addr = 0x3000;
    snCh.push(resp);

    auto rnResp = rnCh.pop();
    assert(rnResp.opcode == Opcode::WriteAck);

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: WriteBackFull -> WriteNoSnp -> WriteAck forwarding\n";
}

void testCleanUniqueDirectComp() {
    HnNode hn(1);
    auto& rnCh = hn.getRNChannel();
    auto& snCh = hn.getSNChannel();

    std::thread hnThread([&hn] { hn.process(); });

    ChiTransaction req;
    req.txnID = 4;
    req.opcode = Opcode::CleanUnique;
    req.addr = 0x4000;
    req.size = 64;
    req.srcNodeID = 0;
    rnCh.push(req);

    auto rnResp = rnCh.pop();
    assert(rnResp.txnID == 4);
    assert(rnResp.opcode == Opcode::Comp);
    assert(rnResp.respStatus == RespStatus::OK);

    auto snReq = snCh.tryPop();
    assert(!snReq.has_value());

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: CleanUnique -> Comp (no SN forward)\n";
}

int main() {
    std::cout << "HnNode tests:\n";
    testReadSharedForwarding();
    testReadUniqueForwarding();
    testWriteBackFullForwarding();
    testCleanUniqueDirectComp();
    std::cout << "All HnNode tests passed!\n";
    return 0;
}
