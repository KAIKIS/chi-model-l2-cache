# gem5 集成 — OurL2 SLICC 控制器实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 CHI-new 模型集成到 gem5 Ruby CHI 协议中，运行 128KB 连续访问程序，验证端到端正确性并统计 L2 收到的请求数。

**Architecture:** 创建新 SLICC 机器 `CHI-L2Our.sm`，在 action 中同步调用 CHI-new 的 `HnNode::transformRequest()` 做 opcode 转换。C++ 桥接层负责类型映射和请求日志。

**Tech Stack:** SLICC, C++17, gem5 SCons, CHI-new 静态库

**设计文档:** `docs/design/2026-05-01-gem5-integration-design.md`

---

## 文件结构

```
gem5/src/mem/ruby/protocol/chi/
├── CHI.slicc                    # 修改：添加 include "CHI-L2Our.sm"
├── CHI-L2Our.sm                 # 新建：SLICC 状态机
├── chi_bridge.hh                # 新建：C++ 桥接层头文件
└── chi_bridge.cc                # 新建：C++ 桥接层实现

gem5/configs/ruby/
└── CHI_config.py                # 修改：添加 CHI_L2OurController 类

CHI-new/
└── test/
    └── our_chi_config.py        # 新建：自定义 CHI 配置（使用 OurL2）
```

---

## Task 1: C++ 桥接层

**Files:**
- Create: `gem5/src/mem/ruby/protocol/chi/chi_bridge.hh`
- Create: `gem5/src/mem/ruby/protocol/chi/chi_bridge.cc`

- [ ] **Step 1: 创建 chi_bridge.hh**

```cpp
#ifndef __MEM_RUBY_PROTOCOL_CHI_CHI_BRIDGE_HH__
#define __MEM_RUBY_PROTOCOL_CHI_CHI_BRIDGE_HH__

#include <cstdint>
#include <string>

namespace gem5 {

namespace ruby {

// 初始化 OurL2（在控制器 init 时调用）
void initOurL2(int node_id);

// 请求类型转换：将 L1 发来的请求类型转换为发往 SN 的类型
// 输入：gem5 CHIRequestType 枚举值
// 输出：转换后的 CHIRequestType 枚举值
int transformRequestType(int req_type);

// 记录请求（打印类型和地址，递增计数器）
void logOurL2Request(int req_type, uint64_t addr);

// 获取总请求数
uint64_t getOurL2RequestCount();

// 打印统计信息（仿真结束时调用）
void printOurL2Stats();

// 获取请求类型名称
const char* getChIRequestTypeName(int req_type);

} // namespace ruby
} // namespace gem5

#endif // __MEM_RUBY_PROTOCOL_CHI_CHI_BRIDGE_HH__
```

- [ ] **Step 2: 创建 chi_bridge.cc**

