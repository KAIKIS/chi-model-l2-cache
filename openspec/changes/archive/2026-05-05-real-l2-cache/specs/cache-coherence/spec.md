## ADDED Requirements

### Requirement: CacheLine 数据结构
Cache 模型 SHALL 提供 CacheLine 数据结构，包含 tag、一致性状态、数据载荷和 sharer 集合。

字段定义：
- `tag` (uint64_t): 地址高位（由 set 索引之外的位组成）
- `state` (LineState): CHI 一致性状态枚举，取值 I/UC/SC/UD/SD
- `data` (uint8_t[64]): 64 字节 cache line 数据
- `sharers` (std::set<NodeID>): 持有该 line 副本的 RN-F 列表

#### Scenario: 创建空 CacheLine
- **WHEN** 创建一个未使用的 CacheLine
- **THEN** state 为 I（Invalid），tag 为 0，data 为空，sharers 为空集合

#### Scenario: 填充 CacheLine
- **WHEN** 从内存 fill 一个 CacheLine，地址为 0x1000，数据为 64 字节
- **THEN** tag 更新为 0x1000 对应的 tag 位，state 根据请求类型设为 UC 或 SC，data 包含内存数据

### Requirement: CacheSet 组相联管理
Cache 模型 SHALL 提供 CacheSet 管理，支持 N-way 组相联和 LRU 替换策略。

每个 CacheSet 包含 N 个 CacheLine 槽位，使用 LRU（Least Recently Used）策略选择替换目标。

#### Scenario: Cache 命中
- **WHEN** 在 CacheSet 中查找地址，且该地址对应的 tag 存在于某个 line 且 state 不为 I
- **THEN** 返回命中，该 line 的 LRU 计数器更新为最近使用

#### Scenario: Cache 未命中且有空位
- **WHEN** 在 CacheSet 中查找地址未命中，且存在 state 为 I 的空闲 line
- **THEN** 使用该空闲 line 进行 fill，不触发驱逐

#### Scenario: Cache 未命中需要驱逐
- **WHEN** 在 CacheSet 中查找地址未命中，且所有 line 都被占用（无 state 为 I）
- **THEN** 选择 LRU 计数器最低的 line 作为驱逐目标

### Requirement: LRU 替换策略
Cache 模型 SHALL 实现 LRU 替换策略，为每次访问更新 LRU 状态。

#### Scenario: 访问更新 LRU
- **WHEN** 命中或 fill 一个 CacheLine
- **THEN** 该 line 的 LRU 计数器设为最大值，其他 line 的计数器减 1

#### Scenario: 驱逐最久未使用
- **WHEN** 需要驱逐且所有 line 计数器不同
- **THEN** 选择计数器最小的 line 进行驱逐

#### Scenario: LRU 计数器溢出
- **WHEN** LRU 计数器达到最大值需要减 1
- **THEN** 计数器正确归零，不产生溢出

### Requirement: CHI 完整状态模型
Cache 模型 SHALL 实现 CHI 协议定义的五种 cache line 状态：I（Invalid）、UC（Unique Clean）、SC（Shared Clean）、UD（Unique Dirty）、SD（Shared Dirty）。

#### Scenario: ReadShared 请求 - 状态转换
- **WHEN** 收到 ReadShared 请求
- **THEN** 状态转换遵循以下规则：
  - I → SC（从内存 fill，记录请求方为 sharer）
  - UC → SC（直接返回数据，记录请求方为 sharer）
  - SC → SC（直接返回数据，记录请求方为 sharer）
  - UD → SD（直接返回数据，记录请求方为 sharer）
  - SD → SD（直接返回数据，记录请求方为 sharer）

#### Scenario: ReadUnique 请求 - 状态转换
- **WHEN** 收到 ReadUnique 请求
- **THEN** 状态转换遵循以下规则：
  - I → UD（从内存 fill，独占）
  - UC → UD（直接返回数据，独占）
  - SC → UD（发送 SnpInvalidate 给所有 sharers，清空 sharers 集合，独占）
  - UD → UD（直接返回数据，已是独占）
  - SD → UD（发送 SnpInvalidate 给所有 sharers，清空 sharers 集合，独占）

#### Scenario: CleanUnique 请求 - 状态转换
- **WHEN** 收到 CleanUnique 请求
- **THEN** 状态转换遵循以下规则：
  - I → UC（从内存 fill，独占 clean）
  - UC → UC（直接返回，已是独占 clean）
  - SC → UC（发送 SnpInvalidate 给所有 sharers，清空 sharers 集合）
  - UD → UC（写回脏数据到内存，转为 clean）
  - SD → UC（写回脏数据到内存，发送 SnpInvalidate 给所有 sharers，转为 clean）

