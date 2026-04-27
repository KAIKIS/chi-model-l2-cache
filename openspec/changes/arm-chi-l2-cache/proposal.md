## Why

需要实现一个可复用的 ARM CHI 协议模型，并通过一个简易的 L2 Cache 模型来验证其正确性。最终目标是将 CHI 模型以源码集成的方式应用到 gem5 仿真器中，替代 gem5 原有的 L2 Cache 控制器，验证 ARM CHI 协议通路的正确性。

## What Changes

- 新建 CHI 协议模型（Transaction 级抽象），提供通用的事务类型定义、Channel 通信机制和 HN-F 节点实现
- 新建简易 L2 Cache 模型（功能模型，直通模式），作为 HN-F 节点接收 L1 请求并转发到 Memory
- 新建 gem5 集成层（SLCc 控制器 + Middleware），以最小化 gem5 代码修改的方式接入 CHI 模型
- 新建验证程序（128KB 连续访问的 C++ 程序），用于端到端验证整个通路
- 使用 CMake 构建 CHI 模型静态库，使用 git 进行版本管理

## Capabilities

### New Capabilities

- `chi-model`: CHI 协议引擎，包含 Transaction 数据结构、Opcode 枚举、Channel 通信模板和 HN-F 节点基类。不绑定任何仿真器，可独立复用。
- `l2-cache-model`: 基于 CHI Model 的简易 L2 Cache 模型，以直通模式运行（无缓存状态），负责将 RN 请求转换为 SN 请求并转发。
- `gem5-integration`: gem5 集成层，包含 SLCc 控制器（新增 .sm/.py 文件，不修改 gem5 原有代码）和 Middleware（gem5 消息格式与 ChiTransaction 的转换）。
- `verification`: 验证框架，包含 128KB 连续访问的 C++ 测试程序和 gem5 SE 模式运行配置。

### Modified Capabilities

（无，这是全新项目）

## Impact

- **新增代码**: CHI-new 项目全部为新增代码，包含 chi_model、cache_model、middleware、test 四个模块
- **gem5 修改**: 仅新增文件（.sm 状态机 + .py 配置 + SConscript 注册），不修改 gem5 原有代码
- **依赖**: gem5 源码（用于集成编译和 SLCc 框架）、C++17 编译器、CMake、aarch64 交叉编译工具链
- **构建系统**: CHI-new 使用 CMake 构建静态库，gem5 侧通过 SConscript 链接