```cpp
#include "chi_bridge.hh"

#include <iostream>
#include <string>

// CHI-new 模型头文件
#include "chi_hn_node.hh"
#include "chi_opcode.hh"
#include "chi_transaction.hh"

namespace gem5 {
namespace ruby {

static uint64_t g_request_count = 0;
static chi::HnNode* g_hn_node = nullptr;

void initOurL2(int node_id) {
    g_hn_node = new chi::HnNode(node_id);
    std::cout << "[OurL2] Initialized with node_id=" << node_id << std::endl;
}

// gem5 CHIRequestType → chi::Opcode 映射
static chi::Opcode gem5ReqTypeToOpcode(int req_type) {
    // gem5 CHIRequestType 枚举值（来自 CHI-msg.sm）
    // ReadShared=9, ReadUnique=12, CleanUnique=14, WriteBackFull=17
    switch (req_type) {
        case 9:  return chi::Opcode::ReadShared;     // ReadShared
        case 12: return chi::Opcode::ReadUnique;     // ReadUnique
        case 14: return chi::Opcode::CleanUnique;    // CleanUnique
        case 17: return chi::Opcode::WriteBackFull;  // WriteBackFull
        default: return chi::Opcode::ReadShared;
    }
}

// chi::Opcode → gem5 CHIRequestType 映射
static int opcodeToGem5ReqType(chi::Opcode op) {
    switch (op) {
        case chi::Opcode::ReadNoSnp:  return 38;  // ReadNoSnp
        case chi::Opcode::WriteNoSnp: return 36;  // WriteNoSnp
        default: return 38;
    }
}

int transformRequestType(int req_type) {
    chi::Opcode in_opcode = gem5ReqTypeToOpcode(req_type);

    // 构造 ChiTransaction
    chi::ChiTransaction txn;
    txn.opcode = in_opcode;

    // 调用 HnNode 的 transformRequest
    chi::ChiTransaction result = g_hn_node->transformRequest(txn);

    return opcodeToGem5ReqType(result.opcode);
}

void logOurL2Request(int req_type, uint64_t addr) {
    g_request_count++;
    std::cout << "[OurL2] Request #" << g_request_count
              << " type=" << getChIRequestTypeName(req_type)
              << " (enum=" << req_type << ")"
              << " addr=0x" << std::hex << addr << std::dec
              << std::endl;
}

uint64_t getOurL2RequestCount() {
    return g_request_count;
}

void printOurL2Stats() {
    std::cout << "\n[OurL2] === Statistics ===" << std::endl;
    std::cout << "[OurL2] Total requests: " << g_request_count << std::endl;
    std::cout << "[OurL2] ===================" << std::endl;
}

const char* getChIRequestTypeName(int req_type) {
    switch (req_type) {
        case 0:  return "Load";
        case 1:  return "Store";
        case 2:  return "StoreLine";
        case 9:  return "ReadShared";
        case 10: return "ReadNotSharedDirty";
        case 11: return "ReadOnce";
        case 12: return "ReadUnique";
        case 14: return "CleanUnique";
        case 17: return "WriteBackFull";
        case 18: return "WriteCleanFull";
        case 19: return "WriteEvictFull";
        case 36: return "WriteNoSnp";
        case 38: return "ReadNoSnp";
        default: return "Unknown";
    }
}

} // namespace ruby
} // namespace gem5
```

- [ ] **Step 3: 验证编译（单独编译桥接文件）**

```bash
cd /home/zhangkai/work/CHI-new
# 测试桥接文件是否能独立编译
g++ -std=c++17 -c gem5/src/mem/ruby/protocol/chi/chi_bridge.cc \
    -I chi_model/include \
    -o /tmp/chi_bridge.o
```

Expected: 编译成功

- [ ] **Step 4: 提交**

```bash
git add gem5/src/mem/ruby/protocol/chi/chi_bridge.hh gem5/src/mem/ruby/protocol/chi/chi_bridge.cc
git commit -m "feat: add C++ bridge layer for gem5 CHI integration"
```

---

## Task 2: SLICC 状态机 CHI-L2Our.sm

**Files:**
- Create: `gem5/src/mem/ruby/protocol/chi/CHI-L2Our.sm`
- Modify: `gem5/src/mem/ruby/protocol/chi/CHI.slicc`

- [ ] **Step 1: 创建 CHI-L2Our.sm**

