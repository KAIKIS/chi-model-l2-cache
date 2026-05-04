## MODIFIED Requirements

### Requirement: 直通模式 L2 Cache
L2 Cache 模型 SHALL 以带存储的真实 cache 模式运行，维护 tag array、data array 和一致性状态。

Cache 参数：
- 总容量：256KB（512 组 × 8-way × 64B line）
- 替换策略：LRU
- 一致性状态：I/UC/SC/UD/SD

#### Scenario: 收到 ReadShared 请求 - Cache 命中
- **WHEN** L2 Cache 收到 ReadShared 请求，且目标地址在 cache 中命中（状态为 UC/SC/UD/SD）
- **THEN** 直接返回数据给请求方，不访问内存，更新 LRU，添加请求方为 sharer

#### Scenario: 收到 ReadShared 请求 - Cache 未命中
- **WHEN** L2 Cache 收到 ReadShared 请求，且目标地址不在 cache 中（状态为 I 或不存在）
- **THEN** 向 SN-F 发送 ReadNoSnp 请求，等待 CompData 响应后 fill cache line，状态设为 SC，记录 sharer

#### Scenario: 收到 ReadUnique 请求 - Cache 命中且无其他 sharers
- **WHEN** L2 Cache 收到 ReadUnique 请求，目标地址在 cache 中命中，状态为 UC 或 UD
- **THEN** 直接返回数据，状态转为 UD，独占

#### Scenario: 收到 ReadUnique 请求 - Cache 命中且有其他 sharers
- **WHEN** L2 Cache 收到 ReadUnique 请求，目标地址在 cache 中命中，状态为 SC 或 SD，有其他 sharers
- **THEN** 发送 SnpInvalidate 给所有 sharers，等待响应后返回数据，状态转为 UD

#### Scenario: 收到 ReadUnique 请求 - Cache 未命中
- **WHEN** L2 Cache 收到 ReadUnique 请求，目标地址不在 cache 中
- **THEN** 向 SN-F 发送 ReadNoSnp 请求，等待 CompData 响应后 fill cache line，状态设为 UD

#### Scenario: 收到 CleanUnique 请求 - Cache 命中且有脏数据
- **WHEN** L2 Cache 收到 CleanUnique 请求，目标地址在 cache 中命中，状态为 UD 或 SD
- **THEN** 如有脏数据先写回内存，如有 sharers 发送 SnpInvalidate，状态转为 UC

#### Scenario: 收到 WriteBackFull 请求
- **WHEN** L2 Cache 收到 WriteBackFull 请求，携带脏数据
- **THEN** 将数据写入 cache line（或直接转发给 SN-F），状态转为 I

### Requirement: 响应转发
L2 Cache 模型 SHALL 根据缓存状态决定响应来源：cache hit 时本地生成响应，cache miss 时转发 SN-F 响应。

#### Scenario: Cache hit 响应
- **WHEN** 请求在 L2 cache 中命中
- **THEN** L2 直接生成 CompData 响应返回给请求方，不访问内存

#### Scenario: Cache miss 响应转发
- **WHEN** 请求在 L2 cache 中未命中，已向 SN-F 发送请求
- **THEN** 收到 SN-F 的 CompData 响应后，fill cache 并转发给请求方

### Requirement: 使用 CHI 模型接口
L2 Cache 模型 SHALL 作为独立的 C++ 类实现，不依赖 gem5 类型，可独立编译和单元测试。

#### Scenario: 接口独立性
- **WHEN** 编译 L2 Cache 模型
- **THEN** 仅依赖标准 C++ 库和 chi_model 的类型定义，不包含 gem5 头文件
