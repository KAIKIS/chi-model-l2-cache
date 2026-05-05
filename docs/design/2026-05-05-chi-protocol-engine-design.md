## Context

当前项目有一个独立的 CHI Model（chi_model/）、一个独立的 L2Cache（cache_model/）和一个已工作的 gem5 中间件（gem5/src/mem/my_l2/）。但三层之间的关系存在架构问题：

1. **CHI Model 采用了异步 channel + 线程架构**，与 gem5 的单线程事件驱动模型不兼容，被设计文档明确放弃使用，成为死代码。
2. **OurL2Middleware** 包含了 ~600 行 CHI 协议处理逻辑，直接在 gem5 回调中操作 L2Cache，绕过了 CHI Model。
3. **L2Cache** 是纯数据结构，不理解 CHI 协议，调用者需要自行理解 CHI 语义。

目标：重构为清晰的三层架构 —— Middleware（gem5 消息翻译）→ Cache = 协议层（CHI 协议引擎）+ 微架构层（L2Cache 存储）。

## Goals / Non-Goals

**Goals:**
- 实现同步的 ChiProtocolEngine，成为 CHI 协议的唯一决策者
- Middleware 简化为纯消息格式翻译 + 命令执行
- 删除所有死代码（HnNode、Channel、SimpleL2Cache、chi_middleware）
- ChiProtocolEngine 可独立于 gem5 进行单元测试

**Non-Goals:**
- 不改变 L2Cache 的接口和行为
- 不增加新的 CHI 操作码或协议特性
- 不改变 gem5 侧的连线（OurL2Middleware 仍然继承 CHIGenericController）

## Architecture

### 分层关系

```
┌─────────────────────────────────────────────┐
│  gem5 (CHIGenericController)                │
│  recvRequestMsg / recvDataMsg / ...         │
└──────────────────┬──────────────────────────┘
                   │ gem5 消息类型 (CHIRequestMsg/CHIDataMsg/...)
                   │ 数据节拍 ↔ 连续 buffer
┌──────────────────┴──────────────────────────┐
│  Middleware (OurL2Middleware)               │
│  - CHIRequestMsg ↔ ChiTransaction           │
│  - MachineID ↔ NodeID                       │
│  - ProtocolAction → gem5 send* 方法          │
│  - 0 协议逻辑                                │
└──────────────────┬──────────────────────────┘
                   │ ChiTransaction / ProtocolAction
┌──────────────────┴──────────────────────────┐
│  Cache                                        │
│  ┌──────────────────────────────────────────┐ │
│  │  协议层 (ChiProtocolEngine)             │ │
│  │  - 接收请求 → lookup → 协议决策 → Action │ │
│  │  - 管理 pending 事务状态机               │ │
│  │  - 管理 snoop 流程                       │ │
│  │  - 0 依赖 gem5                           │ │
│  └────────────────┬─────────────────────────┘ │
│                   │ lookup / fill / invalidate │
│  ┌────────────────┴─────────────────────────┐ │
│  │  微架构层 (L2Cache)                      │ │
│  │  - CacheLine / CacheSet / LRU            │ │
│  │  - 纯数据操作，无协议逻辑                 │ │
│  └──────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

**Cache = 协议层 + 微架构层。** Middleware 与 Cache 之间通过 ChiTransaction 和 ProtocolAction 对话。Cache 内部协议层调用微架构层。

### 数据流（ReadShared miss 示例）

```
Middleware                    ChiProtocolEngine              L2Cache
   │                              │                            │
   ├─recvRequest(ReadShared)─────>│                            │
   │                              ├─lookup(addr)──────────────>│
   │                              │<─Miss──────────────────────│
   │<─[SendReadNoSnp(addr,id)]───│                            │
   │                              │                            │
   ├─sendReadNoSnp via gem5      │                            │
   │  ...wait gem5 callback...    │                            │
   │                              │                            │
   ├─recvData(txnId, data)──────>│                            │
   │                              ├─fill(addr, SC, data)──────>│
   │                              ├─addSharer(addr, node)─────>│
   │<─[SendCompData(...)]────────│                            │
   │                              │                            │
   ├─sendCompData via gem5       │                            │
```

## ChiProtocolEngine Design

### 入口点

```cpp
class ChiProtocolEngine {
public:
    ChiProtocolEngine(L2Cache* cache);