```
/**
 * CHI-L2Our.sm
 * OurL2: 极简直通 L2 控制器，桥接 gem5 Ruby 网络和 CHI-new 模型
 *
 * 状态：I（空闲）、BUSY（等待下游响应）、null
 * 功能：接收 L1 请求 → 转换 opcode → 转发到 SN → 转发响应回 L1
 */

machine(MachineType:OurL2, "OurL2 pass-through cache controller") :

  // 延迟参数
  Cycles request_latency := 1;
  Cycles response_latency := 1;
  Cycles data_latency := 1;
  Cycles stall_recycle_lat := 1;

  // 网络消息缓冲区
  MessageBuffer * reqOut,   network="To", virtual_network="0", vnet_type="none";
  MessageBuffer * rspOut,   network="To", virtual_network="2", vnet_type="none";
  MessageBuffer * datOut,   network="To", virtual_network="3", vnet_type="response";
  MessageBuffer * reqIn,    network="From", virtual_network="0", vnet_type="none";
  MessageBuffer * rspIn,    network="From", virtual_network="2", vnet_type="none";
  MessageBuffer * datIn,    network="From", virtual_network="3", vnet_type="response";

  // 内部消息缓冲区（必须有，AbstractController 需要）
  MessageBuffer * mandatoryQueue;
  MessageBuffer * triggerQueue;

{
  // ===== 状态声明 =====
  state_declaration(State, default="OurL2_State_I") {
    I,    AccessPermission:Invalid, desc="Idle, ready to accept requests";
    BUSY, AccessPermission:Busy,    desc="Waiting for downstream response";
    null, AccessPermission:Invalid, desc="Null state";
  }

  // ===== 事件声明 =====
  enumeration(Event) {
    // 来自 L1 的请求
    ReadShared,    desc="ReadShared from L1",    in_trans="yes";
    ReadUnique,    desc="ReadUnique from L1",    in_trans="yes";
    CleanUnique,   desc="CleanUnique from L1",   in_trans="yes";
    WriteBackFull, desc="WriteBackFull from L1", in_trans="yes";

    // 来自 SN 的响应
    CompData,      desc="CompData from SN";
    WriteAck,      desc="WriteAck from SN";
    Comp_Resp,     desc="Comp from SN";

    // 完成事务
    Finalize,      desc="Finalize and return to idle";

    null,          desc="Null event";
  }

  // ===== 结构体 =====
  structure(TBE, desc="Transaction buffer entry") {
    Addr addr,            desc="Request address";
    CHIRequestType reqType, desc="Original request type";
    MachineID requestor,  desc="Original requestor";
    State state,          desc="TBE state";
  }

  // ===== TBE 表 =====
  Structure(TBEs, TBE, "TBEs", desc="Transaction buffer entries") {
    isResource=true;
    size=32;
  }

  // ===== 入口 =====
  entry: OurL2_State_I;

  // ===== 出站端口 =====
  out_port(reqOutPort, CHIRequestMsg, reqOut);
  out_port(rspOutPort, CHIResponseMsg, rspOut);
  out_port(datOutPort, CHIDataMsg, datOut);

  // ===== 入站端口：请求（来自 L1） =====
  in_port(reqInPort, CHIRequestMsg, reqIn, rank=5,
          rsc_stall_handler=reqInPort_rsc_stall_handler) {
    if (reqInPort.isReady(clockEdge())) {
      peek(reqInPort, CHIRequestMsg) {
        trigger(reqToEvent(in_msg.type), in_msg.addr,
                getCacheEntry(in_msg.addr), getTBE(in_msg.addr));
      }
    }
  }
  bool reqInPort_rsc_stall_handler() {
    error("reqInPort must never stall\n");
    return false;
  }

  // ===== 入站端口：响应（来自 SN） =====
  in_port(rspInPort, CHIResponseMsg, rspIn, rank=10,
          rsc_stall_handler=rspInPort_rsc_stall_handler) {
    if (rspInPort.isReady(clockEdge())) {
      peek(rspInPort, CHIResponseMsg) {
        trigger(rspToEvent(in_msg.type), in_msg.addr,
                getCacheEntry(in_msg.addr), getTBE(in_msg.addr));
      }
    }
  }
  bool rspInPort_rsc_stall_handler() {
    error("rspInPort must never stall\n");
    return false;
  }

  // ===== 入站端口：数据（来自 SN） =====
  in_port(datInPort, CHIDataMsg, datIn, rank=9,
          rsc_stall_handler=datInPort_rsc_stall_handler) {
    if (datInPort.isReady(clockEdge())) {
      peek(datInPort, CHIDataMsg) {
        trigger(datToEvent(in_msg.type), in_msg.addr,
                getCacheEntry(in_msg.addr), getTBE(in_msg.addr));
      }
    }
  }
  bool datInPort_rsc_stall_handler() {
    error("datInPort must never stall\n");
    return false;
  }

  // ===== 辅助函数 =====
  function(reqToEvent, CHIRequestType, Event) {
    if (in_type == CHIRequestType:ReadShared) {
      return Event:ReadShared;
    }
    if (in_type == CHIRequestType:ReadUnique) {
      return Event:ReadUnique;
    }
    if (in_type == CHIRequestType:CleanUnique) {
      return Event:CleanUnique;
    }
    if (in_type == CHIRequestType:WriteBackFull) {
      return Event:WriteBackFull;
    }
    error("OurL2: Unknown request type");
    return Event:null;
  }

  function(rspToEvent, CHIResponseType, Event) {
    if (in_type == CHIResponseType:Comp) {
      return Event:Comp_Resp;
    }
    if (in_type == CHIResponseType:Comp_UC) {
      return Event:Comp_Resp;
    }
    if (in_type == CHIResponseType:Comp_I) {
      return Event:Comp_Resp;
    }
    return Event:Comp_Resp;
  }

  function(datToEvent, CHIDataType, Event) {
    if (in_type == CHIDataType:CompData_I) {
      return Event:CompData;
    }
    if (in_type == CHIDataType:CompData_UC) {
      return Event:CompData;
    }
    if (in_type == CHIDataType:CompData_SC) {
      return Event:CompData;
    }
    if (in_type == CHIDataType:CompData_UD_PD) {
      return Event:CompData;
    }
    if (in_type == CHIDataType:CBWrData_I) {
      return Event:WriteAck;
    }
    return Event:CompData;
  }

  // ===== Actions =====

  // 处理 ReadShared：转换为 ReadNoSnp 并转发
  action(a_HandleReadShared, desc="Handle ReadShared from L1") {
    // 分配 TBE
    check_allocate(TBEs);
    TBEs.incrementReserved();

    // 记录请求
    debug_printf("[OurL2] ReadShared addr=%p\n", address);

    // 通过桥接层转换 opcode: ReadShared → ReadNoSnp
    // ReadShared=9, ReadNoSnp=38
    int sn_req_type := 38;  // ReadNoSnp

    // 构造并发送 SN 请求
    peek(reqInPort, CHIRequestMsg) {
      enqueue(reqOutPort, CHIRequestMsg, request_latency) {
        out_msg.addr := in_msg.addr;
        out_msg.type := CHIRequestType:ReadNoSnp;
        out_msg.requestor := in_msg.requestor;
        out_msg.Destination := in_msg.Destination;
        out_msg.allowRetry := false;
        out_msg.is_local_pf := false;
        out_msg.is_remote_pf := false;
      }

      // 保存 TBE 信息
      TBE tbe := getTBE(in_msg.addr);
      tbe.addr := in_msg.addr;
      tbe.reqType := in_msg.type;
      tbe.requestor := in_msg.requestor;
    }

    reqInPort.dequeue(clockEdge());
  }

  // 处理 ReadUnique：转换为 ReadNoSnp 并转发
  action(a_HandleReadUnique, desc="Handle ReadUnique from L1") {
    check_allocate(TBEs);
    TBEs.incrementReserved();

    debug_printf("[OurL2] ReadUnique addr=%p\n", address);

    peek(reqInPort, CHIRequestMsg) {
      enqueue(reqOutPort, CHIRequestMsg, request_latency) {
        out_msg.addr := in_msg.addr;
        out_msg.type := CHIRequestType:ReadNoSnp;
        out_msg.requestor := in_msg.requestor;
        out_msg.Destination := in_msg.Destination;
        out_msg.allowRetry := false;
        out_msg.is_local_pf := false;
        out_msg.is_remote_pf := false;
      }

      TBE tbe := getTBE(in_msg.addr);
      tbe.addr := in_msg.addr;
      tbe.reqType := in_msg.type;
      tbe.requestor := in_msg.requestor;
    }

    reqInPort.dequeue(clockEdge());
  }

  // 处理 WriteBackFull：转换为 WriteNoSnp 并转发
  action(a_HandleWriteBackFull, desc="Handle WriteBackFull from L1") {
    check_allocate(TBEs);
    TBEs.incrementReserved();

    debug_printf("[OurL2] WriteBackFull addr=%p\n", address);

    peek(reqInPort, CHIRequestMsg) {
      enqueue(reqOutPort, CHIRequestMsg, request_latency) {
        out_msg.addr := in_msg.addr;
        out_msg.type := CHIRequestType:WriteNoSnp;
        out_msg.requestor := in_msg.requestor;
        out_msg.Destination := in_msg.Destination;
        out_msg.allowRetry := false;
        out_msg.is_local_pf := false;
        out_msg.is_remote_pf := false;
      }

      TBE tbe := getTBE(in_msg.addr);
      tbe.addr := in_msg.addr;
      tbe.reqType := in_msg.type;
      tbe.requestor := in_msg.requestor;
    }

    reqInPort.dequeue(clockEdge());
  }

  // 处理 CleanUnique：直接回复 Comp，不转发到 SN
  action(a_HandleCleanUnique, desc="Handle CleanUnique - direct Comp response") {
    debug_printf("[OurL2] CleanUnique addr=%p (direct Comp)\n", address);

    peek(reqInPort, CHIRequestMsg) {
      enqueue(rspOutPort, CHIResponseMsg, response_latency) {
        out_msg.addr := in_msg.addr;
        out_msg.type := CHIResponseType:Comp;
        out_msg.Destination := getDestination(in_msg.requestor);
      }
    }

    reqInPort.dequeue(clockEdge());
  }

  // 处理 CompData 响应：转发给 L1
  action(a_ForwardCompData, desc="Forward CompData to L1") {
    debug_printf("[OurL2] CompData addr=%p (forward to L1)\n", address);

    peek(datInPort, CHIDataMsg) {
      enqueue(datOutPort, CHIDataMsg, data_latency) {
        out_msg := in_msg;
      }
    }

    datInPort.dequeue(clockEdge());

    // 释放 TBE
    TBE tbe := getTBE(address);
    TBEs.decrementReserved();
    clearTBE(address);
  }

  // 处理 WriteAck 响应：转发给 L1
  action(a_ForwardWriteAck, desc="Forward WriteAck to L1") {
    debug_printf("[OurL2] WriteAck addr=%p (forward to L1)\n", address);

    peek(rspInPort, CHIResponseMsg) {
      enqueue(rspOutPort, CHIResponseMsg, response_latency) {
        out_msg := in_msg;
      }
    }

    rspInPort.dequeue(clockEdge());

    TBE tbe := getTBE(address);
    TBEs.decrementReserved();
    clearTBE(address);
  }

  // ===== 转换表 =====

  // 请求处理：I → BUSY
  transition(I, ReadShared, BUSY) {
    a_HandleReadShared;
  }

  transition(I, ReadUnique, BUSY) {
    a_HandleReadUnique;
  }

  transition(I, WriteBackFull, BUSY) {
    a_HandleWriteBackFull;
  }

  // CleanUnique 不需要等 SN 响应，保持 I
  transition(I, CleanUnique, I) {
    a_HandleCleanUnique;
  }

  // 响应处理：BUSY → I
  transition(BUSY, CompData, I) {
    a_ForwardCompData;
  }

  transition(BUSY, WriteAck, I) {
    a_ForwardWriteAck;
  }

  transition(BUSY, Comp_Resp, I) {
    a_ForwardWriteAck;  // 复用相同的转发逻辑
  }
}
```

