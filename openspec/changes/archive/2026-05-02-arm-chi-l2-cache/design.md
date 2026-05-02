## Context

当前项目需要从零构建一个 ARM CHI 协议模型，并通过 gem5 仿真器进行验证。gem5 已有完整的 CHI 协议实现（SLCc 状态机 + Ruby 网络），但我们需要一个独立的、可复用的 CHI 模型，不依赖 gem5 的实现。

验证场景：单核 CPU 执行 128KB 连续访问程序，L1 Cache 使用 gem5 原生实现，L2 Cache 使用我们自己的实现（直通模式），Memory 使用 gem5 原生实现。

## Goals / Non-Goals

**Goals:**
- 实现 Transaction 级的 CHI 协议模型，可独立于 gem5 使用
- 实现直通模式的 L2 Cache 模型，验证 CHI 模型的正确性
- 以最小化 gem5 代码修改的方式完成集成（仅新增文件）
- 支持 128KB 连续访问用例的端到端验证

**Non-Goals:**
- 不实现 cycle-accurate 时序模型
- Phase 1 不支持多核一致性（无 snoop）
- 不实现 L2 缓存行为（Phase 1 为直通模式）
- 不修改 gem5 原有代码

## Decisions

### 决策 1: CHI 模型抽象层次 — Transaction 级

**选择**: Transaction 级（理解 CHI 事务语义，不模拟 flit 物理传输）

**理由**:
- Flit 级模拟复杂度高，对功能验证无必要
- Transaction 级足够覆盖 Read/Write/Snoop 等全部 CHI 事务类型
- 更容易映射到 gem5 的 Ruby CHI 消息格式

**替代方案**: Flit 级（过于底层），Cache 状态机级（与 L2 Cache 模型耦合）

### 决策 2: 通信机制 — Channel

**选择**: 使用 Channel<T> 模板类作为节点间通信接口

**理由**:
- 解耦发送方和接收方，符合"CHI 模型与 Cache 模型解耦"的需求
- 天然支持异步处理和多线程
- 接口简洁，push/pop 语义清晰

**替代方案**: 同步函数调用（简单但耦合），共享内存（复杂）

### 决策 3: gem5 集成方式 — 新增文件（非修改）

**选择**: 新增 SLCc 控制器文件（.sm + .py），不修改 gem5 原有代码

**理由**:
- 最小化 gem5 侵入性，切换开发环境时只需复制新增文件
- 避免与 gem5 上游代码的合并冲突
- SLCc 壳子工作量可控（~100 行模板代码）

**替代方案**: 修改 CHI-cache.sm（侵入性大，合并困难）

### 决策 4: L2 Cache 架构 — HN-F 节点内嵌于 CHI 模型

**选择**: CHI 模型包含 HnNode 基类，L2 Cache 模型继承并实现具体行为

**理由**:
- HnNode 作为 CHI 模型的一部分，后续扩展缓存逻辑时 gem5 侧无需改动
- L2 Cache 模型只需关注业务逻辑（直通/缓存），通信由 CHI 模型的 Channel 负责

**替代方案**: HnNode 逻辑直接写在 SLCc 控制器中（耦合 gem5，不可复用）

### 决策 5: L2 Cache 模式 — 直通（Pass-through）

**选择**: Phase 1 为直通模式，不维护缓存状态

**理由**:
- 最简化验证路径，专注于 CHI 协议正确性
- 直通模式下 HnNode 的核心逻辑仅是 opcode 转换和转发
- 后续可扩展为带缓存的 HN-F，接口不变

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
│  │  │  (CHI Model) │  │  (格式转换)    │           │  │
│  │  └──────────────┘  └───────────────┘           │  │
│  └─────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

## 项目结构

```
CHI-new/
├── CMakeLists.txt
├── chi_model/                  ← CHI 协议引擎（通用）
│   ├── include/
│   │   ├── chi_transaction.hh  ← 事务数据结构
│   │   ├── chi_opcode.hh       ← Opcode 枚举
│   │   ├── chi_node.hh         ← 节点基类
│   │   ├── chi_hn_node.hh      ← HN-F 节点
│   │   ├── chi_channel.hh      ← Channel<T> 模板
│   │   └── chi_types.hh        ← NodeID, TxnID 等
│   └── src/
│       ├── chi_transaction.cc
│       ├── chi_hn_node.cc
│       └── chi_channel.cc
├── cache_model/                ← L2 Cache 模型
│   ├── include/
│   │   └── simple_l2_cache.hh
│   └── src/
│       └── simple_l2_cache.cc
├── middleware/                 ← gem5 ↔ CHI Model 适配
│   ├── include/
│   │   └── chi_middleware.hh
│   └── src/
│       └── chi_middleware.cc
├── test/                       ← 验证程序
│   ├── test_128kb.cc
│   └── CMakeLists.txt
└── gem5_integration/           ← gem5 侧文件
    ├── our_l2.sm               ← SLCc 控制器
    ├── OurL2.py                ← SimObject 配置
    └── SConscript              ← gem5 构建注册
```

## Risks / Trade-offs

- **[风险] SLCc 框架学习成本** → 缓解: Phase 1 的 SLCc 控制器极简（直通），先跑通再逐步深入
- **[风险] gem5 Ruby CHI 消息格式变更** → 缓解: Middleware 层隔离格式差异，仅需修改转换代码
- **[风险] 直通模式不测试缓存一致性** → 缓解: 这是 Phase 1 的明确范围，Phase 2 再扩展
- **[权衡] Channel 通信引入额外开销** → 可接受: 功能模型不关注性能，Channel 的解耦收益大于开销
