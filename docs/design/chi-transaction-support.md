# CHI Model 事务支持说明

## 当前支持的事务

### RN-F → HN-F（L1 发往 L2）

| 事务 | Opcode | 引擎入口 | 命中行为 | 未命中行为 |
|------|--------|---------|---------|-----------|
| **ReadShared** | `0x00` | `handleReadShared` | UD/SD 且有其他 sharer → 发 SnpCleanInvalid 嗅探；其余直接返回 CompData(SC) | SendReadNoSnp → WaitMemData → CompData(SC) |
| **ReadUnique** | `0x01` | `handleReadUnique` | 有其他 sharer → 发 **SnpUnique**；无其他 sharer → CompData(UD) | SendReadNoSnp → WaitMemData → CompData(UD) |
| **ReadNotSharedDirty** | `0x04` | `handleReadNotSharedDirty` | 有其他 sharer → 发 SnpUnique；无其他 sharer → CompData(UD) | SendReadNoSnp → WaitMemData → CompData(UD) |
| **CleanUnique** | `0x02` | `handleCleanUnique` | UC → Comp；UD → WriteNoSnp+Comp；SD → 发 **SnpNotSharedDirty**；SC → 发 SnpCleanInvalid | SendReadNoSnp → WaitMemData → Comp |
| **WriteBackFull** | `0x03` | `handleWriteBackFull` | CompDBIDResp → WaitL1Data → 存入 L2(UD) | — |
| **WriteUniqueFull** | `0x05` | `handleWriteUniqueFull` | 同 WriteBackFull | — |
| **WriteEvictFull** | `0x06` | `handleWriteEvictFull` | 同 WriteBackFull | — |
| **Evict** | — | `invalidate()` (直通) | 直接失效 L2 对应行，返回 Comp_I | — |

### HN-F → SN-F（L2 发往 Memory Controller）

| 事务 | ProtocolAction | 触发场景 |
|------|---------------|---------|
| **ReadNoSnp** | `SendReadNoSnp` | 所有读类请求的 L2 miss |
| **WriteNoSnp** | `SendWriteNoSnp` | CleanUnique 命中 UD 状态时的脏数据写回 |

### HN-F → RN-F（L2 发往 L1）

| 事务 | ProtocolAction | 说明 |
|------|---------------|------|
| **CompData** | `SendCompData` | 返回数据 + 行状态 (SC / UC / UD)，多 beat 发送 |
| **Comp** | `SendComp` | 无数据完成响应（CleanUnique 命中 UC / 未命中完成） |
| **CompDBIDResp** | `SendCompDBIDResp` | 请求 L1 回写脏数据（WriteBackFull 系列） |
| **Comp_I** | `sendComp` | Evict 的完成响应 |

### Snoop 路径

| 事务 | ProtocolAction | 使用场景 | L1 行为 |
|------|---------------|---------|--------|
| **SnpCleanInvalid** | `SendSnpCleanInvalid` | ReadShared (UD/SD) / CleanUnique (SC) | 失效 + 回写脏数据 |
| **SnpUnique** | `SendSnpUnique` | ReadUnique / ReadNotSharedDirty | 失效 + 回写脏数据 |
| **SnpNotSharedDirty** | `SendSnpNotSharedDirty` | CleanUnique (SD) | 保留 SC 副本 + 回写脏数据 |

### 内部处理的消息

| 消息 | 来源 | 处理方式 |
|------|------|---------|
| CompAck | L1 | 直接消费，不触发引擎 |
| SnpRespData_* | L1 (datIn) | 数据+响应合一，同时递减 dataCount 和 respCount |
| SnpResp_* | L1 (rspIn) | 递减 respCount，到零时 completePending |
| CompData (内存响应) | Memory (rspIn) | 路由到 recvData，填充缓存并响应 L1 |
| Comp (内存响应) | Memory (rspIn) | 仅删除 WaitMemWriteAck 条目 |

---

## 与 CHI 协议 SPEC 的一致性

### Issue 1 架构层面的设计选择

当前模型采用**简化的一致性模型**，与 ARM CHI SPEC 的主要差异在架构层面而非协议层面：

