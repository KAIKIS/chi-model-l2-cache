# gem5 集成设计 — OurL2 SLICC 控制器

## 概述

将 CHI-new 模型集成到 gem5 的 Ruby CHI 协议中，替换 gem5 原生 L2 Cache 控制器，运行 128KB 连续访问测试程序进行端到端验证。

## 目标

- 创建 SLICC 状态机 `CHI-L2Our.sm`，作为 gem5 Ruby 网络和 CHI-new 模型之间的桥接
- 在 SLICC action 中同步调用 CHI-new 的 `HnNode::transformRequest()` 进行 opcode 转换
- 在 L2 控制器中加入请求打印（类型 + 地址）和计数统计
- 运行 128KB 连续访问程序，验证端到端正确性

## 非目标

- 不实现完整的缓存行为（直通模式）
- 不使用 Channel 和异步线程（SLICC action 是同步的）
- 不修改 gem5 原有 CHI 协议文件

## 架构

```
gem5 进程（Ruby CHI 模式）
┌──────────────────────────────────────────────────────────┐
│  Ruby 网络                                               │
│  ┌──────┐  ┌─────┐        ┌──────────────┐  ┌────────┐  │
│  │ CPU  │─▶│ L1  │◀══════▶│ CHI-L2Our.sm │──▶│ Memory │  │
│  │      │  │(RN) │  网络   │ (SLCc)       │  │ (SN)   │  │
│  └──────┘  └─────┘        └──────┬───────┘  └────────┘  │
│                                  │ C++ 调用               │
│  ┌───────────────────────────────┼─────────────────────┐  │
│  │  CHI-new 静态库                │                     │  │
│  │  ┌────────────────────┐  ┌────┴──────────────┐     │  │
│  │  │ chi_middleware      │  │ HnNode            │     │  │
│  │  │ (opcode 转换)       │  │ (transformRequest)│     │  │
│  │  └────────────────────┘  └───────────────────┘     │  │
│  └─────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

## 模块设计

### 1. SLICC 状态机：CHI-L2Our.sm

**文件位置：** `gem5/src/mem/ruby/protocol/chi/CHI-L2Our.sm`

**状态：**
```slicc
state_declaration(State, default="OurL2_State_I") {
    I,     AccessPermission:Invalid, desc="Invalid/Ready";
    BUSY,  AccessPermission:Busy,    desc="Waiting for downstream response";
    null,  AccessPermission:Invalid, desc="Null state";
}
```

**事件：**
```slicc
enumeration(Event) {
    ReadShared,    desc="RN ReadShared",    in_trans="yes";
    ReadUnique,    desc="RN ReadUnique",    in_trans="yes";
    CleanUnique,   desc="RN CleanUnique",   in_trans="yes";
    WriteBackFull, desc="RN WriteBackFull", in_trans="yes";
    CompData,      desc="SN CompData";
    WriteAck,      desc="SN WriteAck";
    Comp,          desc="SN Comp";
    Finalize,      desc="Finalize txn";
    null,          desc="Null event";
}
```

**网络端口：**
- `reqIn` (VNet 0, From): 接收 RN 请求
- `reqOut` (VNet 0, To): 发送 SN 请求
- `datIn` (VNet 3, From): 接收 SN 数据响应
- `datOut` (VNet 3, To): 发送 RN 数据响应

**请求流程（ReadShared）：**
1. `reqIn` 收到 ReadShared → Event:ReadShared
2. Action: DPRINTF 打印 + chi_bridge.logRequest() 统计
3. Action: 调用 transformRequest() → ReadNoSnp
4. Action: 构造 CHIRequestMsg → reqOut 发送到 SN
5. 状态 I → BUSY

**响应流程（CompData）：**
1. `datIn` 收到 CompData → Event:CompData
2. Action: 构造 CHIDataMsg → datOut 发送到 RN
3. 状态 BUSY → I

**CleanUnique 特殊处理：**
1. 收到 CleanUnique → 直接构造 Comp 响应发回 RN
2. 不需要等 SN 响应，状态保持 I

### 2. C++ 桥接层

**文件：** `gem5/src/mem/ruby/protocol/chi/chi_bridge.hh` / `chi_bridge.cc`

```cpp
namespace gem5 { namespace ruby {

// 全局 SimpleL2Cache 实例
chi::SimpleL2Cache* getOurL2Cache();
void initOurL2Cache(chi::NodeID id);

// 请求转换：调用 HnNode::transformRequest()
int transformRequest(int reqType);

// 统计和打印
void logRequest(int reqType, uint64_t addr);
void printStats();

}}
```

- `transformRequest()`: 将 gem5 CHIRequestType 映射到 chi::Opcode，调用 `HnNode::transformRequest()`，返回转换后的 opcode
- `logRequest()`: 递增计数器，打印请求类型和地址
- `printStats()`: 仿真结束时输出总请求数

### 3. SLICC 注册

在 `CHI.slicc` 中添加：
```
include "CHI-L2Our.sm";
```

SLICC 编译器自动生成：
- `CHI/L2Our_Controller.{cc,hh}`
- `CHI/CHI_L2Our_Controller.py`
- `CHI/L2Our_Transitions.cc`
- `CHI/L2Our_Wakeup.cc`

### 4. Python 配置

在 `CHI_config.py` 中添加：
```python
class CHI_L2OurController(Base_CHI_Cache_Controller):
    def __init__(self, ruby_system, cache):
        super().__init__(ruby_system)
        self.sequencer = NULL
        self.cache = cache
        self.is_HN = False
        self.number_of_TBEs = 32
```

### 5. 构建集成

- 在 `chi/SConsopts` 中链接 CHI-new 静态库
- 桥接代码 `chi_bridge.cc` 作为额外源文件编译
- 需要 include CHI-new 的头文件路径

### 6. 测试配置

```bash
build/ARM/gem5.opt configs/example/se.py \
    --cpu-type=TimingSimpleCPU \
    --ruby \
    --chi-config=configs/ruby/our_chi_config.py \
    test_128kb_aarch64
```

### 7. 预期输出

```
[OurL2] Request #1 type=ReadShared addr=0x1000
[OurL2] Request #2 type=ReadShared addr=0x1040
[OurL2] Request #3 type=WriteBackFull addr=0x2000
...
[OurL2] Total requests: 1234
```

## 决策记录

| 决策 | 选择 | 替代方案 | 理由 |
|---|---|---|---|
| 集成方式 | SLICC + CHI-new 库调用 | 修改 gem5 现有 L2、纯 C++ 控制器 | 符合 gem5 框架，复用 CHI-new 模型 |
| 桥接方式 | 同步调用转换函数 | Channel + 异步线程 | SLICC action 同步执行，最简单 |
| 状态数 | 3 个（I, BUSY, null） | 完整缓存状态 | 直通模式不需要缓存状态 |
| 日志方式 | DPRINTF + stdout 打印 | 仅 gem5 stats | 满足用户打印需求 |

## 风险

- **SLICC 语法复杂度** → 缓解：参考现有 CHI-cache.sm，只实现最小子集
- **gem5 CHIRequestType 映射** → 缓解：桥接层处理类型转换
- **构建系统集成** → 缓解：CHI-new 作为外部静态库链接
