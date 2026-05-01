# test/run_gem5_se.py
# gem5 SE 模式配置：单核 aarch64，L2 使用 OurL2
# 注意：此脚本是骨架代码，实际运行时需要根据 gem5 Ruby CHI 网络配置调整

import m5
from m5.objects import *

# 系统配置
system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# CPU
system.cpu = TimingSimpleCPU()

# L1 Cache
system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()

# L2 Cache（使用 OurL2 替代 gem5 原生 L2）
system.l2 = OurL2()

# 内存
system.mem_ctrl = DDR3_1600_8x8()
system.mem_ctrl.port = system.l2.port

# 进程配置
process = Process()
process.cmd = ['test_128kb']
system.cpu.workload = process
system.cpu.createThreads()

# 根配置
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {event.getCause()}")
