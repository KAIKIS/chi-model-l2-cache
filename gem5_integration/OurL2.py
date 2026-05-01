# gem5_integration/OurL2.py
from m5.params import *
from m5.objects.ClockedObject import ClockedObject

class OurL2(ClockedObject):
    type = "OurL2"
    cxx_header = "mem/ruby/protocol/our_l2.hh"
    cxx_class = "gem5::OurL2"

    # 端口
    system_port = RequestPort("System port")

    # 参数
    num_entries = Param.Int(64, "Number of in-flight entries")
