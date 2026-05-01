# ARM CHI 协议模型 + L2 Cache 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个可复用的 ARM CHI 协议模型（Transaction 级）和直通模式 L2 Cache，集成到 gem5 进行端到端验证。

**Architecture:** CHI 模型提供 Transaction 数据结构、Channel 通信和 HnNode 基类；L2 Cache 继承 HnNode 实现直通转发；Middleware 负责 gem5 消息格式转换；SLCc 状态机通过 in-flight 跟踪表桥接 gem5 Ruby 网络和 CHI 模型。

**Tech Stack:** C++17, CMake, gem5 SCons, aarch64 交叉编译工具链

**设计文档:** `docs/design/2026-05-01-arm-chi-l2-cache-design.md`

---

## 文件结构

```
CHI-new/
├── CMakeLists.txt                          # 顶层 CMake，C++17，add_subdirectory 各模块
├── chi_model/
│   ├── CMakeLists.txt                      # libchi_model.a 静态库
│   ├── include/
│   │   ├── chi_types.hh                    # NodeID, TxnID, Addr 类型别名
│   │   ├── chi_opcode.hh                   # Opcode 枚举（9 个）
│   │   ├── chi_transaction.hh              # ChiTransaction 结构体
│   │   ├── chi_channel.hh                  # Channel<T> 模板类
│   │   ├── chi_node.hh                     # ChiNode 基类
│   │   └── chi_hn_node.hh                  # HnNode 类
│   └── src/
│       ├── chi_transaction.cc              # ChiTransaction 实现
│       └── chi_hn_node.cc                  # HnNode::process() 主循环 + opcode 转换
├── cache_model/
│   ├── CMakeLists.txt                      # libcache_model.a 静态库
│   ├── include/
│   │   └── simple_l2_cache.hh              # SimpleL2Cache 类
│   └── src/
│       └── simple_l2_cache.cc              # 直通模式实现
├── middleware/
│   ├── CMakeLists.txt                      # libmiddleware.a 静态库
│   ├── include/
│   │   └── chi_middleware.hh               # 转换函数声明
│   └── src/
│       └── chi_middleware.cc               # 直接 opcode 映射实现
├── test/
│   ├── CMakeLists.txt                      # 测试可执行文件
│   ├── test_channel.cc                     # Channel 单元测试
│   ├── test_hn_node.cc                     # HnNode 转发流程测试
│   └── test_128kb.cc                       # 端到端验证程序
└── gem5_integration/
    ├── our_l2.sm                           # SLCc 状态机
    ├── OurL2.py                            # SimObject 配置
    └── SConscript                          # gem5 构建注册
```

---

## Task 1: 项目初始化 — CMake 构建系统

**Files:**
- Create: `CMakeLists.txt`
- Create: `chi_model/CMakeLists.txt`
- Create: `cache_model/CMakeLists.txt`
- Create: `middleware/CMakeLists.txt`
- Create: `test/CMakeLists.txt`

