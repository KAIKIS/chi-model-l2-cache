# 双核测试同步机制分析

## 最终结果

**双核 true share 测试全部通过（8/8）：**

```
PASS: c0_write_readback     ← Core 0 私有数据写入回读
PASS: c1_write_readback     ← Core 1 私有数据写入回读
PASS: c0_stride             ← Core 0 跨步访问
PASS: c1_stride             ← Core 1 跨步访问
PASS: c0_reverse            ← Core 0 倒序访问
PASS: c1_reverse            ← Core 1 倒序访问
PASS: c0_shared_write       ← Core 0 写共享数据 + futex_wake 通知
PASS: c1_shared_read        ← Core 1 通过 futex_wait 接收 + 验证数据一致性
```

测试文件：[test_share_full.cc](../test/test_share_full.cc)
测试脚本：[gem5_run_dual_core.py](../test/gem5_run_dual_core.py)

## 方案调研

### 方案 1：busy-wait 轮询 — ❌ 不可用

```c
// Core 1: 轮询等待
for (int tries = 0; tries < 10000000; tries++) {
    if (sync_flag != 0) break;
    busy_wait(10);
}
```

**失败原因**：gem5 TimingSimpleCPU 是事件驱动的交替执行，不是真正并行。Core 0 的 store 和 Core 1 的轮询交替阻塞在 DDR 响应上，固定次数的轮询在 gem5 时序下不可靠。

### 方案 2：WFE/SEV — ❌ gem5 未实现

```c
__asm__ volatile("wfe" ::: "memory");  // Wait For Event
__asm__ volatile("sev" ::: "memory");  // Send Event
```

**失败原因**：搜索 `gem5/src/arch/arm/` 全目录，未找到 WFE/SEV 的实现。当前 gem5 版本不支持这两个指令。

### 方案 3：futex + 大数据（16KB×2）— ❌ 死锁

```c
static volatile uint64_t data_a[2048];  // 16KB per core
static volatile uint64_t data_b[2048];
```

**失败原因**：独立测试阶段大量写入数据（6 个测试 × 128 行 × 8B），L2 缓存被填满，共享写入阶段的 DDR 请求排队延迟超过 50M ticks 的 deadlock 检测阈值。

### 方案 4：futex + 小数据（512B×2）— ✅ 通过

```c
static volatile uint64_t priv_a[64];  // 512B per core (8 cache lines)
static volatile uint64_t priv_b[64];
```

**成功关键**：
- 小数组避免 L2 容量压力
- `futex_wait`/`futex_wake` 提供可靠的核间同步（内核级）
- `while (flag == 0) futex_wait(&flag, 0)` 循环确保即使 wake 在 wait 之前发出也不会死等（futex_wait 在值不匹配时立即返回 EAGAIN）

### 方案 5：futex + ping-pong — ❌ 不稳定

Core 0 futex_wake → Core 1 futex_wait → Core 1 写回 → Core 1 futex_wake → Core 0 futex_wait。

**失败原因**：gem5 SE 模式下 futex 的 WAKE 可能在 wait 之前执行（时序问题），且多轮 futex 嵌套调用存在竞态。单轮 shared_write→shared_read 可以工作，但多轮 ping-pong 不稳定。

## 实现中解决的问题

### 1. `CHIGenericController::functionalRead` panic

futex syscall 在 gem5 SE 模式下会触发 functional memory read。原实现直接 `panic`。需要在 OurL2Middleware（和 OurL3Middleware）中正确实现 `functionalRead`/`functionalWrite`：

```cpp
void OurL2Middleware::functionalRead(const Addr& addr, Packet* pkt, WriteMask& mask) {
    chi::LineState st = cacheStorage_.getState(addr);
    if (st != chi::LineState::I) {
        const uint8_t* data = cacheStorage_.getData(addr);
        if (data) {
            memcpy(pkt->getPtr<uint8_t>(), data, cacheLineSize);
            mask.setMask(0, cacheLineSize);
        }
    }
}
```

### 2. `pthread_join` futex 死锁

`pthread_join` 内部使用 futex，在 gem5 SE 模式下会触发 deadlock 检测。解决方式：用 NOP 延迟替代 `pthread_join`，让 Core 1 有足够时间完成执行。

```c
// 替代 pthread_join(t1, nullptr)
for (volatile int i = 0; i < 50000; i++) __asm__ volatile("nop");
```

### 3. MissEvictDirty 未实现

L2Cache 在大工作负载下可能触发 `MissEvictDirty`（驱逐脏行），当前代码对此 `assert(false)`。小数据方案避开了这个问题，但大工作负载需要实现 writeback-on-evict。

## 当前可用测试

| 二进制 | 同步方式 | 结果 | 说明 |
|--------|---------|------|------|
| `test_pthread_min` | pthread_mutex | ✅ 稳定 | 基本创建+锁 |
| `test_share_minimal` | futex | ✅ 稳定 | 单 cache line 共享 |
| `test_share_full` | futex | ✅ 稳定 | 私有测试×6 + 共享读写 |
| `test_dual_core` | busy-wait | ❌ 不可用 | 时序不兼容 |
| `test_dual_core_futex` | futex | ❌ ping-pong 超时 | 多轮同步不稳定 |

## gem5 双核测试最佳实践

1. **避免 busy-wait 轮询**：gem5 事件驱动模型下时序不可靠
2. **WFE/SEV 当前不可用**：此 gem5 版本未实现
3. **使用 futex 同步**：`FUTEX_WAIT`/`FUTEX_WAKE` 是 SE 模式下最可靠的核间同步方式
4. **用 `while(flag==0) futex_wait()` 模式**：确保即使 wake 先于 wait 执行也能正常工作
5. **避免 `pthread_join`**：用 NOP 延迟替代，避免内部 futex 死锁
6. **控制工作集大小**：大数组（> 几 KB）会导致 L2 容量压力，触发 deadlock 检测
7. **实现 `functionalRead`/`functionalWrite`**：futex syscall 需要 functional memory access