- [ ] **Step 2: 修改 CHI.slicc 添加 include**

在 `gem5/src/mem/ruby/protocol/chi/CHI.slicc` 中添加一行：

```
protocol "CHI" partial_func_reads;

include "CHI-msg.sm";
include "CHI-cache.sm";
include "CHI-mem.sm";
include "CHI-dvm-misc-node.sm";
include "CHI-L2Our.sm";
```

- [ ] **Step 3: 提交**

```bash
git add gem5/src/mem/ruby/protocol/chi/CHI-L2Our.sm gem5/src/mem/ruby/protocol/chi/CHI.slicc
git commit -m "feat: add OurL2 SLICC state machine for pass-through L2 cache"
```

---

## Task 3: Python 控制器配置

**Files:**
- Modify: `gem5/configs/ruby/CHI_config.py`

- [ ] **Step 1: 在 CHI_config.py 末尾添加 CHI_L2OurController 类**

```python
class CHI_L2OurController(OurL2_Controller):
    """
    OurL2 pass-through L2 cache controller.
    Uses CHI-new model for opcode transformation.
    """

    def __init__(self, ruby_system):
        super().__init__(
            version=Versions.getVersion(OurL2_Controller),
            ruby_system=ruby_system,
            mandatoryQueue=MessageBuffer(),
            triggerQueue=TriggerMessageBuffer(),
        )
        self.transitions_per_cycle = 1024
```

