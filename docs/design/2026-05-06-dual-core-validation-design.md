## Context

单核 test_128kb 已在 gem5 CHI Ruby 全链路仿真中 PASS。下一步需要验证双核共享 L2 Cache 场景，确保 ChiProtocolEngine 的 snoop 路径在多核一致性协议中正常工作。

当前 test_dual_core.cc 已实现：两个线程通过共享内存同步，完成 shared write→read 和 ping-pong 测试。gem5 SE 模式通过 `clone()` syscall 支持 pthread_create，自动将子线程调度到 Core 1 的 ThreadContext 上执行。

## Goals / Non-Goals

**Goals:**
- 用 gem5 CHI Ruby + 双核 L1 + OurL2Middleware + ChiProtocolEngine 跑通 test_dual_core.cc
- 验证 Engine 的 snoop 路径（SnpCleanInvalid、retToSrc、pendingSnoopCount/DataCount）
- 确认双核共享数据的正确性（Core 0 写 → Core 1 读验证）

**Non-Goals:**
- 不修改 Engine 和 Middleware 代码（除非发现 bug）
- 不新增协议操作码
- 不实现三核及以上场景

## Architecture

```
test_dual_core (单 aarch64 二进制，内含 pthread_create)
        │
        ▼
   gem5 SimpleProcessor(num_cores=2)
        │ clone() syscall 被 gem5 拦截
        ▼
   Core 0 ──► core0_func()     Core 1 ──► core1_func()
        │                              │
   L1 dcache (CHI/MOESI)       L1 dcache (CHI/MOESI)
        │                              │
        └── SimplePt2Pt Network ──────┘
                      │
              OurL2Middleware
              ┌──────────────────────┐
              │  Middleware (thin)   │ ← gem5 ↔ CHI 翻译
              │  ChiProtocolEngine   │ ← 协议决策 (含 snoop)
              │  L2Cache             │ ← 数据存储
              └──────────────────────┘
                      │
               Memory Controller (SN-F)
                      │
                    DRAM
```

### 数据流：Core 0 写 → Core 1 读

```
Core 0 (Write)
  → L1 miss (ReadUnique)
  → OurL2: lookup miss → ReadNoSnp to memory
  → Memory returns CompData
  → OurL2: fill(UD), SendCompData(UD) to Core 0
  → Core 0 writes data to cache line

Core 0 evicts dirty line
  → WriteBackFull to OurL2
  → OurL2: CompDBIDResp → L1 sends CBWrData
  → OurL2: absorb dirty data to L2 (UD)

Core 1 (Read shared_data)
  → L1 miss (ReadShared)
  → OurL2: lookup hit (UD, dirty in L2)
  → OurL2: serve CompData(SC) to Core 1 directly
  → Core 1 reads and verifies

Core 0 sets sync_flag = 1
  → Write to shared address
  → L1 has it in UD → already coherent

Core 1 reads sync_flag
  → L1 miss (ReadShared)
  → OurL2: lookup hit (may be in SD if Core 0 shared)
  → serve from L2
```

### Ping-Pong 数据流

```
Phase 1: Core 0 writes pong_data (pattern A)
  → ReadUnique + WriteBackFull → L2 has pong_data in UD
  → dmb sy + pong_sync1 = 1

Phase 2: Core 1 reads pong_sync1 → sees flag
  → ReadShared for pong_data → OurL2 has UD
  → CHI protocol: UD → SD transition (marking shared)
  → OurL2 serves CompData(SD) to Core 1
  → Core 1 verifies pattern A

Phase 3: Core 1 writes pong_data (pattern B)
  → Core 1 has SD → needs CleanUnique or ReadUnique
  → OurL2: sees sharers include Core 0
  → SendSnpCleanInvalid to Core 0
  → Core 0 invalidates its copy → SnpCleanInvalidResp
  → OurL2: transitions to UD, SendCompData(UD) to Core 1
  → Core 1 writes pattern B
  → dmb sy + pong_sync2 = 1

Phase 4: Core 0 reads pong_sync2 → sees flag
  → ReadShared for pong_data → OurL2 has UD (owned by Core 1)
  → OurL2: UD → SD transition
  → OurL2 serves CompData(SD) to Core 0
  → Core 0 verifies pattern B
```

## Engine Snoop 路径分析

以下路径在单核测试中未被覆盖，双核测试将首次触发：