#### Scenario: WriteBackFull 请求 - 状态转换
- **WHEN** 收到 WriteBackFull 请求
- **THEN** 状态转换遵循以下规则：
  - UD → I（写回数据到内存）
  - SD → I（写回数据到内存，通知所有 sharers invalidate）
  - 其他状态（UC/SC/I）→ 记录错误日志，忽略该请求（不应发生）

### Requirement: Sharer 跟踪
Cache 模型 SHALL 为每个 cache line 维护一个 sharer 集合，记录哪些 RN-F 持有该 line 的副本。

#### Scenario: 添加 sharer
- **WHEN** 处理 ReadShared 请求且 line 状态变为 SC 或 SD
- **THEN** 请求方的 NodeID 被添加到 sharer 集合

#### Scenario: 移除 sharer
- **WHEN** 发送 SnpInvalidate 给某个 RN-F 且收到响应
- **THEN** 该 RN-F 的 NodeID 从 sharer 集合中移除

#### Scenario: 清空所有 sharers
- **WHEN** 处理 ReadUnique 或 CleanUnique 请求，line 从 SC/SD 转为 UD/UC
- **THEN** 所有 sharer 收到 SnpInvalidate，sharer 集合清空

### Requirement: Snoop 操作 - SnpCleanInvalid
Cache 模型 SHALL 支持 SnpCleanInvalid 操作（gem5 中等价于 CHI SnpInvalidate），用于通知 RN-F 使其持有的 cache line 失效。

gem5 API：使用 `CHIRequestType_SnpCleanInvalid`，通过 `sendSnoopMsg()` 发送。

#### Scenario: 向单个 sharer 发送 SnpCleanInvalid
- **WHEN** line 状态为 SC，sharer 集合为 {RN0}，收到 ReadUnique 请求
- **THEN** 向 RN0 发送 SnpCleanInvalid 消息，地址为该 line 的地址

#### Scenario: 向多个 sharers 发送 SnpCleanInvalid
- **WHEN** line 状态为 SC，sharer 集合为 {RN0, RN1}，收到 ReadUnique 请求
- **THEN** 向 RN0 和 RN1 分别发送 SnpCleanInvalid 消息

#### Scenario: 等待 Snoop 响应
- **WHEN** 发送 SnpCleanInvalid 后
- **THEN** 在收到所有 SnpCleanInvalid 响应之前，不向请求方返回 CompData/Comp

### Requirement: Memory Fill 操作
Cache 模型 SHALL 支持从内存（SN-F）填充 cache line 数据。

#### Scenario: Cache miss 触发 fill
- **WHEN** ReadShared/ReadUnique/CleanUnique 请求在 L2 未命中
- **THEN** 向 SN-F 发送 ReadNoSnp 请求，等待 CompData 响应

#### Scenario: Fill 后更新 cache
- **WHEN** 收到 SN-F 的 CompData 响应
- **THEN** 将数据写入对应的 CacheLine，更新 tag 和状态

### Requirement: Writeback 操作
Cache 模型 SHALL 支持将脏 cache line 数据写回内存。

#### Scenario: 驱逐脏 line 触发 writeback
- **WHEN** 需要驱逐的 line 状态为 UD 或 SD
- **THEN** 先向 SN-F 发送 WriteNoSnp 写回脏数据，收到 WriteAck 后再 fill 新 line

#### Scenario: WriteBackFull 直接写回
- **WHEN** RN-F 发送 WriteBackFull 请求，携带脏数据
- **THEN** 将数据转发给 SN-F（WriteNoSnp），更新 line 状态为 I

### Requirement: Pending 事务管理
Cache 模型 SHALL 管理所有 in-flight 事务，使用 TxnID 作为事务标识。

#### Scenario: 创建 pending 事务
- **WHEN** 收到一个 cache miss 请求
- **THEN** 创建 PendingTxn 记录请求信息、原始请求方、当前状态

#### Scenario: 查找 pending 事务
- **WHEN** 收到 SN-F 的响应
- **THEN** 通过 TxnID 查找对应的 PendingTxn，继续处理

#### Scenario: 完成事务清理
- **WHEN** 事务处理完成（响应已发送给请求方）
- **THEN** 从 pendingTxns 中删除该事务记录
