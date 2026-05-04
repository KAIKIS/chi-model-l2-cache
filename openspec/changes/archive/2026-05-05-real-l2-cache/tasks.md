## 1. Cache 数据结构（纯 C++，不依赖 gem5）

- [ ] 1.1 在 cache_model 中新增 CacheLine 结构体（tag/state/data[64]/sharers），定义 LineState 枚举（I/UC/SC/UD/SD）
- [ ] 1.2 实现 CacheSet 类：N-way 组相联管理，tag lookup，LRU 计数器更新
- [ ] 1.3 实现 LRU 替换策略：访问更新 LRU、驱逐最久未使用 line、计数器溢出处理
- [ ] 1.4 实现 L2Cache 类：sets 数组（512 组 × 8-way = 256KB）、地址分解（tag/set/offset）、lookup 接口
- [ ] 1.5 实现 PendingTxn 管理：std::unordered_map<TxnID, PendingTxn>，创建/查找/清理接口

## 2. CHI 一致性状态机（L2Cache 内部逻辑）

- [ ] 2.1 实现 ReadShared 状态转换：I→SC（fill+sharer）、UC/SC→SC（+sharer）、UD/SD→SD（+sharer）
- [ ] 2.2 实现 ReadUnique 状态转换：I→UD（fill）、UC→UD、SC/SD→UD（snp+clear sharers）、UD→UD
- [ ] 2.3 实现 CleanUnique 状态转换：I→UC、UC→UC、SC→UC（snp）、UD→UC（writeback）、SD→UC（writeback+snp）
- [ ] 2.4 实现 WriteBackFull 状态转换：UD/SD→I（writeback），其他状态报错
- [ ] 2.5 实现 Memory Fill 操作：向 SN-F 发送请求，收到响应后 fill cache line
- [ ] 2.6 实现 Writeback 操作：驱逐脏 line 时先 writeback 再 fill，WriteBackFull 直接转发

## 3. Snoop 操作

- [ ] 3.1 在 chi_opcode.hh 中新增 SnpCleanInvalid opcode 和对应的 snoop 相关 opcode
- [ ] 3.2 实现 SnpCleanInvalid 发送逻辑：遍历 sharers 集合，为每个 sharer 构造 snoop 消息
- [ ] 3.3 实现 Snoop 响应处理：收到 SnpCleanInvalidResp 后从 sharers 集合移除，计数到齐后继续原请求
- [ ] 3.4 实现 Snoop 等待机制：pendingTxn 中记录等待的 snoop 响应数，全部到齐后才返回数据

## 4. OurL2Middleware 重写（同步模型）

- [ ] 4.1 重写 recvRequestMsg()：不再调用 hnNode->transformRequest()，改为调用 L2Cache::lookup() 同步处理
- [ ] 4.2 重写 recvDataMsg()：收到 SN-F 的 CompData 后执行 cache fill，然后将数据发送给原始请求方
- [ ] 4.3 实现 recvSnoopMsg()：处理来自其他 HN-F 的 snoop（当前场景下可简化为 no-op，因为只有一个 HN-F）
- [ ] 4.4 实现 Snoop 消息构造和发送：通过 gem5 的 snoop 通道向 RN-F 发送 SnpCleanInvalid
- [ ] 4.5 更新 SConscript：编译新增的 cache_model 源文件，更新 include 路径

## 5. gem5 2-Core 网络配置

- [ ] 5.1 修改 our_l2_hierarchy.py：num_cores=1 改为 num_cores=2
- [ ] 5.2 验证 NodeID 分配：确认 2-core 下各控制器的 NodeID 正确（core0.dcache=0, core0.icache=1, core1.dcache=2, core1.icache=3, OurL2Middleware=4, MemoryController=5）
- [ ] 5.3 测试 gem5 2-core 启动：确认 2 个 core 能正常启动并发送请求到 OurL2Middleware

## 6. 双核测试程序

- [ ] 6.1 编写 test_core0.cc：共享读场景（写入 N 个 cache line），写冲突场景（写入地址 X），乒乓场景
- [ ] 6.2 编写 test_core1.cc：共享读验证（读取并校验 checksum），写冲突验证（覆写后 Core0 读验证），乒乓验证
- [ ] 6.3 实现共享内存同步机制：atomic 变量 + 自旋 barrier
- [ ] 6.4 实现原始 syscall 输出：write(1, msg, len) 输出测试结果
- [ ] 6.5 交叉编译为 aarch64 静态链接二进制

## 7. 单元测试

- [ ] 7.1 编写 CacheLine 状态转换单元测试：覆盖所有 ReadShared/ReadUnique/CleanUnique/WriteBackFull 的状态转换路径
- [ ] 7.2 编写 LRU 替换单元测试：命中更新、驱逐最久未使用、计数器溢出
- [ ] 7.3 编写 Sharer 跟踪单元测试：添加/移除/清空 sharers
- [ ] 7.4 编写 L2Cache 集成测试：hit/miss/fill/evict 完整流程

## 8. gem5 集成测试

- [ ] 8.1 单核回归测试：1-core 配置运行 test_128kb_aarch64，确认 PASS
- [ ] 8.2 双核基本测试：2-core 配置运行 test_core0 + test_core1，确认 ALL TESTS PASSED
- [ ] 8.3 验证 gem5 stats：检查 stats.txt 中 L2 cache hit/miss 计数和 SnpCleanInvalid 次数