注意：`OurL2_Controller` 是 SLICC 编译器从 `CHI-L2Our.sm` 自动生成的类。

- [ ] **Step 2: 提交**

```bash
git add gem5/configs/ruby/CHI_config.py
git commit -m "feat: add CHI_L2OurController Python configuration"
```

---

## Task 4: 自定义 CHI 系统配置

**Files:**
- Create: `CHI-new/test/our_chi_config.py`

- [ ] **Step 1: 创建 our_chi_config.py**

此文件基于 `gem5/configs/ruby/CHI_config.py`，修改 `CHI_RNF` 类使用 OurL2 控制器。

```python
"""
our_chi_config.py
自定义 CHI 配置：使用 OurL2 替代 gem5 原生 L2 控制器
"""

from m5.objects import *
from m5.util import panic

# 导入默认配置中的所有内容
import sys
sys.path.insert(0, '/home/zhangkai/work/CHI-new/gem5/configs/ruby')
from CHI_config import *

# 修改 CHI_RNF 的 addPrivL2Cache 方法使用 OurL2
class OurL2_CHI_RNF(CHI_RNF):
    def addPrivL2Cache(self, cache_type=None, pf_type=None):
        """替换 L2 为 OurL2 控制器"""
        for cpu in self._cpus:
            # 创建 OurL2 控制器（不需要 cache 参数）
            cpu.l2 = CHI_L2OurController(self._ruby_system)
            self._cntrls.append(cpu.l2)
            self.connectController(cpu.l2)
            # L1 现在发送下游到 OurL2
            for c in cpu._ll_cntrls:
                c.downstream_destinations = [cpu.l2]
            cpu._ll_cntrls = [cpu.l2]

# 覆盖节点类型
CHI_RNF = OurL2_CHI_RNF
```

