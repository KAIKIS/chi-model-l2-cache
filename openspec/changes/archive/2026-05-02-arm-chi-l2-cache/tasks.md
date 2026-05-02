## 1. 项目初始化

- [x] 1.1 创建项目目录结构（chi_model、cache_model、middleware、test、gem5_integration）
- [x] 1.2 编写顶层 CMakeLists.txt，配置 C++17 标准和静态库目标
- [x] 1.3 初始化 git 仓库，添加 .gitignore

## 2. CHI 模型 — 基础类型

- [x] 2.1 实现 chi_types.hh：定义 NodeID、TxnID 等基础类型别名
- [x] 2.2 实现 chi_opcode.hh：定义 Opcode 枚举（ReadShared、ReadUnique、CleanUnique、WriteBackFull、ReadNoSnp、WriteNoSnp、CompData、WriteAck、Comp）
- [x] 2.3 实现 chi_transaction.hh/cc：定义 ChiTransaction 结构体（txnID、opcode、addr、size、data、respStatus）

## 3. CHI 模型 — Channel 通信

- [x] 3.1 实现 chi_channel.hh：Channel<T> 模板类，支持阻塞式 push/pop（mutex + condition_variable）

## 4. CHI 模型 — HN-F 节点

- [x] 4.1 实现 chi_node.hh：定义 ChiNode 基类接口（nodeID、nodeType）
- [x] 4.2 实现 chi_hn_node.hh/cc：实现 HnNode 类，包含 RN 侧和 SN 侧的 Channel，以及 process() 主循环
- [x] 4.3 实现 HnNode 的 opcode 转换逻辑（ReadShared→ReadNoSnp、ReadUnique→ReadNoSnp、WriteBackFull→ReadNoSnp、CleanUnique→Comp）
- [x] 4.4 编写 CMakeLists.txt（chi_model 子目录），编译 libchi_model.a

## 5. L2 Cache 模型

- [x] 5.1 实现 simple_l2_cache.hh/cc：继承 HnNode，实现直通模式的 L2 Cache
- [x] 5.2 实现请求转发：RN 请求 → opcode 转换 → SN 请求
- [x] 5.3 实现响应转发：SN 响应 → RN 响应
- [x] 5.4 编写 CMakeLists.txt（cache_model 子目录），编译静态库

## 6. CHI 模型单元测试

- [x] 6.1 编写 Channel 单元测试：验证 push/pop 阻塞行为
- [x] 6.2 编写 HnNode 单元测试：验证 ReadShared 请求的完整转发流程
- [x] 6.3 编写 HnNode 单元测试：验证 ReadUnique、WriteBackFull、CleanUnique 的转发
- [x] 6.4 编写 test/CMakeLists.txt，集成测试可执行文件

## 7. Middleware — gem5 格式转换

- [x] 7.1 实现 chi_middleware.hh/cc：gem5 CHIRequestType 与 ChiTransaction Opcode 的双向映射
- [x] 7.2 实现 gem5 消息到 ChiTransaction 的转换函数
- [x] 7.3 实现 ChiTransaction 到 gem5 消息的转换函数
- [x] 7.4 编写 CMakeLists.txt（middleware 子目录），编译静态库

## 8. gem5 集成 — CHIGenericController

> 注：原计划使用 SLICC 状态机（8.1-8.3），实际改为 CHIGenericController 方案，更简洁。

- [x] 8.1 实现 OurL2Middleware（CHIGenericController），替代 SLICC 状态机方案
- [x] 8.2 编写 OurL2Middleware.py：SimObject 配置类
- [x] 8.3 编写 SConscript：注册 SimObject 和编译配置，集成 CHI-new 源码
- [x] 8.4 将 CHI-new 模型源码集成到 gem5 构建系统（SConscript 直接编译 .cc 文件）

## 9. 验证 — 128KB 测试程序

- [x] 9.1 编写 test_128kb.cc：分配 128KB 内存，顺序写入，顺序读取，输出校验和
- [x] 9.2 编写 aarch64 交叉编译脚本或 CMake 工具链文件
- [x] 9.3 编写 gem5 SE 模式运行配置（our_l2_hierarchy.py）

## 10. 端到端验证

- [x] 10.1 编译 CHI-new 静态库
- [x] 10.2 编译 gem5（集成 OurL2 控制器）
- [x] 10.3 编译测试程序（aarch64 静态链接）
- [x] 10.4 运行 gem5 SE 模式，验证程序输出正确的校验和
- [x] 10.5 检查 gem5 日志，确认 L2 收到的事务类型和数量符合预期
