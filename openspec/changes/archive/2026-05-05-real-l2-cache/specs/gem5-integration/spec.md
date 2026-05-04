## MODIFIED Requirements

### Requirement: SLCc 控制器（新增文件）
gem5 集成 SHALL 通过 CHIGenericController 实现，OurL2Middleware 继承 CHIGenericController。

OurL2Middleware 持有 L2Cache 实例，在 gem5 回调线程中同步执行 cache 逻辑。

#### Scenario: gem5 编译
- **WHEN** 编译 gem5，包含 OurL2Middleware 和 cache_model 源文件
- **THEN** 编译成功，OurL2Middleware 正确注册为 CHIGenericController

#### Scenario: 同步 cache lookup
- **WHEN** recvRequestMsg() 被调用
- **THEN** 直接调用 L2Cache::lookup()，hit 时本地生成响应，miss 时向内存发送请求

### Requirement: Middleware 格式转换
OurL2Middleware SHALL 实现 gem5 CHI 消息格式与内部 cache 操作之间的转换。

转换包括：
- gem5 CHIRequestType → cache 操作（lookup/fill/evict/writeback）
- gem5 地址格式 → cache 地址（tag/set/offset 分解）
- Cache 响应 → gem5 CHI 响应消息

#### Scenario: 转换 ReadShared 请求
- **WHEN** OurL2Middleware 收到 gem5 的 ReadShared 请求消息
- **THEN** 提取地址，执行 cache lookup，根据 hit/miss 决定本地响应或转发到内存

#### Scenario: 转换 CompData 响应
- **WHEN** L2 cache 命中需要返回数据
- **THEN** 构造 gem5 的 CompData 响应消息，填充 cache line 数据

### Requirement: Channel 连接
OurL2Middleware SHALL 直接在 gem5 回调中处理请求，不使用 HnNode 的 channel 模型。

#### Scenario: 请求处理
- **WHEN** gem5 调用 recvRequestMsg()
- **THEN** OurL2Middleware 同步执行 cache 逻辑，不经过 channel 中转

#### Scenario: 内存数据接收
- **WHEN** gem5 调用 recvDataMsg()，携带 SN-F 的 CompData
- **THEN** OurL2Middleware 执行 cache fill，然后将数据发送给原始请求方

### Requirement: 2-Core 网络配置
gem5 配置 SHALL 支持 2 个 RN-F 节点共享 1 个 HN-F 节点（OurL2Middleware）。

#### Scenario: 配置创建 2 个 core
- **WHEN** gem5 配置中 num_cores=2
- **THEN** 创建 2 个 CPU，每个 CPU 有自己的 L1 I/D cache，所有 L1 共享同一个 OurL2Middleware

#### Scenario: NodeID 分配
- **WHEN** 2-core 配置启动
- **THEN** core0.dcache=0, core0.icache=1, core1.dcache=2, core1.icache=3, OurL2Middleware=4, MemoryController=5

#### Scenario: 请求来源区分
- **WHEN** OurL2Middleware 收到来自不同 core 的请求
- **THEN** 通过消息中的 requestor MachineID 正确区分请求来源

### Requirement: Snoop 消息发送
OurL2Middleware SHALL 支持向 RN-F 发送 SnpCleanInvalid 消息（gem5 中等价于 CHI SnpInvalidate）。

gem5 API：使用 `CHIRequestType_SnpCleanInvalid`，通过 `sendSnoopMsg()` 发送，消息类型为 `CHIRequestMsg`。

#### Scenario: 构造 SnpCleanInvalid
- **WHEN** L2 cache 需要 invalidate 某个 RN-F 的 cache line
- **THEN** OurL2Middleware 构造 SnpCleanInvalid 消息，设置正确的地址和目标 NetDest

#### Scenario: 发送到网络
- **WHEN** SnpCleanInvalid 消息构造完成
- **THEN** 通过 CHIGenericController 的 sendSnoopMsg() 发送到目标 RN-F

### Requirement: 禁用 DMT 模式
OurL2Middleware SHALL 禁用 DMT（Direct Memory Transfer）模式，让内存数据经过 HN-F 以便 fill 到 cache。

gem5 API：发送 ReadNoSnp 时设置 `fwdRequestor=m_machineID` 和 `dataToFwdRequestor=false`。

#### Scenario: Read miss 发送到内存
- **WHEN** L2 cache miss 需要从内存读取数据
- **THEN** 发送 ReadNoSnp 到内存，fwdRequestor 设为自身，dataToFwdRequestor=false，数据返回到 HN-F

#### Scenario: 数据经过 HN-F
- **WHEN** 内存返回 CompData 到 HN-F
- **THEN** recvDataMsg() 收到数据，执行 cache fill，然后转发给原始请求方
