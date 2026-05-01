# ARM CHI 协议模型 + L2 Cache 设计文档

## 概述

实现一个可复用的 ARM CHI 协议模型（Transaction 级），并通过简易 L2 Cache 模型（直通模式）验证其正确性。最终以源码集成方式嵌入 gem5 仿真器，替代 gem5 原生 L2 Cache 控制器。

## 目标 / 非目标

**目标：**
- 实现 Transaction 级 CHI 协议模型，可独立于 gem5 使用
- 实现直通模式 L2 Cache 模型，验证 CHI 模型正确性
- 最小化 gem5 代码修改（仅新增文件）
- 支持 128KB 连续访问用例的端到端验证

**非目标：**
- 不实现 cycle-accurate 时序模型
- Phase 1 不支持多核一致性（无 snoop）
- 不实现 L2 缓存行为（Phase 1 为直通模式）
- 不修改 gem5 原有代码

## 架构总览

```
gem5 进程
┌──────────────────────────────────────────────────────┐
│  gem5 Ruby CHI                                       │
│  ┌──────┐  ┌─────┐        ┌──────────┐  ┌────────┐  │
│  │ CPU  │─▶│ L1  │◀══════▶│ our-l2.sm│──▶│ Memory │  │
│  │      │  │(RN) │  网络   │ (SLCc)   │  │ (SN)   │  │
│  └──────┘  └─────┘        └────┬─────┘  └────────┘  │
│                                │ Channel              │
│  ┌─────────────────────────────┼───────────────────┐  │
│  │  CHI-new 静态库              │                   │  │
│  │  ┌──────────────┐  ┌───────┴───────┐           │  │
│  │  │  HnNode      │◀▶│  Middleware   │           │  │
│  │  │  (9 opcodes) │  │  (直接映射)    │           │  │
│  │  └──────────────┘  └───────────────┘           │  │
│  └─────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

## 模块设计

### 1. CHI 模型（chi_model）

CHI 模型是独立于 gem5 的通用协议引擎，提供 Transaction 数据结构、Opcode 枚举、Channel 通信和 HN-F 节点基类。

#### 1.1 ChiTransaction 结构体

```cpp
struct ChiTransaction {
    TxnID       txnID;          // 事务标识符
    Opcode      opcode;         // CHI opcode
    Addr        addr;           // 物理地址
    uint32_t    size;           // 传输大小（字节）
    NodeID      srcNodeID;      // 来源 RN 节点 ID
    TxnID       returnTxnID;    // 写响应引用的原始请求 ID
    std::vector<uint8_t> data;  // 数据载荷
    RespStatus  respStatus;     // 响应状态
};
```

#### 1.2 Opcode 枚举（9 个）

请求方向：
- `ReadShared` — RN 读共享
- `ReadUnique` — RN 读独占
- `CleanUnique` — RN 清理独占
- `WriteBackFull` — RN 写回

SN 请求方向：
- `ReadNoSnp` — 无 snoop 读
- `WriteNoSnp` — 无 snoop 写

响应方向：
- `CompData` — 数据完成响应
- `WriteAck` — 写确认
- `Comp` — 完成响应（无数据）

#### 1.3 Channel<T> 模板类

```cpp
template<typename T>
class Channel {
public:
    void push(T item);   // 阻塞式发送
    T pop();             // 阻塞式接收
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};
```

Channel 用于 HnNode 与 SLCc 之间的通信，天然支持异步处理。

#### 1.4 HnNode 基类

HnNode 继承自 ChiNode 基类，包含 RN 侧和 SN 侧的 Channel，以及 `process()` 主循环。

**Opcode 转换逻辑（直通模式）：**

| RN 请求 | → | SN 请求/响应 |
|---|---|---|
| ReadShared | → | ReadNoSnp |
| ReadUnique | → | ReadNoSnp |
| CleanUnique | → | Comp（无 SN 请求） |
| WriteBackFull | → | WriteNoSnp |

### 2. L2 Cache 模型（cache_model）

SimpleL2Cache 继承 HnNode，以直通模式运行（无缓存状态）。

**职责：**
- 接收来自 RN 的请求
- 调用 HnNode 的 opcode 转换逻辑
- 将转换后的请求发送到 SN
- 接收 SN 响应并转发回 RN

### 3. gem5 集成层

#### 3.1 SLCc 状态机（our_l2.sm）

SLCc 控制器维护一个 **in-flight 跟踪表**，记录已转发但尚未收到响应的事务。

**InFlightEntry 结构：**
```cpp
struct InFlightEntry {
    TxnID   rnTxnID;       // RN 侧事务 ID
    TxnID   snTxnID;       // SN 侧分配的事务 ID
    NodeID  srcNodeID;      // 来源 RN
    Opcode  originalOp;     // 原始 opcode（用于响应映射）
    Addr    addr;           // 地址（调试/查找用）
    enum State { SEND_SN, WAIT_SN_RESP, SEND_RN_RESP } state;
};
```

**表大小：** 固定 64 条目，线性扫描查找。

**ReadShared 请求流程：**
1. SLCc 从 RN 收到 ReadShared
2. 分配 InFlightEntry（state = SEND_SN）
3. 通过 Middleware 转换为 ReadNoSnp → 推入 Channel
4. HnNode 处理并转发到 SN
5. SN 以 CompData 响应 → Channel 送达 SLCc
6. SLCc 按 `snTxnID` 查找 in-flight 表
7. 转换 CompData，发送给 RN（state = SEND_RN_RESP）
8. 释放 entry

**CleanUnique 流程（无 SN 请求）：**
1. SLCc 收到 CleanUnique
2. 分配 entry，立即发送 Comp 给 RN
3. 无 SN 交互 — 释放 entry

#### 3.2 Middleware 层（chi_middleware）

采用**直接 opcode 映射**——每对 opcode 写一个转换函数。

**gem5 → ChiTransaction（请求方向）：**

| gem5 消息类型 | 转换函数 | 目标 Opcode |
|---|---|---|
| CHIRequestMsg (ReadShared) | `convertReadShared()` | ReadShared |
| CHIRequestMsg (ReadUnique) | `convertReadUnique()` | ReadUnique |
| CHIRequestMsg (CleanUnique) | `convertCleanUnique()` | CleanUnique |
| CHIRequestMsg (WriteBackFull) | `convertWriteBackFull()` | WriteBackFull |

**ChiTransaction → gem5（响应方向）：**

| ChiTransaction Opcode | 转换函数 | gem5 消息类型 |
|---|---|---|
| CompData | `convertCompData()` | CHIDataMsg |
| WriteAck | `convertWriteAck()` | CHIWriteAckMsg |
| Comp | `convertComp()` | CHICompMsg |

每个转换函数负责提取 gem5 消息中的 addr、size、data、srcNodeID 等字段，填入 ChiTransaction 对应字段，以及反向操作。

#### 3.3 SimObject 配置（OurL2.py）和构建注册（SConscript）

- OurL2.py：gem5 SimObject 配置类
- SConscript：注册 SLCc 机器、链接 CHI-new 静态库

### 4. 验证

- **测试程序**：test_128kb.cc — 分配 128KB 内存，顺序写入后顺序读取，输出校验和
- **运行模式**：gem5 SE 模式，单核 aarch64
- **验证点**：程序输出正确校验和 + gem5 stats 中 L2 事务类型和数量符合预期

## 项目结构

```
CHI-new/
├── CMakeLists.txt
├── chi_model/
│   ├── include/
│   │   ├── chi_transaction.hh
│   │   ├── chi_opcode.hh
│   │   ├── chi_node.hh
│   │   ├── chi_hn_node.hh
│   │   ├── chi_channel.hh
│   │   └── chi_types.hh
│   └── src/
│       ├── chi_transaction.cc
│       ├── chi_hn_node.cc
│       └── chi_channel.cc
├── cache_model/
│   ├── include/
│   │   └── simple_l2_cache.hh
│   └── src/
│       └── simple_l2_cache.cc
├── middleware/
│   ├── include/
│   │   └── chi_middleware.hh
│   └── src/
│       └── chi_middleware.cc
├── test/
│   ├── test_128kb.cc
│   └── CMakeLists.txt
└── gem5_integration/
    ├── our_l2.sm
    ├── OurL2.py
    └── SConscript
