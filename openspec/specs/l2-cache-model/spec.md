## Requirements

### Requirement: 直通模式 L2 Cache
L2 Cache 模型 SHALL 以直通模式运行，不维护任何缓存状态（无 tag、无 data array、无一致性状态）。

#### Scenario: 收到 ReadShared 请求
- **WHEN** L2 Cache 收到 ReadShared 请求
- **THEN** 将 opcode 转换为 ReadNoSnp，通过 SN 侧 Channel 转发

#### Scenario: 收到 ReadUnique 请求
- **WHEN** L2 Cache 收到 ReadUnique 请求
- **THEN** 将 opcode 转换为 ReadNoSnp，通过 SN 侧 Channel 转发

#### Scenario: 收到 CleanUnique 请求
- **WHEN** L2 Cache 收到 CleanUnique 请求
- **THEN** 将 opcode 转换为 Comp（无数据完成响应），通过 RN 侧响应 Channel 返回

#### Scenario: 收到 WriteBackFull 请求
- **WHEN** L2 Cache 收到 WriteBackFull 请求
- **THEN** 将 opcode 转换为 WriteNoSnp，携带数据通过 SN 侧 Channel 转发

### Requirement: 响应转发
L2 Cache 模型 SHALL 将 SN 侧的响应原样转发到 RN 侧。

#### Scenario: 收到 CompData 响应
- **WHEN** L2 Cache 从 SN 侧收到 CompData 响应
- **THEN** 将该响应放入 RN 侧响应 Channel

#### Scenario: 收到 WriteAck 响应
- **WHEN** L2 Cache 从 SN 侧收到 WriteAck 响应
- **THEN** 将该响应放入 RN 侧响应 Channel

### Requirement: 使用 CHI 模型接口
L2 Cache 模型 SHALL 通过 CHI 模型的 Channel 接口进行所有通信，不直接依赖 gem5 类型。

#### Scenario: 接口独立性
- **WHEN** 编译 L2 Cache 模型
- **THEN** 仅依赖 chi_model 的头文件，不包含 gem5 头文件
