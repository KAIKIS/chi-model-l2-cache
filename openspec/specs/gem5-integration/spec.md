## Requirements

### Requirement: SLCc 控制器（新增文件）
gem5 集成 SHALL 通过新增 SLCc 控制器文件实现，不修改 gem5 原有代码。

新增文件包括：
- `our_l2.sm`: SLCc 状态机定义
- `OurL2.py`: SimObject 配置类
- `SConscript` 或注册代码

#### Scenario: gem5 编译
- **WHEN** 将新增文件放入 gem5 源码树并编译
- **THEN** 编译成功，不修改 gem5 原有文件

#### Scenario: 配置切换
- **WHEN** 在 gem5 配置文件中指定使用 OurL2 控制器
- **THEN** gem5 使用 OurL2 而非默认的 CHI-cache 控制器

### Requirement: Middleware 格式转换
Middleware SHALL 实现 gem5 Ruby CHI 消息格式与 ChiTransaction 之间的双向转换。

转换包括：
- gem5 CHIRequestType → ChiTransaction Opcode
- ChiTransaction Opcode → gem5 CHIRequestType
- gem5 地址/数据格式 → ChiTransaction 字段

#### Scenario: 转换 ReadShared 请求
- **WHEN** Middleware 收到 gem5 的 ReadShared 请求消息
- **THEN** 生成 opcode 为 ReadShared 的 ChiTransaction，addr 和 size 正确映射

#### Scenario: 转换 CompData 响应
- **WHEN** Middleware 收到 ChiTransaction 的 CompData 响应
- **THEN** 生成 gem5 的 CompData 响应消息，数据内容一致

### Requirement: Channel 连接
Middleware SHALL 将 gem5 SLCc 控制器与 CHI 模型的 HnNode 通过 Channel 连接。

#### Scenario: 请求转发
- **WHEN** SLCc 控制器收到 L1 的 CHI 请求
- **THEN** Middleware 将其转换为 ChiTransaction 并 push 到 HnNode 的请求 Channel

#### Scenario: 响应返回
- **WHEN** HnNode 将响应 push 到响应 Channel
- **THEN** Middleware 将其转换为 gem5 消息格式并返回给 SLCc 控制器