- [ ] **Step 1: 创建顶层 CMakeLists.txt**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(CHI-new LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(chi_model)
add_subdirectory(cache_model)
add_subdirectory(middleware)
add_subdirectory(test)
```

- [ ] **Step 2: 创建 chi_model/CMakeLists.txt**

```cmake
# chi_model/CMakeLists.txt
add_library(chi_model STATIC
    src/chi_transaction.cc
    src/chi_hn_node.cc
)
target_include_directories(chi_model PUBLIC include)
```

- [ ] **Step 3: 创建 cache_model/CMakeLists.txt**

```cmake
# cache_model/CMakeLists.txt
add_library(cache_model STATIC
    src/simple_l2_cache.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
```

- [ ] **Step 4: 创建 middleware/CMakeLists.txt**

```cmake
# middleware/CMakeLists.txt
add_library(middleware STATIC
    src/chi_middleware.cc
)
target_include_directories(middleware PUBLIC include)
target_link_libraries(middleware PUBLIC chi_model)
```

- [ ] **Step 5: 创建 test/CMakeLists.txt**

```cmake
# test/CMakeLists.txt
add_executable(test_channel test_channel.cc)
target_link_libraries(test_channel PRIVATE chi_model)

add_executable(test_hn_node test_hn_node.cc)
target_link_libraries(test_hn_node PRIVATE chi_model)

add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)
```

- [ ] **Step 6: 验证 CMake 配置**

```bash
cd /home/zhangkai/work/CHI-new
mkdir -p build && cd build
cmake ..
```

Expected: CMake 配置成功（源文件尚不存在会报错，这是正常的）

- [ ] **Step 7: 提交**

```bash
git add CMakeLists.txt chi_model/CMakeLists.txt cache_model/CMakeLists.txt middleware/CMakeLists.txt test/CMakeLists.txt
git commit -m "feat: initialize CMake build system with C++17 and static library targets"
```

---

## Task 2: CHI 基础类型 — chi_types.hh + chi_opcode.hh

**Files:**
- Create: `chi_model/include/chi_types.hh`
- Create: `chi_model/include/chi_opcode.hh`

- [ ] **Step 1: 创建 chi_types.hh**

```cpp
// chi_model/include/chi_types.hh
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
```

- [ ] **Step 2: 创建 chi_opcode.hh**

```cpp
// chi_model/include/chi_opcode.hh
#pragma once

#include <cstdint>
#include <string>

namespace chi {

enum class Opcode : uint8_t {
    // RN 请求
    ReadShared    = 0x00,
    ReadUnique    = 0x01,
    CleanUnique   = 0x02,
    WriteBackFull = 0x03,

    // SN 请求
    ReadNoSnp     = 0x10,
    WriteNoSnp    = 0x11,

    // 响应
    CompData      = 0x20,
    WriteAck      = 0x21,
    Comp          = 0x22,
};

inline const char* opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::ReadShared:    return "ReadShared";
        case Opcode::ReadUnique:    return "ReadUnique";
        case Opcode::CleanUnique:   return "CleanUnique";
        case Opcode::WriteBackFull: return "WriteBackFull";
        case Opcode::ReadNoSnp:     return "ReadNoSnp";
        case Opcode::WriteNoSnp:    return "WriteNoSnp";
        case Opcode::CompData:      return "CompData";
        case Opcode::WriteAck:      return "WriteAck";
        case Opcode::Comp:          return "Comp";
        default:                    return "Unknown";
    }
}

} // namespace chi
```

- [ ] **Step 3: 提交**

```bash
git add chi_model/include/chi_types.hh chi_model/include/chi_opcode.hh
git commit -m "feat: define CHI basic types (NodeID, TxnID, Addr) and 9 opcodes"
```

---

## Task 3: ChiTransaction 结构体

**Files:**
- Create: `chi_model/include/chi_transaction.hh`
- Create: `chi_model/src/chi_transaction.cc`

- [ ] **Step 1: 创建 chi_transaction.hh**

```cpp
// chi_model/include/chi_transaction.hh
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

    bool isRequest() const;
    bool isResponse() const;
    bool needsSNForward() const;
};

} // namespace chi
```

- [ ] **Step 2: 创建 chi_transaction.cc**

```cpp
// chi_model/src/chi_transaction.cc
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
```

- [ ] **Step 3: 更新 test/CMakeLists.txt，添加编译依赖**

在 `test/CMakeLists.txt` 顶部添加空行占位（后续 Task 6 会添加测试内容），确保 `chi_model` 编译通过。

- [ ] **Step 4: 验证编译**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make chi_model
```

Expected: `libchi_model.a` 编译成功

- [ ] **Step 5: 提交**

```bash
git add chi_model/include/chi_transaction.hh chi_model/src/chi_transaction.cc
git commit -m "feat: implement ChiTransaction struct with srcNodeID and returnTxnID"
```

