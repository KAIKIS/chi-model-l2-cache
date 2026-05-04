## Why

当前 L2 Cache 是一个纯透传模型（SimpleL2Cache），没有 tag/data 存储，没有一致性状态跟踪，每个请求都直接到内存。要验证 CHI 协议的实际行为（cache hit/miss、一致性状态转换、多核 snoop），需要实现一个真正带存储的 L2 Cache，支持 2-core shared L2 架构，并通过自定义双核测试程序验证正确性。

## What Changes

- **L2 Cache 数据结构**：新增 CacheLine（tag + state + data[64] + sharers）、CacheSet（N-way 组相联 + LRU）、L2Cache（sets + pendingTxns）
- **CHI 完整状态模型**：实现 I/UC/SC/UD/SD 五种状态，覆盖 ReadShared、ReadUnique、CleanUnique、WriteBackFull 的完整状态转换
- **Snoop 操作**：新增 SnpInvalidate 等 snoop opcode，实现多核一致性维护（sharer 跟踪、invalidation 广播）
- **同步模型**：将 OurL2Middleware 从 channel 异步模型改为同步模型，直接在 gem5 回调线程中执行 cache lookup + 状态转换
- **2-Core 网络配置**：修改 gem5 配置支持 2 个 RN-F 共享 1 个 HN-F
- **双核测试程序**：编写两个独立的 aarch64 测试二进制（test_core0 / test_core1），验证共享读、写冲突、驱逐正确性

## Capabilities

### New Capabilities
- `cache-coherence`: CHI 一致性状态模型——CacheLine 数据结构、I/UC/SC/UD/SD 状态转换机、sharer 跟踪、snoop 发送逻辑
- `dual-core-test`: 双核验证程序——两个独立 aarch64 二进制，覆盖共享读、写冲突、ping-pong、驱逐压力场景

### Modified Capabilities
- `l2-cache-model`: 从透传模式升级为带 tag/data 存储的真实 L2 Cache，新增 CacheLine/CacheSet/L2Cache 数据结构，新增 LRU 替换、memory fill/writeback 逻辑
- `gem5-integration`: OurL2Middleware 从 channel 异步模型改为同步模型，新增 snoop 消息发送、multi-core 请求区分、2-core 网络配置支持
- `verification`: 从单核 128KB 测试升级为双核一致性验证框架

## Impact

- `cache_model/`：SimpleL2Cache 重写，新增 CacheLine/CacheSet/L2Cache 类
- `chi_model/`：新增 snoop opcode（SnpInvalidate 等），ChiTransaction 可能需要新字段
- `gem5/src/mem/my_l2/`：OurL2Middleware.cc 重写 recvRequestMsg/recvDataMsg 逻辑，新增 recvSnoopMsg 处理
- `gem5/configs/example/arm/our_l2_hierarchy.py`：num_cores=2，网络拓扑调整
- `test/`：新增 test_core0.cc、test_core1.cc，交叉编译
- `CMakeLists.txt`：cache_model 目录可能新增源文件