    std::vector<ProtocolAction> recvRequest(const ChiTransaction& txn);
    std::vector<ProtocolAction> recvData(TxnID txnId, const uint8_t* data);
    std::vector<ProtocolAction> recvResponse(TxnID txnId, RespStatus status);
};
```

三个入口对应 Middleware 的三类 gem5 回调：
- `recvRequest` ← gem5 `recvRequestMsg`（RN-F 发来的请求）和 `recvSnoopMsg`（外部 snoop，当前单 HN-F 下为空操作）
- `recvData` ← gem5 `recvDataMsg`（内存返回数据、L1 写回数据、snoop 写回数据）
- `recvResponse` ← gem5 `recvResponseMsg`（CompAck、WriteAck、snoop 响应、Comp）

每次调用返回 `vector<ProtocolAction>`。**空 vector 表示事务仍在等待外部事件（内存响应 / snoop 响应），无需 Middleware 动作。**

### ProtocolAction

```cpp
struct ProtocolAction {
    enum Type {
        SendReadNoSnp,        // 发读请求到内存
        SendWriteNoSnp,       // 发写请求到内存
        SendCompData,         // 发带数据响应给 RN-F
        SendComp,             // 发无数据响应给 RN-F
        SendCompDBIDResp,     // 通知 L1 发送写回数据
        SendSnpCleanInvalid,  // 发 snoop 给 RN-F
    };