---

## Task 4: Channel<T> 模板类

**Files:**
- Create: `chi_model/include/chi_channel.hh`

- [ ] **Step 1: 创建 chi_channel.hh**

```cpp
// chi_model/include/chi_channel.hh
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace chi {

template<typename T>
class Channel {
public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace chi
```

- [ ] **Step 2: 验证编译**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make chi_model
```

Expected: 编译成功（Channel 是 header-only 模板）

- [ ] **Step 3: 提交**

```bash
git add chi_model/include/chi_channel.hh
git commit -m "feat: implement Channel<T> template with blocking push/pop"
```

---

## Task 5: ChiNode 基类 + HnNode 实现

**Files:**
- Create: `chi_model/include/chi_node.hh`
- Create: `chi_model/include/chi_hn_node.hh`
- Create: `chi_model/src/chi_hn_node.cc`

- [ ] **Step 1: 创建 chi_node.hh**

```cpp
// chi_model/include/chi_node.hh
#pragma once

#include "chi_types.hh"

namespace chi {

enum class NodeType : uint8_t {
    RN_F = 0,   // Request Node - Fully coherent
    HN_F = 1,   // Home Node - Fully coherent
    SN_F = 2,   // Slave Node - Fully coherent
};

class ChiNode {
public:
    ChiNode(NodeID id, NodeType type) : nodeID_(id), nodeType_(type) {}
    virtual ~ChiNode() = default;

    NodeID getNodeID() const { return nodeID_; }
    NodeType getNodeType() const { return nodeType_; }

    virtual void process() = 0;

protected:
    NodeID nodeID_;
    NodeType nodeType_;
};

} // namespace chi
```

- [ ] **Step 2: 创建 chi_hn_node.hh**

```cpp
// chi_model/include/chi_hn_node.hh
#pragma once

#include "chi_node.hh"
#include "chi_transaction.hh"
#include "chi_channel.hh"

namespace chi {

class HnNode : public ChiNode {
public:
    HnNode(NodeID id);

    // RN 侧 Channel（SLCc ↔ HnNode）
    Channel<ChiTransaction>& getRNChannel() { return rnChannel_; }
    const Channel<ChiTransaction>& getRNChannel() const { return rnChannel_; }

    // SN 侧 Channel（HnNode ↔ SN/Memory）
    Channel<ChiTransaction>& getSNChannel() { return snChannel_; }
    const Channel<ChiTransaction>& getSNChannel() const { return snChannel_; }

    void process() override;
    void stop();

protected:
    // 子类可覆盖：决定如何处理请求（直通模式默认转发）
    virtual ChiTransaction transformRequest(const ChiTransaction& req);

    // 子类可覆盖：决定如何处理响应
    virtual ChiTransaction transformResponse(const ChiTransaction& resp);

private:
    void processRequest(const ChiTransaction& req);
    void processResponse(const ChiTransaction& resp);

    Channel<ChiTransaction> rnChannel_;   // 收 RN 请求，发 RN 响应
    Channel<ChiTransaction> snChannel_;   // 发 SN 请求，收 SN 响应
    bool running_ = true;
};

} // namespace chi
```

- [ ] **Step 3: 创建 chi_hn_node.cc**

```cpp
// chi_model/src/chi_hn_node.cc
#include "chi_hn_node.hh"

