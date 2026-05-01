#include "chi_channel.hh"
#include "chi_transaction.hh"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace chi;

void testPushPop() {
    Channel<int> ch;
    ch.push(42);
    assert(ch.pop() == 42);
    std::cout << "  PASS: push/pop basic\n";
}

void testTryPopEmpty() {
    Channel<int> ch;
    auto result = ch.tryPop();
    assert(!result.has_value());
    std::cout << "  PASS: tryPop on empty channel\n";
}

void testTryPopNonEmpty() {
    Channel<int> ch;
    ch.push(100);
    auto result = ch.tryPop();
    assert(result.has_value());
    assert(result.value() == 100);
    assert(ch.empty());
    std::cout << "  PASS: tryPop on non-empty channel\n";
}

void testBlockingPop() {
    Channel<int> ch;
    bool received = false;
    int value = 0;

    std::thread consumer([&] {
        value = ch.pop();
        received = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!received);

    ch.push(99);
    consumer.join();
    assert(received);
    assert(value == 99);
    std::cout << "  PASS: blocking pop waits for push\n";
}

void testTransactionChannel() {
    Channel<ChiTransaction> ch;
    ChiTransaction txn;
    txn.txnID = 1;
    txn.opcode = Opcode::ReadShared;
    txn.addr = 0x1000;
    txn.size = 64;
    txn.srcNodeID = 0;

    ch.push(txn);
    auto received = ch.pop();
    assert(received.txnID == 1);
    assert(received.opcode == Opcode::ReadShared);
    assert(received.addr == 0x1000);
    assert(received.srcNodeID == 0);
    std::cout << "  PASS: ChiTransaction channel\n";
}

int main() {
    std::cout << "Channel tests:\n";
    testPushPop();
    testTryPopEmpty();
    testTryPopNonEmpty();
    testBlockingPop();
    testTransactionChannel();
    std::cout << "All Channel tests passed!\n";
    return 0;
}
