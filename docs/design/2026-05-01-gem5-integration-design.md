# gem5 集成设计 — CHIGenericController 中间件架构

## 概述

将 CHI-new 模型集成到 gem5 的 Ruby CHI 协议中，替换 gem5 原生 Home Node，运行 128KB 连续访问测试程序进行端到端验证。

采用 `CHIGenericController`（C++ 控制器）而非 SLICC 状态机，以便自由调用 CHI-new 模型的 C++ 代码。

## 目标

- 创建 `CHIGenericController` 子类作为中间件，在 `recvRequestMsg` 回调中调用 CHI-new 模型
- gem5 CHI 消息 → 自定义消息 → CHI-new 模型处理 → gem5 CHI 消息，验证端到端正确性
- 请求日志（类型 + 地址）和计数统计

## 非目标

- 不实现完整缓存行为（直通模式）
- 不修改 gem5 原有 CHI 协议文件

## 架构

```
gem5 进程（Ruby CHI 模式）
┌──────────────────────────────────────────────────────────────────┐
│  Ruby 网络 (SimplePt2Pt, 4 VNet)                                │
│  ┌──────┐  ┌─────────────────┐  ┌──────────────────┐  ┌──────┐  │
│  │ CPU  │─▶│ L1 (SLICC)      │◀═▶│ CHIMiddleware    │──▶│ Mem  │  │
│  │      │  │ PrivateL1MOESI  │  │ (CHIGenericCtrl) │  │ Ctrl │  │
│  └──────┘  └─────────────────┘  └────────┬─────────┘  └──────┘  │
│                                          │                       │
│                                          │ 调用                   │
│                                          ▼                       │
│                               ┌──────────────────┐               │
│                               │ CHI-new 模型      │               │
│                               │ HnNode            │               │
│                               │ transformRequest()│               │
│                               └──────────────────┘               │
└──────────────────────────────────────────────────────────────────┘
```

## 为什么选择 CHIGenericController 而非 SLICC

| | SLICC | CHIGenericController |
|---|---|---|
| 语言 | DSL，编译生成 C++ | 直接写 C++ |
| 调外部代码 | 不支持 | 随意调用 |
| 适用场景 | 标准缓存协议 | 自定义逻辑 |
| 与 L1 兼容 | 是 | 是（demo 已验证） |

**核心原因：** SLICC 无法调用 CHI-new 模型的 C++ 代码，而 `CHIGenericController` 可以。

## 模块设计

### 1. 中间件控制器

**文件：** `src/mem/my_l2/our_l2_middleware.hh` / `our_l2_middleware.cc`

**Python 定义：** `src/mem/my_l2/OurL2Middleware.py`

```cpp
class OurL2Middleware : public CHIGenericController {
    // CHI-new 模型实例
    std::unique_ptr<chi::HnNode> hnNode;

    bool recvRequestMsg(const CHIRequestMsg* msg) override {
        // 1. gem5 CHI 消息 → CHI-new opcode
        // 2. 调用 hnNode->transformRequest()
        // 3. 根据请求类型分发：
        //    - CleanUnique → 直接返回 Comp
        //    - Write* → 发送 CompDBIDResp，等待数据
        //    - Read* → 转发到 Memory（DMT 模式）
    }

    bool recvDataMsg(const CHIDataMsg* msg) override {
        // L1 写数据 → 消费（不需响应）
    }

    bool recvResponseMsg(const CHIResponseMsg* msg) override {
        // Memory 响应 → 转发给 L1
    }
};
```

### 2. 消息转换

gem5 CHI 枚举 ↔ CHI-new `chi::Opcode` 映射：

**gem5 → CHI-new（gem5ToOpcode）：**

| gem5 CHIRequestType | chi::Opcode | 说明 |
|---|---|---|
| ReadShared | ReadShared | 读共享 |
| ReadUnique | ReadUnique | 读独占 |
| CleanUnique | CleanUnique | 特殊处理：直接返回 Comp |
| MakeReadUnique | ReadUnique | 升级为独占 |
| WriteBackFull | WriteBackFull | 写回 |
| WriteUniqueFull/Ptl/Zero | WriteBackFull | 写独占 |
| WriteEvictFull | WriteBackFull | 写逐出 |
| WriteCleanFull | WriteBackFull | 写清洁 |
| Evict | WriteBackFull | 逐出 |
| StashOnceShared/Unique | ReadShared | Stash 操作 |

**CHI-new → gem5（opcodeToGem5）：**

| chi::Opcode | gem5 CHIRequestType | 说明 |
|---|---|---|
| ReadNoSnp | ReadNoSnp | 读请求转发 |
| WriteNoSnp | WriteNoSnp | 写请求转发 |
| 其他 | ReadNoSnp（fallback） | 透传 opcode 的降级处理 |

### 3. 数据路由（DMT 模式）

对于读请求，使用 Direct Memory Transfer（DMT）模式：
- 中间件设置 `fwdRequestor = L1` 和 `dataToFwdRequestor = true`
- Memory 直接发送数据到 L1（不经过中间件）
- L1 发送 CompAck 到中间件（消费）

```
L1 → ReadShared → 中间件 → ReadNoSnp → Memory
                                    ↓
                             CompData → L1（直达）
L1 → CompAck → 中间件（消费）
```

### 4. 日志系统

CHI-new 模型使用独立的日志系统（`chi_log.hh`），与 gem5 的 DPRINTF 解耦：

```cpp
#include "chi_log.hh"

// 设置日志级别
chi::setLogLevel(chi::LogLevel::DEBUG);

// 使用宏
CHI_LOG_INFO("processRequest: opcode=%s", opcodeToString(op));
CHI_LOG_DEBUG("transformRequest: %s -> ReadNoSnp", ...);
CHI_LOG_WARN("pass-through opcode: %s", ...);
```

日志级别：NONE < ERROR < WARN < INFO < DEBUG < TRACE

### 5. Python 配置

```python
class OurL2Middleware(CHIGenericController):
    # 8 个 MessageBuffer（req/snp/rsp/dat × In/Out）
    # 连接到 Ruby 网络
```

### 6. 系统配置

```python
class OurL2CacheHierarchy(AbstractRubyCacheHierarchy):
    def incorporate_cache(self, board):
        # 创建 RubySystem + SimplePt2Pt 网络
        # 创建 L1 集群（PrivateL1MOESICache）
        # 创建 OurL2Middleware（替代 Home Node）
        # 创建 MemoryController
        # 设置 downstream_destinations
```

### 7. 构建集成

- `src/mem/my_l2/SConscript`：注册 SimObject + 编译 .cc
- 链接 CHI-new 静态库（`libchi_model`）
- include CHI-new 头文件路径

### 8. 测试命令

```bash
scons build/ARM/gem5.opt -j$(nproc)
./build/ARM/gem5.opt configs/example/arm/our_l2_hierarchy.py
```

## 决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| 集成方式 | CHIGenericController | 可自由调用 CHI-new C++ 代码 |
| 架构模式 | 中间件 + 回调 | 与 demo 一致，已验证可行 |
| 文件位置 | src/mem/my_l2/ | 沿用 demo 路径，不污染 gem5 源码 |
| 消息格式 | 直接使用 gem5 CHI 消息 | 直通模式，不需要自定义消息格式 |