namespace chi {

HnNode::HnNode(NodeID id)
    : ChiNode(id, NodeType::HN_F) {
}

void HnNode::process() {
    while (running_) {
        // 检查 RN 侧是否有请求
        auto reqOpt = rnChannel_.tryPop();
        if (reqOpt.has_value()) {
            processRequest(reqOpt.value());
        }

        // 检查 SN 侧是否有响应
        auto respOpt = snChannel_.tryPop();
        if (respOpt.has_value()) {
            processResponse(respOpt.value());
        }
    }
}

void HnNode::stop() {
    running_ = false;
}

ChiTransaction HnNode::transformRequest(const ChiTransaction& req) {
    ChiTransaction snReq = req;
    switch (req.opcode) {
        case Opcode::ReadShared:
        case Opcode::ReadUnique:
            snReq.opcode = Opcode::ReadNoSnp;
            break;
        case Opcode::WriteBackFull:
            snReq.opcode = Opcode::WriteNoSnp;
            break;
        default:
            break;
    }
    return snReq;
}

ChiTransaction HnNode::transformResponse(const ChiTransaction& resp) {
    // 直通模式：响应原样转发，保留 srcNodeID 用于路由
    return resp;
}

void HnNode::processRequest(const ChiTransaction& req) {
    if (req.opcode == Opcode::CleanUnique) {
        // CleanUnique 不需要转发到 SN，直接回复 Comp
        ChiTransaction comp;
        comp.txnID = req.txnID;
        comp.opcode = Opcode::Comp;
        comp.addr = req.addr;
        comp.srcNodeID = req.srcNodeID;
        comp.respStatus = RespStatus::OK;
        rnChannel_.push(std::move(comp));
        return;
    }

    // 需要转发到 SN 的请求
    ChiTransaction snReq = transformRequest(req);
    snChannel_.push(std::move(snReq));
}

void HnNode::processResponse(const ChiTransaction& resp) {
    ChiTransaction rnResp = transformResponse(resp);
    rnChannel_.push(std::move(rnResp));
}

} // namespace chi
```

- [ ] **Step 4: 验证编译**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make chi_model
```

Expected: `libchi_model.a` 编译成功

- [ ] **Step 5: 提交**

```bash
git add chi_model/include/chi_node.hh chi_model/include/chi_hn_node.hh chi_model/src/chi_hn_node.cc
git commit -m "feat: implement ChiNode base class and HnNode with opcode transformation"
```

---

## Task 6: Channel 单元测试

**Files:**
- Create: `test/test_channel.cc`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: 创建 test_channel.cc**

```cpp
// test/test_channel.cc
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

    // 给 consumer 时间阻塞在 pop 上
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
```

- [ ] **Step 2: 更新 test/CMakeLists.txt**

确认 `test/CMakeLists.txt` 包含：

```cmake
add_executable(test_channel test_channel.cc)
target_link_libraries(test_channel PRIVATE chi_model pthread)
```

注意：需要链接 `pthread` 用于 std::thread。

- [ ] **Step 3: 编译并运行测试**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make test_channel
./test_channel
```

Expected:
```
Channel tests:
  PASS: push/pop basic
  PASS: tryPop on empty channel
  PASS: tryPop on non-empty channel
  PASS: blocking pop waits for push
  PASS: ChiTransaction channel
All Channel tests passed!
```

- [ ] **Step 4: 提交**

```bash
git add test/test_channel.cc test/CMakeLists.txt
git commit -m "test: add Channel unit tests (push/pop, blocking, tryPop)"
```

---

## Task 7: HnNode 单元测试

**Files:**
- Create: `test/test_hn_node.cc`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: 创建 test_hn_node.cc**

```cpp
// test/test_hn_node.cc
#include "chi_hn_node.hh"

#include <cassert>
#include <iostream>
#include <thread>

using namespace chi;