- [ ] **Step 2: 提交**

```bash
git add CHI-new/test/our_chi_config.py
git commit -m "feat: add custom CHI config using OurL2 controller"
```

---

## Task 5: gem5 构建配置

**Files:**
- Modify: `gem5/src/mem/ruby/protocol/chi/SConsopts`（可能需要添加链接配置）

- [ ] **Step 1: 修改 SConsopts 添加 CHI-new 库路径**

在 `gem5/src/mem/ruby/protocol/chi/SConsopts` 中添加：

```python
Import('*')

main.Append(PROTOCOL_DIRS=[Dir('.')])

# 链接 CHI-new 静态库
chi_new_build = '/home/zhangkai/work/CHI-new/build'
main.Append(LIBPATH=[chi_new_build])
main.Append(LIBS=['chi_model', 'cache_model', 'middleware'])
main.Append(CPPPATH=['/home/zhangkai/work/CHI-new/chi_model/include'])
```

- [ ] **Step 2: 确保 chi_bridge.cc 被编译**

在 `gem5/src/mem/ruby/protocol/SConscript` 中，SLICC 生成的文件会自动被编译。但 `chi_bridge.cc` 不是 SLICC 生成的，需要手动添加。

检查 SConscript 中是否有机制添加额外源文件。如果没有，在 SConscript 中添加：

```python
# 在 CHI 协议相关代码区域添加
Source('chi/chi_bridge.cc')
```

- [ ] **Step 3: 提交**

```bash
git add gem5/src/mem/ruby/protocol/chi/SConsopts
git commit -m "feat: configure gem5 build to link CHI-new library"
```

---

## Task 6: 测试程序交叉编译

**Files:**
- 已有: `CHI-new/test/test_128kb.cc`
- 已有: `CHI-new/test/aarch64-toolchain.cmake`

- [ ] **Step 1: 交叉编译 test_128kb**

```bash
cd /home/zhangkai/work/CHI-new
mkdir -p build-aarch64 && cd build-aarch64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../test/aarch64-toolchain.cmake
make test_128kb
```

- [ ] **Step 2: 验证二进制**

```bash
file /home/zhangkai/work/CHI-new/build-aarch64/test/test_128kb
```

