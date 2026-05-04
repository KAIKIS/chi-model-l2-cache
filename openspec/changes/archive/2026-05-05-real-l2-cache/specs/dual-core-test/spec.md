## ADDED Requirements

### Requirement: 双核测试程序架构
验证框架 SHALL 包含两个独立的 aarch64 可执行文件（test_core0 和 test_core1），分别运行在 Core 0 和 Core 1 上。

程序约束：
- 静态链接（-static）
- 使用 `_exit()` 退出（避免 gem5 Ruby drain hang）
- 不依赖 libc 的复杂功能（printf 等），使用原始 syscall 输出

#### Scenario: 交叉编译
- **WHEN** 使用 aarch64-linux-gnu-g++ 交叉编译 test_core0.cc 和 test_core1.cc
- **THEN** 生成两个静态链接的 aarch64 可执行文件

#### Scenario: gem5 SE 模式加载
- **WHEN** gem5 配置中指定 `--cmd0=test_core0 --cmd1=test_core1`
- **THEN** Core 0 运行 test_core0，Core 1 运行 test_core1

### Requirement: 共享内存同步机制
测试程序 SHALL 使用共享内存中的 atomic 变量进行双核同步。

同步方式：
- 使用 `std::atomic<uint64_t>` 类型的共享变量
- 使用 `memory_order_release` / `memory_order_acquire` 保证可见性
- 使用自旋等待（spin-wait）实现 barrier

#### Scenario: Barrier 同步
- **WHEN** Core 0 和 Core 1 都到达 barrier 点
- **THEN** 两个 core 继续执行后续代码

#### Scenario: 单方先到达
- **WHEN** Core 0 先到达 barrier，Core 1 尚未到达
- **THEN** Core 0 自旋等待，直到 Core 1 也到达

### Requirement: 测试场景 - 共享读
测试程序 SHALL 验证 Core 0 写入的数据能被 Core 1 正确读取。

测试流程：
1. Core 0 写入 N 个 cache line 的数据
2. Barrier 同步
3. Core 1 读取并验证数据正确性

#### Scenario: Core 0 写入
- **WHEN** Core 0 执行顺序写入 64 个 cache line（4KB）
- **THEN** L2 收到 ReadUnique 请求，line 状态变为 UD

#### Scenario: Core 1 读取验证
- **WHEN** Core 1 读取 Core 0 写入的数据并计算 checksum
- **THEN** checksum 与预期值一致，验证通过

### Requirement: 测试场景 - 写冲突
测试程序 SHALL 验证两个 core 对同一地址的写操作能正确串行化。

测试流程：
1. Core 0 写入地址 X 的值为 V0
2. Barrier 同步
3. Core 1 写入地址 X 的值为 V1
4. Barrier 同步
5. Core 0 读取地址 X，验证值为 V1

#### Scenario: Core 1 覆写 Core 0 的数据
- **WHEN** Core 0 写 X=V0，然后 Core 1 写 X=V1
- **THEN** L2 对 Core 1 的写操作发送 SnpCleanInvalid 给 Core 0

#### Scenario: Core 0 读取最新值
- **WHEN** Core 0 在 Core 1 写入后读取 X
- **THEN** 读取到的值为 V1（Core 1 写入的最新值）

### Requirement: 测试场景 - 乒乓模式
测试程序 SHALL 验证两个 core 交替读写同一地址的正确性。

测试流程：
1. 重复 N 次：
   - Core 0 写 X = i
   - Barrier
   - Core 1 读 X == i
   - Core 1 写 X = i+1
   - Barrier
   - Core 0 读 X == i+1

#### Scenario: 交替写入正确性
- **WHEN** Core 0 和 Core 1 交替写入同一地址 100 次
- **THEN** 每次读取都获得对方最后一次写入的值

### Requirement: 测试场景 - 驱逐压力
测试程序 SHALL 验证 L2 cache 驱逐后数据正确性。

测试流程：
1. Core 0 写入超过 L2 容量的数据（如 512KB，L2 为 256KB）
2. Barrier 同步
3. Core 1 读取前 256KB 数据并验证

#### Scenario: 驱逐触发
- **WHEN** Core 0 写入 128KB 数据到连续地址
- **THEN** L2 发生驱逐，部分 line 被写回内存

#### Scenario: 驱逐后数据正确
- **WHEN** Core 1 读取 Core 0 写入的前 64KB 数据
- **THEN** 所有数据正确，即使部分 line 已被驱逐并重新 fill

### Requirement: 测试结果输出
测试程序 SHALL 通过原始 syscall 输出测试结果。

输出方式：
- 使用 `write(1, msg, len)` 系统调用输出到 stdout
- 每个测试场景输出 PASS 或 FAIL
- 最终输出总结

#### Scenario: 所有测试通过
- **WHEN** 所有测试场景验证成功
- **THEN** 输出 "ALL TESTS PASSED" 并以 exit code 0 退出

#### Scenario: 某个测试失败
- **WHEN** 某个测试场景验证失败
- **THEN** 输出失败的测试名称和详情，以 exit code 1 退出
