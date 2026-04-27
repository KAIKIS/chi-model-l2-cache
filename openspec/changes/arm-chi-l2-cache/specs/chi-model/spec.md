## ADDED Requirements

### Requirement: ChiTransaction 数据结构
CHI 模型 SHALL 提供 ChiTransaction 数据结构，包含以下字段：
- `txnID` (uint64_t): 事务唯一标识
- `opcode` (Opcode): CHI 操作码
- `addr` (uint64_t): 目标地址
- `size` (uint32_t): 数据传输大小（字节）
- `data` (vector<uint8_t>): 数据载荷
- `respStatus` (RespStatus): 响应状态

Phase 1 的 Opcode 枚举 SHALL 包含：ReadShared, ReadUnique, CleanUnique, WriteBackFull, ReadNoSnp, WriteNoSnp, CompData, WriteAck, Comp。

#### Scenario: 创建 ReadShared 请求
- **WHEN** 创建一个 opcode 为 ReadShared 的 ChiTransaction
- **THEN** txnID 非零，opcode 为 ReadShared，addr 和 size 有有效值，data 为空（请求阶段）

#### Scenario: 创建 CompData 响应
- **WHEN** 创建一个 opcode 为 CompData 的 ChiTransaction
- **THEN** respStatus 为成功，data 包含请求的数据（大小为 size 字节）

### Requirement: Channel 通信模板
CHI 模型 SHALL 提供 Channel<T> 模板类，支持阻塞式消息传递。

Channel 必须支持的操作：
- `push(T msg)`: 将消息放入 channel，阻塞直到成功
- `pop() -> T`: 从 channel 取出消息，阻塞直到有消息

#### Scenario: 单生产者单消费者通信
- **WHEN** 线程 A 调用 push(msg)，线程 B 调用 pop()
- **THEN** 线程 B 收到的消息与线程 A 发送的 msg 一致

#### Scenario: 空 channel 阻塞
- **WHEN** channel 为空时调用 pop()
- **THEN** 调用线程阻塞，直到有其他线程调用 push()

### Requirement: HN-F 节点基类
CHI 模型 SHALL 提供 HnNode 类，实现 HN-F 节点的基本框架。

HnNode 必须包含：
- 请求输入 Channel 和响应输出 Channel（面向 RN 侧）
- 请求输出 Channel 和响应输入 Channel（面向 SN 侧）
- `process()` 方法：从请求 Channel 读取事务，进行处理，将响应写入响应 Channel

#### Scenario: 接收并转发请求
- **WHEN** HnNode 的 RN 侧请求 Channel 中有一个 ReadShared 请求
- **THEN** HnNode 将其转换为 ReadNoSnp 并放入 SN 侧请求 Channel

#### Scenario: 接收 SN 响应并返回
- **WHEN** HnNode 的 SN 侧响应 Channel 中有一个 CompData 响应
- **THEN** HnNode 将其放入 RN 侧响应 Channel

### Requirement: 可独立编译
CHI 模型 SHALL 能够作为独立的 C++17 静态库编译，不依赖 gem5 或其他仿真器的头文件。

#### Scenario: 独立编译
- **WHEN** 使用 CMake 构建 chi_model 目标
- **THEN** 生成 libchi_model.a 静态库，无外部依赖
