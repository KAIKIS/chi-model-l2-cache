## Context

当前项目已完成 Phase 1：透传 L2 Cache + CHIGenericController gem5 集成。SimpleL2Cache 继承 HnNode，不做任何缓存操作，所有请求直接到内存。gem5 配置为单核（num_cores=1），通过 SimplePt2Pt 网络连接 RN-F → HN-F → SN-F。

下一阶段需要将透传 L2 升级为真正带存储的 Cache，支持 2-core shared L2，并实现完整的 CHI 一致性状态模型。

## Goals / Non-Goals

**Goals:**
- 实现带 tag/data 存储的 L2 Cache（组相联 + LRU 替换）
- 实现 CHI 完整一致性状态模型（I/UC/SC/UD/SD）
- 支持 2-core shared L2 架构（2 个 RN-F 共享 1 个 HN-F）
- 实现 snoop 操作（SnpCleanInvalid）维护多核一致性
- 编写双核测试程序验证正确性

**Non-Goals:**
- 不实现 cycle-accurate 时序模型（保持 transaction-level）
- 不实现 flit-level CHI 协议
- 不支持超过 2 核
- 不实现 DMT/DCT 高级传输模式的完善（保持 Phase 1 的 DMT 基本支持）
- 不实现 PCrdGrant/RetryAck 流控机制
- 不实现 ReadOnce、WriteUnique 等非缓存操作

## Decisions

### Decision 1: 同步模型替代 channel 异步模型

**选择**: OurL2Middleware 直接在 gem5 回调线程中同步执行 cache 逻辑，不再通过 HnNode 的 channel + process loop。

**原因**:
- CHIGenericController 的 4 通道回调（recvRequestMsg/recvResponseMsg/recvDataMsg/recvSnoopMsg）本身就是同步入口
- Cache 的 in-flight 事务可以用 `std::unordered_map<TxnID, PendingTxn>` 管理，比 channel + 线程 + 回调三层异步更直观
- gem5 事件驱动模型天然适合同步 cache 操作：收到请求 → lookup → hit 直接返回 / miss 发请求到内存 → 收到内存数据 → fill → 返回

**替代方案**: 继续用 HnNode 的 channel 模型，引入状态机跟踪 in-flight 事务。复杂度过高，且与 gem5 wakeup 模型不完全匹配。

**影响**: HnNode 的 channel 模型保留给纯协议仿真场景（不依赖 gem5），OurL2Middleware 不再使用 HnNode。

### Decision 2: Cache 数据结构设计

**选择**: 在 cache_model 中实现独立的 CacheLine/CacheSet/L2Cache 类，OurL2Middleware 持有 L2Cache 实例。

```
CacheLine {
    tag:     uint64_t           // 地址高位
    state:   LineState          // I/UC/SC/UD/SD
    data:    uint8_t[64]        // 64 字节数据
    sharers: std::set<NodeID>   // 持有副本的 RN 列表
}

CacheSet {
    lines[8]                    // 8-way 组相联
    lru: std::array<int, 8>     // LRU 计数器
}

L2Cache {
    sets[512]                   // 512 组，总容量 512×8×64 = 256KB
    pendingTxns: unordered_map<TxnID, PendingTxn>
}
```

**原因**: 独立的 cache 数据结构不依赖 gem5，可独立单元测试。OurL2Middleware 作为 gem5 回调层调用 L2Cache 的 lookup/fill/evict 接口。

### Decision 3: 2-Core 网络配置

**选择**: 修改 `our_l2_hierarchy.py`，将 `num_cores=1` 改为 `num_cores=2`。Python 配置的循环结构已天然支持多核。

**NodeID 分配**:
| 控制器 | NodeID |
|--------|--------|
| core0.dcache | 0 |
| core0.icache | 1 |
| core1.dcache | 2 |
| core1.icache | 3 |
| OurL2Middleware | 4 |
| MemoryController | 5 |

**原因**: gem5 的 AbstractNode._version 自增机制保证 NodeID 唯一。消息中已携带 requestor MachineID，OurL2Middleware 可自动区分请求来源。

### Decision 4: Snoop 操作范围

**选择**: 只实现 SnpCleanInvalid（gem5 中等价于 CHI 的 SnpInvalidate），不实现 SnpShared/SnpUnique 等。