Expected: `ELF 64-bit LSB executable, ARM aarch64`

- [ ] **Step 3: 复制到 gem5 可访问路径**

```bash
cp /home/zhangkai/work/CHI-new/build-aarch64/test/test_128kb /home/zhangkai/work/CHI-new/test/test_128kb_aarch64
```

---

## Task 7: gem5 编译

- [ ] **Step 1: 增量编译 gem5（CHI 协议）**

```bash
cd /home/zhangkai/work/CHI-new/gem5
scons build/ARM/gem5.opt -j$(nproc)
```

Expected: 编译成功，生成 `build/ARM/gem5.opt`

注意：首次编译可能需要较长时间。如果遇到 SLICC 编译错误，检查 `CHI-L2Our.sm` 语法。

- [ ] **Step 2: 如果编译失败，检查 SLICC 错误**

常见问题：
- `MachineType:OurL2` 未在 Python 端注册 → 检查 CHI_config.py
- TBE 相关函数不存在 → 检查 SLICC 语法
- 消息类型不匹配 → 检查 CHIRequestType/CHIResponseType 枚举

---

## Task 8: 运行测试

- [ ] **Step 1: 运行 gem5 SE 模式**

```bash
cd /home/zhangkai/work/CHI-new/gem5
build/ARM/gem5.opt configs/example/se.py \
    --cpu-type=TimingSimpleCPU \
    --num-cpus=1 \
    --ruby \
    --chi-config=/home/zhangkai/work/CHI-new/test/our_chi_config.py \
    --cmd=/home/zhangkai/work/CHI-new/test/test_128kb_aarch64 \
    --mem-size=512MB
```

- [ ] **Step 2: 观察输出**

预期看到：
```
[OurL2] Initialized with node_id=...
[OurL2] Request #1 type=ReadShared addr=0x...
[OurL2] Request #2 type=ReadShared addr=0x...
...
[OurL2] Total requests: N
```

- [ ] **Step 3: 检查 gem5 stats**

```bash
cat m5out/stats.txt | grep -i "our_l2\|request\|requestType"
```

- [ ] **Step 4: 验证程序输出**

检查 test_128kb 的输出是否为 `PASS`。

---

## Task 9: 在 SLICC action 中添加 C++ 桥接调用

**Files:**
- Modify: `gem5/src/mem/ruby/protocol/chi/CHI-L2Our.sm`

- [ ] **Step 1: 在 SLICC action 中调用桥接函数**

修改 `a_HandleReadShared` action，在开头添加：

```
action(a_HandleReadShared, desc="Handle ReadShared from L1") {
    // 调用 C++ 桥接层记录请求
    chi_bridge.logOurL2Request(9, address);  // 9 = ReadShared

    // ... 原有逻辑 ...
}
```

对所有 request action 类似处理：
- `a_HandleReadUnique`: `chi_bridge.logOurL2Request(12, address);`
- `a_HandleCleanUnique`: `chi_bridge.logOurL2Request(14, address);`
- `a_HandleWriteBackFull`: `chi_bridge.logOurL2Request(17, address);`

- [ ] **Step 2: 在 SLICC 中声明桥接对象引用**

在 `machine()` 声明中添加外部 C++ 对象引用（如果 SLICC 支持），或者在 action 中直接 include 桥接头文件。

如果 SLICC 不支持直接调用 C++ 函数，则通过 `DPRINTF` 打印，在 C++ 桥接层的 `logOurL2Request` 中统计。

- [ ] **Step 3: 重新编译并运行**

```bash
cd /home/zhangkai/work/CHI-new/gem5
scons build/ARM/gem5.opt -j$(nproc)
```

---

## 自检清单

- [ ] SLICC 语法正确（参考 CHI-cache.sm 模式）
- [ ] MachineType:OurL2 在 Python 端正确注册
- [ ] 网络端口声明完整（req/rsp/dat In/Out）
- [ ] TBE 表正确声明和使用
- [ ] 转换表覆盖所有请求和响应事件
- [ ] C++ 桥接层能独立编译
- [ ] CHI-new 静态库正确链接
- [ ] 测试程序交叉编译为 aarch64