    Type      type;
    Addr      addr;
    TxnID     txnId;
    NodeID    destNode;
    LineState respState;                 // CompData 时的缓存行状态
    bool      retToSrc;                  // Snoop 时是否需要回传数据
    uint8_t   data[CACHE_LINE_SIZE];     // 响应/写回数据
};
```

Middleware 遍历返回的 Action 列表，依次翻译为 gem5 消息发送：

| ProtocolAction | gem5 操作 |
|---|---|
| SendReadNoSnp | sendRequestMsg(CHIRequestType_ReadNoSnp, ...) |
| SendWriteNoSnp | sendRequestMsg(CHIRequestType_WriteNoSnp) + sendDataMsg(NCBWrData) |
| SendCompData | sendDataMsg(CompData_SC/UC/UD_PD) in beats |
| SendComp | sendResponseMsg(Comp_UC/Comp, ...) |
| SendCompDBIDResp | sendResponseMsg(CompDBIDResp, ...) |
| SendSnpCleanInvalid | sendSnoopMsg(SnpCleanInvalid, ...) |

### 内部 pending 事务

Engine 维护 `unordered_map<TxnID, PendingTxn>` 跟踪多步骤事务：

```cpp
struct PendingTxn {
    ChiTransaction origReq;     // 原始请求
    PendingState  state;        // WaitMemData / WaitSnoopResp / WaitMemWriteAck
    LineState     targetState;  // 完成后缓存行目标状态
    int           pendingSnoopCount;
    int           pendingSnoopDataCount;
    TxnID         origTxnId;    // 关联的 L1 事务 ID
};
```

### 协议处理逻辑（从当前 OurL2Middleware 提取）

Engine 按 opcode 分发：

| Opcode | 逻辑 |
|--------|------|
| ReadShared | lookup → SC/UC hit: SendCompData 直接返回; UD/SD 有其它 sharer: SendSnpCleanInvalid; miss: SendReadNoSnp |
| ReadUnique | lookup → UC/UD hit 无其它 sharer: 转换 UD + SendCompData; 有其它 sharer: SendSnpCleanInvalid; miss: SendReadNoSnp |
| CleanUnique | lookup → UC hit: SendComp(Comp_UC); UD hit: SendWriteNoSnp; SC/SD hit 有其它 sharer: SendSnpCleanInvalid |
| WriteBackFull | SendCompDBIDResp + 记录 pending 等待 L1 数据 |
| Evict (gem5 特有) | invalidate，返回空 Action（消费） |

### 状态机回调

snoop 响应到达时，Engine 递减 pendingSnoopCount / pendingSnoopDataCount。全部收齐后：
- ReadShared/ReadUnique snoop 完成: 设置目标状态，添加新 sharer，SendCompData
- CleanUnique snoop 完成: SendComp(Comp_UC)

内存数据到达时：
- ReadShared/ReadUnique miss: fill cache，添加 sharer，SendCompData
- CleanUnique miss: fill cache (UC)，SendComp(Comp_UC)

## Middleware Changes

Middleware 简化为四个职责：

1. **消息翻译**：`gem5ToOpcode()` + `ChiTransaction` 构造 / 解构
2. **调用 Engine**：`engine_->recvRequest(txn)` → 获得 Action 列表
3. **执行 Action**：遍历列表，调用对应的 `send*` gem5 方法
4. **数据节拍管理**：gem5 数据按 beat 到达，需收集完整 cache line 后再调用 `engine_->recvData()`

Middleware **不再持有** `l2Cache_` 指针，只持有 `chiProtocolEngine_`。

## File Changes

### 删除（死代码清理）

| 文件 | 原因 |
|------|------|
| chi_model/include/chi_channel.hh | 异步通道，gem5 不兼容 |
| chi_model/include/chi_node.hh | 抽象基类，仅 HnNode 使用 |
| chi_model/include/chi_hn_node.hh | 异步线程模型，已被 Engine 替代 |
| chi_model/src/chi_hn_node.cc | 同上 |
| cache_model/include/simple_l2_cache.hh | Phase 1 透传 L2，已被 L2Cache 替代 |
| cache_model/src/simple_l2_cache.cc | 同上 |
| middleware/include/chi_middleware.hh | 无人调用 |
| middleware/src/chi_middleware.cc | 同上 |
| test/test_channel.cc | 测试死代码 |
| test/test_hn_node.cc | 测试死代码 |

### 新建

| 文件 | 说明 |
|------|------|
| chi_model/include/chi_protocol_engine.hh | 协议引擎头文件 |
| chi_model/src/chi_protocol_engine.cc | 协议引擎实现（从 OurL2Middleware 提取协议逻辑） |
| test/test_protocol_engine.cc | 独立单元测试，不依赖 gem5 |

### 修改

| 文件 | 变更 |
|------|------|
| chi_model/include/chi_transaction.hh | 删除 isRequest/isResponse/needsSNForward 及 chi_channel.hh include |
| chi_model/src/chi_transaction.cc | 删除三个方法实现 |
| chi_model/CMakeLists.txt | chi_hn_node → chi_protocol_engine |
| cache_model/CMakeLists.txt | 删除 simple_l2_cache，删除 chi_model 链接 |
| CMakeLists.txt | 删除 middleware 子目录 |
| test/CMakeLists.txt | 删除旧测试，添加 test_protocol_engine |
| gem5/src/mem/my_l2/our_l2_middleware.hh | 用 ChiProtocolEngine 替换 L2Cache |
| gem5/src/mem/my_l2/our_l2_middleware.cc | 删除协议逻辑，委托给 Engine；增加数据节拍收集 |

### 保留不变

| 文件 | 说明 |
|------|------|
| chi_model/include/chi_types.hh | NodeID/TxnID/Addr/RespStatus |
| chi_model/include/chi_opcode.hh | Opcode 枚举 |
| chi_model/include/chi_log.hh | 日志系统 |
| chi_model/include/chi_transaction.hh | ChiTransaction 数据结构（精简后） |
| cache_model/include/cache_line.hh | CacheLine/CacheSet/LRU/LineState |
| cache_model/include/l2_cache.hh | L2Cache 类声明 |
| cache_model/src/l2_cache.cc | L2Cache 实现 |
| test/test_l2_cache.cc | L2Cache 测试 |
| test/test_dual_core.cc | 双核一致性测试 |
| test/our_chi_config.py | gem5 配置 |
| gem5_integration/OurL2.py | SimObject 定义 |

### 依赖关系检查

```
chi_model/  → 0 外部依赖（纯 CHI 类型 + 引擎）
cache_model/ → chi_model (include chi_types.hh, chi_log.hh)
test/        → chi_model + cache_model
gem5/ (MyL2) → chi_model + cache_model + gem5
```

`cache_model/` 不再链接 `chi_model`（只 include `chi_types.hh` 和 `chi_log.hh`，都是 header-only）。

## Test Plan

### test_protocol_engine.cc（独立单元测试）

| 用例 | 操作 | 期望 |
|------|------|------|
| ReadShared SC hit | recvRequest(ReadShared, addr已填充SC) | 1 Action: SendCompData(state=SC) |
| ReadShared UC hit | recvRequest(ReadShared, addr已填充UC, 1 sharer) | 1 Action: SendCompData(state=SC), cache→SC |
| ReadShared miss | recvRequest(ReadShared, addr不在cache) | 1 Action: SendReadNoSnp |
| ReadShared miss→fill→response | recvRequest + recvData | recvData后: SendCompData(state=SC) |
| ReadShared UD hit with sharer | recvRequest(ReadShared, addr=UD, 2 sharer) | 2 Action: SendSnpCleanInvalid(retToSrc=true) |
| ReadShared snoop完成 | recvRequest→snoop→recvResponse×2 | SendCompData(state=SC) |
| ReadUnique miss | recvRequest(ReadUnique, miss) | 1 Action: SendReadNoSnp |
| ReadUnique UD hit sole sharer | recvRequest(ReadUnique, UD, only requestor) | 1 Action: SendCompData(state=UD) |
| ReadUnique SC hit with other sharers | recvRequest(ReadUnique, SC, 2 other sharer) | 2 Action: SendSnpCleanInvalid |
| CleanUnique UC hit | recvRequest(CleanUnique, UC) | 1 Action: SendComp(Comp_UC) |
| CleanUnique SD hit with other sharer | recvRequest(CleanUnique, SD) | SendSnpCleanInvalid |
| CleanUnique miss | recvRequest(CleanUnique, miss) | SendReadNoSnp |
| WriteBackFull | recvRequest(WriteBackFull) | SendCompDBIDResp |
| Evict | recvRequest(Evict) | 空 Action, cache line invalidated |
| Evict后lookup miss | recvRequest(Evict) + recvRequest(ReadShared, same addr) | Evict:空; ReadShared: SendReadNoSnp(miss) |
| Snoop数据带写回 | recvRequest(ReadShared, UD) → snoop → recvData(snoop) → recvResponse | recvData后更新cache, recvResponse后完成 |

### 已有测试继续通过

- `test_l2_cache` — 不受影响，L2Cache 接口不变
- `test_dual_core` — 不受影响，裸机二进制

## Implementation Deviations (from gem5 integration testing)

### 1. recvData 增加 addr 参数

原设计 `recvData(TxnID, const uint8_t*)`。实现中增加了 `Addr addr` 参数，用于 WriteBackFull 数据到达时的按地址回退查找。gem5 的 L1 发送 WriteBackFull 数据时 txnId 可能与原始请求不一致，Engine 通过地址查找 `WaitL1Data` 状态的 pending 条目。

### 2. WriteBackFull 吸收策略

原设计：收到 L1 写回数据后发送 WriteNoSnp 写入内存，然后 invalidate L2 行。

实际实现：将脏数据直接保留在 L2 (UD 状态)，不发送 WriteNoSnp。原因：WriteNoSnp + NCBWrData 在同 tick 发送时，Memory Controller 可能先收到 DAT 通道的 NCBWrData 再收到 REQ 通道的 WriteNoSnp，导致 `state: READY event: WriteData` 的非法转换。L2 保留脏数据是有效的 CHI 行为（HN-F 作为写回缓存）。

**影响：** 脏数据仅在 L2 保留，未写入内存。若 L2 后续逐出脏行，需处理 MissEvictDirty（当前未实现，有断言保护）。

### 3. Evict 需发送 Comp_I 响应

gem5 的 L1 发送 Evict 后期望 `Comp_I` 响应确认完成。Middleware 在 invalidate L2 行后额外调用 `sendComp(..., Comp_I, ...)`。原设计仅提到 consume Evict。

### 4. CompAck 直接消费

Middleware 在 `recvResponseMsg` 中对 `CompAck` 直接返回 true，不传给 Engine。Engine 不跟踪 CompAck（它是 gem5 内部的握手机制）。

### 5. pending_ 混合键策略

`pending_` map 使用两种键：
- `memId`（≥20000）：内存操作（ReadNoSnp/WriteNoSnp），保证不与 snoop 条目冲突
- `txn.txnID`（原始 L1 事务 ID）：snoop 操作和 WriteBackFull 的 WaitL1Data 阶段

原设计假设全部使用 `txn.txnID` 作为键，但 gem5 L1 复用 txnId 会导致冲突。

### 6. MissEvictDirty 未实现

当 L2 cache 满且 LRU 受害者是脏行时，`lookup()` 返回 `MissEvictDirty`。当前 Engine 在三个 miss 路径中均有 `assert(resp.result != LookupResult::MissEvictDirty)` 保护，并在 `handleReadShared::Miss` 块中有详细的 TODO 说明实现步骤。当前 L2 (256KB) 大于测试工作集 (128KB)，不会触发此条件。