| 方面 | CHI SPEC | 本实现 | 影响 |
|------|---------|--------|------|
| **缓存行状态** | I/UC/UD/SC/SD (5 态) | 同左 | ✓ 一致 |
| **TxnID 管理** | 8-bit (0-255) | 引擎使用 10000+ (snoop) / 20000+ (memory)，gem5 消息使用 gem5 分配的 txnId (0-63) | ⚠️ 引擎内部使用大范围 txnId 避免与 gem5 冲突，外部消息使用 gem5 的 txnId |
| **TBE 管理** | 硬件资源限制 (TBETable) | 使用 `std::unordered_map` (无限制) | ⚠️ 功能正确，但不模拟资源限制 |
| **缓存容量** | 可配置 | L2Cache 固定 512-set × 8-way = 256KB | ⚠️ 可配置但当前固定 |
| **Requester 顺序** | 支持 RespOrder | 不支持 | ❌ 未实现 |

### Issue 2 snoop 类型与协议 SPEC 的对应关系

| 场景 | SPEC 要求 | 本实现 | 一致性 |
|------|----------|--------|--------|
| ReadShared + 脏共享者 | SnpCleanInvalid (retToSrc=true) | SnpCleanInvalid | ✓ |
| ReadUnique + 有其他共享者 | SnpUnique (retToSrc=needData) | SnpUnique | ✓ |
| ReadNotSharedDirty + 有其他共享者 | SnpUnique (retToSrc=needData) | SnpUnique | ✓ |
| CleanUnique + SD 状态 | 需要脏数据但共享者可保留 SC | SnpNotSharedDirty | ✓ |
| CleanUnique + SC 状态 | 共享者需失效 | SnpCleanInvalid | ✓ |

### Issue 3 已知限制

1. **不支持 DMT (Direct Memory Transfer)**：所有 ReadNoSnp 使用非 DMT 模式 (`dataToFwdRequestor=false`)
2. **不支持 DCT (Direct Cache Transfer)**：无 Cache Stash 机制
3. **不支持原子操作**：AtomicLoad/Store/Swap/Compare 未实现
4. **不支持 DVM**：无 Distributed Virtual Memory 事务
5. **不支持流控**：RetryAck / PCrdGrant 未实现（MessageBuffer 默认无限容量）
6. **不支持 Cache 维护操作**：CleanShared/Invalid/Persist/MakeInvalid 未实现
7. **不支持 ReadOnce / ReadClean / ReadNotSharedDirty 命中 SD 时的 WB 语义**——当前直接返回数据而不写回内存
8. **MissEvictDirty 未处理**：L2 miss 同时需要驱逐脏行时触发 assert，大工作集会失败
9. **WriteUniqueFull 未区分语义**：SPEC 中 L1 保留 Unique 状态，当前实现等同于 WriteBackFull（存入 L2 之后 L1 状态由 gem5 L1 自行管理，不影响正确性）

---

## 如何运行测试

### 前提条件

```bash
cd /home/zhangkai/work/CHI-new
```

### 1. 单元测试（独立于 gem5）

测试 ChiProtocolEngine 的所有 handler，无需编译 gem5：

```bash
# 编译
cd build && make test_protocol_engine -j$(nproc)

# 运行
./test/test_protocol_engine
```

预期输出：`All ChiProtocolEngine tests passed!`（当前 22 项测试）

### 2. 单核集成测试（gem5 全链路）

验证 CPU → L1 → L2 → Memory 的完整路径：

```bash
# 编译 gem5（增量编译，仅编译变更文件）
cd gem5 && scons build/ARM/gem5.opt -j$(nproc)

# 运行
./build/ARM/gem5.opt ../test/gem5_run_128kb.py
```

预期输出：`Simulation complete.` + L2 统计信息

### 3. 双核一致性测试（gem5 全链路）

验证双核共享 L2 的 snoop 路径：

```bash
# 编译（同上）
cd gem5 && scons build/ARM/gem5.opt -j$(nproc)

# 运行
./build/ARM/gem5.opt ../test/gem5_run_dual_core.py
```

预期输出：
```
main: begin
child: started
child: done
main: done
```

### 4. 编译并运行测试二进制

```bash
# 编译 aarch64 测试程序（需要 aarch64-linux-gnu 交叉编译工具链）
cd build-aarch64 && make -j$(nproc)

# 可用测试二进制：
#   test_128kb        — 单核 128KB 数据测试
#   test_pthread_min  — 双核最小 pthread 测试
#   test_dual_core    — 双核完整一致性测试
#   test_shared       — 共享变量测试
#   test_noshare      — 无共享变量测试（对照组）
```

### 一键运行所有测试

```bash
# 单元测试
cd build && make test_protocol_engine -j$(nproc) && ./test/test_protocol_engine

# 单核集成测试
cd ../gem5 && ./build/ARM/gem5.opt ../test/gem5_run_128kb.py

# 双核集成测试
./build/ARM/gem5.opt ../test/gem5_run_dual_core.py
```
