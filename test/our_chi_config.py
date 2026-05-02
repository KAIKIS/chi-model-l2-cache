"""
our_chi_config.py
Custom CHI configuration: uses OurL2 pass-through L2 controller
instead of the standard gem5 L2 cache controller.

Usage with gem5:
    build/ARM/gem5.opt configs/example/se.py \
        --chi-config=/path/to/our_chi_config.py ...
"""

import sys
import os

# Import everything from the default CHI_config
# Use absolute path since __file__ may not be available when loaded by gem5
sys.path.insert(0, '/home/zhangkai/work/CHI-new/gem5/configs/ruby')
from CHI_config import *


class OurL2_CHI_RNF(CHI_RNF):
    """
    CHI_RNF variant that uses OurL2 pass-through controller
    instead of the standard L2 cache controller.
    """

    def addPrivL2Cache(self, cache_type=None, pf_type=None):
        """Replace L2 with OurL2 pass-through controller"""
        self._ll_cntrls = []
        for cpu in self._cpus:
            cpu.l2 = CHI_L2OurController(self._ruby_system)
            self._cntrls.append(cpu.l2)

            # Manually create only the 6 buffers OurL2 SLICC declares
            # (do NOT use connectController — it creates snpOut/snpIn
            #  which OurL2 doesn't have, breaking L1 TBE credits)
            cpu.l2.reqOut = MessageBuffer()
            cpu.l2.rspOut = MessageBuffer()
            cpu.l2.datOut = MessageBuffer()
            cpu.l2.reqIn = MessageBuffer()
            cpu.l2.rspIn = MessageBuffer()
            cpu.l2.datIn = MessageBuffer()

            cpu.l2.reqOut.out_port = self._network.in_port
            cpu.l2.rspOut.out_port = self._network.in_port
            cpu.l2.datOut.out_port = self._network.in_port
            cpu.l2.reqIn.in_port = self._network.out_port
            cpu.l2.rspIn.in_port = self._network.out_port
            cpu.l2.datIn.in_port = self._network.out_port

            # L1 controllers now send downstream to OurL2
            for c in cpu._ll_cntrls:
                c.downstream_destinations = [cpu.l2]
            cpu._ll_cntrls = [cpu.l2]
            self._ll_cntrls.append(cpu.l2)


# Override CHI_RNF so create_system uses OurL2
CHI_RNF = OurL2_CHI_RNF
