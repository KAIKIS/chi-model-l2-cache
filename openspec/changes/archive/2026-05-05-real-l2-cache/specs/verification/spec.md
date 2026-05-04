## MODIFIED Requirements

### Requirement: 128KB 连续访问测试程序
验证框架 SHALL 保留现有单核 128KB 测试作为回归测试，同时新增双核一致性测试。

#### Scenario: 单核回归测试
- **WHEN** 在 1-core 配置下运行 test_128kb_aarch64
- **THEN** 程序执行完成，输出 PASS，数据正确性验证通过

### Requirement: gem5 SE 模式运行
验证框架 SHALL 支持在 gem5 SE 模式下运行单核和双核测试。

运行配置：
- 单核：num_cores=1，运行 test_128kb_aarch64
- 双核：num_cores=2，运行 test_core0_aarch64 和 test_core1_aarch64

#### Scenario: 单核端到端运行
- **WHEN** 使用 gem5 1-core 配置加载 test_128kb_aarch64
- **THEN** 程序执行完成，无 hang、无 crash

#### Scenario: 双核端到端运行
- **WHEN** 使用 gem5 2-core 配置加载 test_core0_aarch64 和 test_core1_aarch64
- **THEN** 两个程序执行完成，无 hang、无 crash，输出 ALL TESTS PASSED

### Requirement: CHI Model 单元测试
验证框架 SHALL 包含 L2Cache 的单元测试，不依赖 gem5。

测试内容：
- CacheLine 状态转换正确性
- LRU 替换策略正确性
- Sharer 跟踪正确性
- Cache hit/miss 行为

#### Scenario: CacheLine 状态转换测试
- **WHEN** 对 CacheLine 执行 ReadShared/ReadUnique/CleanUnique/WriteBackFull
- **THEN** 状态按 CHI 规范正确转换

#### Scenario: LRU 替换测试
- **WHEN** CacheSet 满时访问新地址
- **THEN** 最久未使用的 line 被驱逐

#### Scenario: Sharer 跟踪测试
- **WHEN** 多个 RN 读取同一 line，然后一个 RN 写入
- **THEN** sharer 集合正确添加/移除，SnpInvalidate 发送给正确的 RN

### Requirement: gem5 统计验证
验证框架 SHALL 支持通过 gem5 stats.txt 验证 cache 行为。

#### Scenario: Cache hit/miss 统计
- **WHEN** 双核测试运行完成
- **THEN** stats.txt 中包含 L2 cache 的 hit/miss 计数，且 hit 率大于 0（验证 cache 确实在工作）

#### Scenario: Snoop 统计
- **WHEN** 双核测试的写冲突场景完成
- **THEN** stats.txt 中记录了 SnpInvalidate 的发送次数
