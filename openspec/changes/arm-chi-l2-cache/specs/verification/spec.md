## ADDED Requirements

### Requirement: 128KB 连续访问测试程序
验证框架 SHALL 包含一个 C++ 测试程序，连续访问 128KB 内存空间。

测试程序行为：
1. 分配 128KB 内存
2. 顺序写入 128KB（触发 ReadUnique + WriteBackFull）
3. 顺序读取 128KB（触发 ReadShared）
4. 输出校验和验证数据正确性

#### Scenario: 程序编译
- **WHEN** 使用 aarch64 交叉编译工具链编译测试程序
- **THEN** 生成静态链接的 ARM 可执行文件

#### Scenario: 写入阶段事务
- **WHEN** 测试程序执行顺序写入 128KB
- **THEN** L2 收到 ReadUnique 请求（L1 写 miss），数量约为 2048（128KB / 64B）

#### Scenario: 读取阶段事务
- **WHEN** 测试程序执行顺序读取 128KB
- **THEN** L2 收到 ReadShared 请求（L1 读 miss），数量约为 2048

#### Scenario: 数据正确性
- **WHEN** 测试程序完成读取并计算校验和
- **THEN** 校验和与预期值一致，证明数据在 CHI 通路中未损坏

### Requirement: gem5 SE 模式运行
验证框架 SHALL 支持在 gem5 SE（Syscall Emulation）模式下运行测试程序。

运行配置：
- CPU 类型: AtomicSimpleCPU（或 TimingSimpleCPU）
- Cache: Ruby CHI，L1 使用 gem5 原生，L2 使用 OurL2
- 内存: gem5 原生 DRAM

#### Scenario: 端到端运行
- **WHEN** 使用 gem5 SE 模式加载测试程序
- **THEN** 程序执行完成，无 hang、无 crash，输出正确的校验和

### Requirement: CHI Model 单元测试
验证框架 SHALL 包含 CHI 模型的单元测试，不依赖 gem5。

测试内容：
- ChiTransaction 创建和字段访问
- Channel 的 push/pop 阻塞行为
- HnNode 的请求转发和响应返回

#### Scenario: Channel 测试
- **WHEN** 在独立进程中测试 Channel
- **THEN** push 的消息能被 pop 正确接收，空 channel 的 pop 正确阻塞

#### Scenario: HnNode 测试
- **WHEN** 向 HnNode 的请求 Channel 发送 ReadShared
- **THEN** 从 SN 侧请求 Channel 收到 ReadNoSnp，从 RN 侧响应 Channel 收到 CompData