```

## 决策记录

| 决策 | 选择 | 替代方案 | 理由 |
|---|---|---|---|
| 抽象层次 | Transaction 级 | Flit 级、Cache 状态机级 | 足够覆盖功能验证，映射 gem5 消息更容易 |
| 通信机制 | Channel<T> 模板 | 同步函数调用、共享内存 | 解耦收发方，天然支持异步 |
| gem5 集成 | 新增文件 | 修改 CHI-cache.sm | 最小侵入性，避免合并冲突 |
| L2 架构 | HnNode 内嵌 CHI 模型 | SLCc 直接实现 | 可复用，gem5 侧无需改动 |
| L2 模式 | 直通（Pass-through） | 带缓存 | 最简验证路径，专注协议正确性 |
| Opcode 范围 | 9 个基础 opcode | 扩展集、全集 | 足够覆盖 128KB 测试用例 |
| SLCc 状态 | In-flight 跟踪表 | 单状态、逐条目状态机 | 支持乱序响应，复杂度适中 |
| Middleware | 直接 opcode 映射 | 表驱动转换 | 易调试，每个函数职责明确 |

## 风险 / 权衡

- **SLCc 框架学习成本** → 缓解：Phase 1 极简设计，先跑通再深入
- **gem5 CHI 消息格式变更** → 缓解：Middleware 层隔离格式差异
- **直通模式不测试缓存一致性** → 缓解：Phase 1 明确范围，Phase 2 扩展
- **Channel 通信开销** → 可接受：功能模型不关注性能