**gem5 API 映射**: gem5 CHI 实现中没有 `SnpInvalidate`，使用 `CHIRequestType_SnpCleanInvalid` 代替。通过 `sendSnoopMsg()` 发送，消息类型为 `CHIRequestMsg`。

**原因**: 对于 2-core 场景，SnpCleanInvalid 足以覆盖所有写一致性场景：
- ReadUnique（写操作）需要 invalidate 所有其他 sharers → SnpCleanInvalid
- CleanUnique 需要 invalidate sharers 并可能写回脏数据 → SnpCleanInvalid + WriteBack

SnpShared（读共享时通知其他 RN 降级）在 2-core 场景下不是必需的，因为 ReadShared 不需要其他 RN 做任何操作。

### Decision 6: 禁用 DMT 模式

**选择**: OurL2Middleware 发送 ReadNoSnp 到内存时，设置 `fwdRequestor=m_machineID` 和 `dataToFwdRequestor=false`，让内存把数据发回 HN-F 而不是直接发给 L1。

**原因**: 当前 Phase 1 使用 DMT（Direct Memory Transfer），内存直接把数据发给 L1，绕过 HN-F。这对 cache 来说是错误的 — HN-F 必须收到数据才能 fill 到 cache 中。禁用 DMT 后，数据流变为：

```
L1 → HN-F(lookup miss) → SN-F(read)
SN-F → HN-F(fill cache) → L1(return data)
```

**gem5 API**: 发送 ReadNoSnp 时：
```cpp
req->setfwdRequestor(m_machineID);      // 数据发回给自己
req->setdataToFwdRequestor(false);       // 不走 DMT
```

### Decision 5: 双核测试程序架构

**选择**: 两个独立的 aarch64 二进制（test_core0、test_core1），gem5 配置中通过 `--cmd0` 和 `--cmd1` 分别指定。

**替代方案**: 单二进制 + MPIDR_EL1 区分 core ID。在 SE 模式下复杂度较高，需要汇编启动代码。

**测试场景**:
1. 共享读：Core0 写 → Core1 读验证
2. 写冲突：Core0 写 → Core1 写同一地址 → Core0 读验证最新值
3. 驱逐压力：访问超过 L2 容量的数据，验证驱逐后数据正确性

**同步机制**: 使用自旋等待 + 共享内存标志位（简单、无需 OS 支持）。

## Risks / Trade-offs

**[Risk] CHIGenericController 的 recvSnoopMsg 在当前实现中是 no-op**
→ 需要在 OurL2Middleware 中实现 snoop 消息的构造和发送。gem5 的 CHI 网络支持 HN-F 向 RN-F 发送 snoop 消息，但需要确认 NetDest 设置正确。

**[Risk] 2-core 下 L1 的 MOESI 状态与 L2 的 CHI 状态可能存在映射问题 — 已解决**
→ CHI 协议本身就是 L1 MOESI 和 L2 CHI 之间的抽象层。L1 的 SLICC 状态机已经把 MOESI 行为翻译成了 CHI 请求（ReadShared/ReadUnique/WriteBackFull 等），L2 只需要根据请求类型（opcode）决定 CHI 状态转换，不需要关心 L1 内部的 MOESI 状态。

**[Risk] PendingTxn 管理的复杂度 — 已解决**
→ 经调研确认：gem5 SE 模式是单线程事件队列，所有回调（recvResponseMsg → recvDataMsg → recvSnoopMsg → recvRequestMsg）严格串行执行。当前 wakeup() 发出的消息在下一个 wakeup() 才可见（最小延迟 1 cycle）。pendingTxns 不需要加锁，与 gem5 自带的 TLM CacheController 用法一致。

**[Trade-off] L2 256KB，L1 32KB**
→ L2 为 512 组 × 8-way × 64B = 256KB，L1 为 32KB。L2 比 L1 大 8 倍，符合典型层次结构。驱逐压力测试需要访问超过 256KB 才能触发 L2 驱逐。

**[Trade-off] 不实现流控（PCrdGrant/RetryAck）**
→ 简化了实现，但无法模拟真实的 CHI 流控行为。在 2-core 低并发场景下影响不大。