### SnpCleanInvalid 触发条件

| Engine 入口 | 触发条件 | retToSrc | pendingSnoopDataCount |
|------------|---------|----------|----------------------|
| handleReadShared | UD/SD 命中，有其他 sharer | true | = snoopCount |
| handleReadUnique | 任意状态命中，有其他 sharer | UD/SD 时为 true | = snoopCount (needData 时) |
| handleCleanUnique | SC/SD 命中，有其他 sharer | SD 时为 true | = snoopCount (needsWB 时) |

### 预期 Engine 行为

1. recvRequest 返回 `SendSnpCleanInvalid` action，pending 记录 `pendingSnoopCount` 和 `pendingSnoopDataCount`
2. snoop 响应到达时 `recvResponse` 递减 `pendingSnoopCount`
3. snoop 数据到达时 `recvData` 递减 `pendingSnoopDataCount`
4. 两者都归零时 `completePending` 被调用，设置目标状态，发送 CompData/Comp 给原始请求者

### 已知风险

| 风险 | 说明 | 应对 |
|------|------|------|
| snoop 响应与数据到达顺序不确定 | L1 先发 SnpResp_I 再发 CBWrData，或同时发送 | Engine 分别处理响应和数据，各自递减计数器 |
| 双核 txnId 可能冲突 | 两个 L1 可能使用相同的 txnId | pending_ 在 snoop 路径用 txn.txnID 作为 key，需要确认不同 core 的 txnId 不会冲突 |
| 数据节拍 | Snoop 数据可能分多个 beat 到达 | Middleware 的 recvDataMsg 每次 beat 都会调用 recvData，需要正确累积 |

## Gem5 Configuration

基于 `test/gem5_run_128kb.py` 修改为双核版本：

```python
# test/gem5_run_dual_core.py

processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING,
    isa=ISA.ARM,
    num_cores=2,  # 改为 2
)

# 使用 set_se_binary_workload（同一二进制加载到所有 core）
binpath = ".../build-aarch64/test_dual_core"
board.set_se_binary_workload(binary=BinaryResource(binpath))
```

Python 侧的 `incorporate_cache` 中循环 `board.get_processor().get_cores()` 自动为每个 core 创建 L1 cluster。

每个 core 的 sequencer `version` 参数（即 core_id）用于区分 CHI 消息中的 NodeID。

## Test Plan

### 编译验证

```bash
aarch64-linux-gnu-gcc -static -pthread -O2 \
  -o build-aarch64/test_dual_core test/test_dual_core.cc
file build-aarch64/test_dual_core
# Expected: ELF 64-bit LSB executable, ARM aarch64, statically linked
```

### Unit Tests（已有，持续通过）

```bash
cd build && cmake --build . -j$(nproc)
./test/test_l2_cache     # 13 tests
./test/test_protocol_engine  # 13 tests
```

### Gem5 仿真

```bash
cd gem5
scons build/ARM/gem5.opt -j$(nproc)
./build/ARM/gem5.opt --outdir=../m5out_dual ../test/gem5_run_dual_core.py
```

预期输出：
```
core0: started
core1: started
PASS: core0_write_readback
PASS: core1_write_readback
PASS: core0_stride
PASS: core1_stride
PASS: core0_reverse
PASS: core1_reverse
PASS: core0_shared_write
PASS: core1_shared_read
PASS: core0_pong_readback
PASS: core1_pong_write
ALL TESTS PASSED
```

### 调试策略

1. 先验证编译通过（交叉编译 + 静态 pthread）
2. 运行 gem5，观察 panic 信息
3. 若 snoop 路径有问题，panic 会在 Engine 的 pendingSnoopCount/DataCount 相关代码中触发
4. 根据具体 panic 信息修复 Engine（本次 scope 内）

## File Changes

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `test/gem5_run_dual_core.py` | 双核 gem5 运行脚本 |
| 修改 | （无需修改 Engine/Middleware） | 仅当调试发现 bug 时修改 |

## Non-Goals (明确排除)

- 不改动 `ChiProtocolEngine` 的接口（三个入口点已足够）
- 不实现超过 2 核的配置（但脚本结构天然支持扩展 num_cores）
- 不增加流控（PCrdGrant/RetryAck）或 DMT/DCT 高级传输模式
- 不修改 L2Cache 数据结构
