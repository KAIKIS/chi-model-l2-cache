# gem5 集成 — CHIGenericController 中间件实施计划

**Goal:** 用 `CHIGenericController`（C++ 控制器）替代 SLICC，将 CHI-new 模型集成到 gem5，运行 128KB 连续访问程序验证端到端正确性。

**Architecture:** 继承 `CHIGenericController`，在 `recvRequestMsg` 回调中调用 CHI-new 模型的 `HnNode::transformRequest()`，然后转发到 gem5 MemoryController。

**参考:** `/home/zhangkai/gem5/demo_readme.md`（已验证可行的中间件架构）

**设计文档:** `docs/design/2026-05-01-gem5-integration-design.md`

---

## 文件结构

```
src/mem/my_l2/
├── OurL2Middleware.py        # SimObject 定义
├── our_l2_middleware.hh      # 中间件头文件
├── our_l2_middleware.cc      # 中间件实现（调用 CHI-new 模型）
└── SConscript                # 构建配置（已存在，需修改）

configs/example/arm/
└── our_l2_hierarchy.py       # 系统配置脚本
```

---

## Task 1: SimObject 定义

**Files:**
- Create: `src/mem/my_l2/OurL2Middleware.py`

```python
from m5.params import *
from m5.SimObject import SimObject
from m5.objects.Controller import CHIGenericController

class OurL2Middleware(CHIGenericController):
    type = 'OurL2Middleware'
    cxx_header = 'mem/my_l2/our_l2_middleware.hh'
    cxx_class = 'gem5::OurL2Middleware'
```

---

## Task 2: 中间件实现

**Files:**
- Create: `src/mem/my_l2/our_l2_middleware.hh`
- Create: `src/mem/my_l2/our_l2_middleware.cc`

核心逻辑：
- `recvRequestMsg`: gem5 CHIRequestType → chi::Opcode → 调用 `HnNode::transformRequest()` → chi::Opcode → gem5 CHIRequestType → 发送到 Memory
- `recvDataMsg`: 转发 CompData 给 L1
- `recvResponseMsg`: 转发响应给 L1
- 日志打印：每个请求打印类型 + 地址 + 计数

---

## Task 3: 构建配置

**Files:**
- Modify: `src/mem/my_l2/SConscript`

添加 OurL2Middleware 的 SimObject 注册和源文件编译，链接 CHI-new 静态库。

---

## Task 4: 系统配置脚本

**Files:**
- Create: `configs/example/arm/our_l2_hierarchy.py`

参考 `chi_my_cache_hierarchy.py`，创建：
- RubySystem + SimplePt2Pt 网络（4 VNet）
- L1 集群（PrivateL1MOESICache）
- OurL2Middleware（替代 Home Node）
- MemoryController

---

## Task 5: 编译和测试

```bash
# 编译
scons build/ARM/gem5.opt -j$(nproc)

# 运行
./build/ARM/gem5.opt configs/example/arm/our_l2_hierarchy.py

# 预期输出
[OurL2] Request #1 type=ReadShared addr=0x...
[OurL2] Request #2 type=ReadShared addr=0x...
...
PASS
```

---

## 清理 SLICC 方案遗留文件

以下文件是 SLICC 方案的产物，新方案不需要：

- `gem5/src/mem/ruby/protocol/chi/CHI-L2Our.sm`
- `gem5/src/mem/ruby/protocol/chi/chi_bridge.hh`
- `gem5/src/mem/ruby/protocol/chi/chi_bridge.cc`
- `gem5/src/mem/ruby/protocol/chi/CHI.slicc` 中的 `include "CHI-L2Our.sm"`
- `gem5/configs/ruby/CHI_config.py` 中的 `CHI_L2OurController` 类
- `gem5/src/mem/ruby/protocol/RubySlicc_Exports.sm` 中的 `OurL2` MachineType