void testReadSharedForwarding() {
    HnNode hn(1);
    auto& rnCh = hn.getRNChannel();
    auto& snCh = hn.getSNChannel();

    // 启动 HnNode 处理线程
    std::thread hnThread([&hn] { hn.process(); });

    // 发送 ReadShared 请求
    ChiTransaction req;
    req.txnID = 1;
    req.opcode = Opcode::ReadShared;
    req.addr = 0x1000;
    req.size = 64;
    req.srcNodeID = 0;
    rnCh.push(req);

    // 从 SN 侧接收转发后的请求
    auto snReq = snCh.pop();
    assert(snReq.txnID == 1);
    assert(snReq.opcode == Opcode::ReadNoSnp);  // ReadShared → ReadNoSnp
    assert(snReq.addr == 0x1000);
    assert(snReq.srcNodeID == 0);

    // SN 回复 CompData
    ChiTransaction resp;
    resp.txnID = 1;
    resp.opcode = Opcode::CompData;
    resp.addr = 0x1000;
    resp.data = std::vector<uint8_t>(64, 0xAB);
    resp.respStatus = RespStatus::OK;
    snCh.push(resp);

    // 从 RN 侧接收响应
    auto rnResp = rnCh.pop();
    assert(rnResp.txnID == 1);
    assert(rnResp.opcode == Opcode::CompData);
    assert(rnResp.data.size() == 64);
    assert(rnResp.data[0] == 0xAB);

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: ReadShared → ReadNoSnp → CompData forwarding\n";
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
    assert(snReq.opcode == Opcode::ReadNoSnp);  // ReadUnique → ReadNoSnp

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
    std::cout << "  PASS: ReadUnique → ReadNoSnp forwarding\n";
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
    assert(snReq.opcode == Opcode::WriteNoSnp);  // WriteBackFull → WriteNoSnp
    assert(snReq.data[0] == 0xCD);

    // SN 回复 WriteAck
    ChiTransaction resp;
    resp.txnID = 3;
    resp.opcode = Opcode::WriteAck;
    resp.addr = 0x3000;
    snCh.push(resp);

    auto rnResp = rnCh.pop();
    assert(rnResp.opcode == Opcode::WriteAck);

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: WriteBackFull → WriteNoSnp → WriteAck forwarding\n";
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

    // CleanUnique 不转发到 SN，直接回复 Comp
    auto rnResp = rnCh.pop();
    assert(rnResp.txnID == 4);
    assert(rnResp.opcode == Opcode::Comp);
    assert(rnResp.respStatus == RespStatus::OK);

    // SN 侧应该没有消息
    auto snReq = snCh.tryPop();
    assert(!snReq.has_value());

    hn.stop();
    hnThread.join();
    std::cout << "  PASS: CleanUnique → Comp (no SN forward)\n";
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
```

- [ ] **Step 2: 更新 test/CMakeLists.txt**

确认包含：

```cmake
add_executable(test_hn_node test_hn_node.cc)
target_link_libraries(test_hn_node PRIVATE chi_model pthread)
```

- [ ] **Step 3: 编译并运行测试**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make test_hn_node
./test_hn_node
```

Expected:
```
HnNode tests:
  PASS: ReadShared → ReadNoSnp → CompData forwarding
  PASS: ReadUnique → ReadNoSnp forwarding
  PASS: WriteBackFull → WriteNoSnp → WriteAck forwarding
  PASS: CleanUnique → Comp (no SN forward)
All HnNode tests passed!
```

- [ ] **Step 4: 提交**

```bash
git add test/test_hn_node.cc test/CMakeLists.txt
git commit -m "test: add HnNode unit tests for all 4 opcode forwarding paths"
```

---

## Task 8: SimpleL2Cache 模型

**Files:**
- Create: `cache_model/include/simple_l2_cache.hh`
- Create: `cache_model/src/simple_l2_cache.cc`

- [ ] **Step 1: 创建 simple_l2_cache.hh**

```cpp
// cache_model/include/simple_l2_cache.hh
#pragma once

#include "chi_hn_node.hh"

namespace chi {

// 直通模式 L2 Cache，继承 HnNode
// Phase 1：无缓存状态，所有请求直接转发
class SimpleL2Cache : public HnNode {
public:
    SimpleL2Cache(NodeID id);

    // 直通模式：不覆盖 transformRequest/transformResponse
    // 使用 HnNode 的默认 opcode 转换逻辑
};

} // namespace chi
```

- [ ] **Step 2: 创建 simple_l2_cache.cc**

```cpp
// cache_model/src/simple_l2_cache.cc
#include "simple_l2_cache.hh"

namespace chi {

SimpleL2Cache::SimpleL2Cache(NodeID id)
    : HnNode(id) {
}

} // namespace chi
```

- [ ] **Step 3: 验证编译**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make cache_model
```

Expected: `libcache_model.a` 编译成功

- [ ] **Step 4: 提交**

```bash
git add cache_model/include/simple_l2_cache.hh cache_model/src/simple_l2_cache.cc
git commit -m "feat: implement SimpleL2Cache pass-through model"
```

---

## Task 9: Middleware 层

**Files:**
- Create: `middleware/include/chi_middleware.hh`
- Create: `middleware/src/chi_middleware.cc`

- [ ] **Step 1: 创建 chi_middleware.hh**

```cpp
// middleware/include/chi_middleware.hh
#pragma once

#include "chi_transaction.hh"

namespace chi {
namespace middleware {

// gem5 消息类型占位符（实际类型在 gem5 集成时替换）
// 这里定义通用接口，gem5 侧通过特化实现

// 请求方向：gem5 → ChiTransaction
ChiTransaction convertToTransaction(
    int gem5Opcode,
    uint64_t addr,
    uint32_t size,
    uint16_t srcNodeID,
    uint32_t txnID,
    const uint8_t* data = nullptr,
    uint32_t dataLen = 0
);

// 响应方向：ChiTransaction → gem5 格式
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

// gem5 opcode 常量（与 gem5 CHI 协议对应）
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
```

- [ ] **Step 2: 创建 chi_middleware.cc**

```cpp
// middleware/src/chi_middleware.cc
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
```

- [ ] **Step 3: 验证编译**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make middleware
```

Expected: `libmiddleware.a` 编译成功

- [ ] **Step 4: 提交**

```bash
git add middleware/include/chi_middleware.hh middleware/src/chi_middleware.cc
git commit -m "feat: implement middleware with direct opcode mapping (gem5 ↔ ChiTransaction)"
```

---

## Task 10: gem5 集成 — SLCc 状态机（our_l2.sm）

**Files:**
- Create: `gem5_integration/our_l2.sm`

- [ ] **Step 1: 创建 our_l2.sm**

```
// gem5_integration/our_l2.sm
// SLCc 状态机：桥接 gem5 Ruby CHI 网络和 CHI-new 模型

machine(MachineType, "OurL2"):
    // 网络端口
    network_port(RequestToDir, in, "Request network port")
    network_port(ResponseToDir, in, "Response network port")
    network_port(RequestFromDir, out, "Request network port")
    network_port(ResponseFromDir, out, "Response network port")

    // 状态声明
    enumeration(State, default="IDLE"):
        IDLE
        PROCESSING

    // In-flight 跟踪表
    structure(in_flight_table, Entry, 64):
        field(rxn_id, uint32_t)
        field(snxn_id, uint32_t)
        field(src_node, uint16_t)
        field(orig_op, uint8_t)
        field(addr, uint64_t)
        field(state, uint8_t)   // 0=SEND_SN, 1=WAIT_SN_RESP, 2=SEND_RN_RESP

    // Action: 处理来自 RN 的请求
    action(handle_rn_request, "a", desc="Handle incoming RN request"):
        // 1. 从网络接收请求
        // 2. 分配 in-flight entry
        // 3. 通过 middleware 转换
        // 4. 推入 Channel 到 HnNode
        // 5. 标记 entry 为 WAIT_SN_RESP

    // Action: 处理来自 SN 的响应
    action(handle_sn_response, "b", desc="Handle SN response"):
        // 1. 从 Channel 接收响应
        // 2. 查找 in-flight table by txnID
        // 3. 通过 middleware 转换回 gem5 格式
        // 4. 发送到 RN 网络
        // 5. 释放 in-flight entry

    // Transition
    transition(IDLE, RequestToDir, handle_rn_request, IDLE)
    transition(IDLE, ResponseToDir, handle_sn_response, IDLE)

    // 入口
    entry: IDLE
```

注意：`.sm` 文件的具体语法需要根据 gem5 SLCc 框架的实际规范调整。上述是骨架代码，实际实现时需要参考 gem5 源码中 `src/mem/ruby/protocol/CHI-cache.sm` 的写法。

- [ ] **Step 2: 提交**

```bash
git add gem5_integration/our_l2.sm
git commit -m "feat: add SLCc state machine skeleton for OurL2"
```

---

## Task 11: gem5 集成 — SimObject 配置

**Files:**
- Create: `gem5_integration/OurL2.py`

- [ ] **Step 1: 创建 OurL2.py**

```python
# gem5_integration/OurL2.py
from m5.params import *
from m5.objects.ClockedObject import ClockedObject

class OurL2(ClockedObject):
    type = "OurL2"
    cxx_header = "mem/ruby/protocol/our_l2.hh"
    cxx_class = "gem5::OurL2"

    # 端口
    system_port = RequestPort("System port")

    # 参数
    num_entries = Param.Int(64, "Number of in-flight entries")
```

注意：`cxx_header` 和 `cxx_class` 的路径需要根据 gem5 的实际目录结构调整。

- [ ] **Step 2: 提交**

```bash
git add gem5_integration/OurL2.py
git commit -m "feat: add OurL2 SimObject configuration"
```

---

## Task 12: gem5 集成 — SConscript 构建注册

**Files:**
- Create: `gem5_integration/SConscript`

- [ ] **Step 1: 创建 SConscript**

```python
# gem5_integration/SConscript
Import('*')

# 注册 SLCc 状态机
SimProtocol('our_l2')

# 链接 CHI-new 静态库
chi_new_lib = Dir('/home/zhangkai/work/CHI-new/build')

env.Append(LIBPATH=[chi_new_lib])
env.Append(LIBS=['cache_model', 'chi_model', 'middleware'])
```

注意：路径需要根据实际构建目录调整。gem5 的 SConscript 语法可能需要微调。

- [ ] **Step 2: 提交**

```bash
git add gem5_integration/SConscript
git commit -m "feat: add SConscript for gem5 build registration"
```

---

## Task 13: 验证程序 — test_128kb.cc

**Files:**
- Create: `test/test_128kb.cc`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: 创建 test_128kb.cc**

```cpp
// test/test_128kb.cc
// 128KB 连续访问验证程序
// 用于 gem5 SE 模式端到端验证

#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr size_t SIZE = 128 * 1024;  // 128KB
constexpr size_t ELEMENTS = SIZE / sizeof(uint64_t);

static uint64_t memory[SIZE / sizeof(uint64_t)];

uint64_t computeChecksum(const uint64_t* data, size_t count) {
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += data[i];
    }
    return sum;
}

int main() {
    // 顺序写入
    for (size_t i = 0; i < ELEMENTS; i++) {
        memory[i] = static_cast<uint64_t>(i);
    }

    // 顺序读取并计算校验和
    uint64_t checksum = computeChecksum(memory, ELEMENTS);

    // 预期校验和：sum(0..N-1) = N*(N-1)/2
    uint64_t expected = (ELEMENTS * (ELEMENTS - 1)) / 2;

    printf("Elements: %lu\n", ELEMENTS);
    printf("Checksum: %lu\n", checksum);
    printf("Expected: %lu\n", expected);

    if (checksum == expected) {
        printf("PASS\n");
        return 0;
    } else {
        printf("FAIL\n");
        return 1;
    }
}
```

- [ ] **Step 2: 更新 test/CMakeLists.txt**

确认包含：

```cmake
add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)
```

- [ ] **Step 3: 编译验证程序（本机）**

```bash
cd /home/zhangkai/work/CHI-new/build
cmake .. && make test_128kb
./test_128kb
```

Expected:
```
Elements: 16384
Checksum: 134215680
Expected: 134215680
PASS
```

- [ ] **Step 4: 提交**

```bash
git add test/test_128kb.cc test/CMakeLists.txt
git commit -m "feat: add 128KB sequential access verification program"
```

---

## Task 14: aarch64 交叉编译

**Files:**
- Create: `test/aarch64-toolchain.cmake`

- [ ] **Step 1: 创建 CMake 工具链文件**

```cmake
# test/aarch64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

- [ ] **Step 2: 验证 aarch64 交叉编译工具链可用**

```bash
which aarch64-linux-gnu-g++
```

Expected: 路径输出（如 `/usr/bin/aarch64-linux-gnu-g++`）

- [ ] **Step 3: 交叉编译 test_128kb**

```bash
cd /home/zhangkai/work/CHI-new
mkdir -p build-aarch64 && cd build-aarch64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../test/aarch64-toolchain.cmake
make test_128kb
```

Expected: `test_128kb` 为 aarch64 二进制

```bash
file test_128kb
```

Expected: `ELF 64-bit LSB executable, ARM aarch64`

- [ ] **Step 4: 提交**

```bash
git add test/aarch64-toolchain.cmake
git commit -m "feat: add aarch64 cross-compilation toolchain file"
```

---

## Task 15: gem5 SE 模式运行配置

**Files:**
- Create: `test/run_gem5_se.py`

- [ ] **Step 1: 创建 gem5 SE 模式运行脚本**

```python
# test/run_gem5_se.py
# gem5 SE 模式配置：单核 aarch64，L2 使用 OurL2

import m5
from m5.objects import *

# 系统配置
system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# CPU
system.cpu = TimingSimpleCPU()

# L1 Cache
system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()

# L2 Cache（使用 OurL2 替代 gem5 原生 L2）
# 注意：实际集成时需要根据 Ruby CHI 网络配置调整
system.l2 = OurL2()

# 内存
system.mem_ctrl = DDR3_1600_8x8()
system.mem_ctrl.port = system.l2.port

# 进程配置
process = Process()
process.cmd = ['test_128kb']
system.cpu.workload = process
system.cpu.createThreads()

# 根配置
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {event.getCause()}")
```

注意：此脚本是骨架代码，实际运行时需要根据 gem5 Ruby CHI 网络的实际配置方式调整 L2 接入方式。

- [ ] **Step 2: 提交**

```bash
git add test/run_gem5_se.py
git commit -m "feat: add gem5 SE mode run configuration skeleton"
```

---

## Task 16: 全量编译验证

- [ ] **Step 1: 清理并全量编译 CHI-new**

```bash
cd /home/zhangkai/work/CHI-new
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
```

Expected: 所有静态库和测试程序编译成功

- [ ] **Step 2: 运行所有单元测试**

```bash
cd /home/zhangkai/work/CHI-new/build
./test_channel
./test_hn_node
./test_128kb
```

Expected: 全部 PASS

- [ ] **Step 3: 交叉编译验证**

```bash
cd /home/zhangkai/work/CHI-new
rm -rf build-aarch64 && mkdir build-aarch64 && cd build-aarch64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../test/aarch64-toolchain.cmake
make -j$(nproc)
```

Expected: aarch64 二进制编译成功

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "chore: verify full build (native + aarch64 cross-compile)"
```

---

## 自检清单

- [ ] 所有文件路径具体且准确
- [ ] 无 TBD/TODO 占位符
- [ ] 类型名称一致（ChiTransaction, HnNode, SimpleL2Cache 等）
- [ ] Opcode 集一致（9 个）
- [ ] ChiTransaction 字段一致（含 srcNodeID, returnTxnID）
- [ ] 每个 Task 有编译/测试验证步骤
- [ ] TDD 测试先于或与实现同步
- [ ] 提交信息清晰描述变更
