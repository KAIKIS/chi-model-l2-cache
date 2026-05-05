"""
Run test_128kb through gem5 with OurL2Middleware (CHI Ruby network).

Architecture:
    ARM CPU -> L1 (CHI/MOESI) -> Network -> OurL2Middleware -> MemoryController

Usage:
    cd CHI-new/gem5
    scons build/ARM/gem5.opt -j$(nproc)
    ./build/ARM/gem5.opt ../test/gem5_run_128kb.py
"""

from itertools import chain

import m5
from m5.objects import (
    MessageBuffer,
    OurL2Middleware,
    NULL,
    RubyPortProxy,
    RubySequencer,
    RubySystem,
)
from m5.objects.SubSystem import SubSystem

from gem5.coherence_protocol import CoherenceProtocol
from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.abstract_cache_hierarchy import (
    AbstractCacheHierarchy,
)
from gem5.components.cachehierarchies.ruby.abstract_ruby_cache_hierarchy import (
    AbstractRubyCacheHierarchy,
)
from gem5.components.cachehierarchies.chi.nodes.abstract_node import (
    AbstractNode,
)
from gem5.components.cachehierarchies.chi.nodes.private_l1_moesi_cache import (
    PrivateL1MOESICache,
)
from gem5.components.cachehierarchies.chi.nodes.memory_controller import (
    MemoryController,
)
from gem5.components.cachehierarchies.ruby.topologies.simple_pt2pt import (
    SimplePt2Pt,
)
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.abstract_core import AbstractCore
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires
from gem5.utils.override import overrides


class SingleCoreL2Hierarchy(AbstractRubyCacheHierarchy):
    """CHI hierarchy with OurL2Middleware for single-core."""

    def __init__(self) -> None:
        super().__init__()

    @overrides(AbstractCacheHierarchy)
    def get_coherence_protocol(self):
        return CoherenceProtocol.CHI

    @overrides(AbstractRubyCacheHierarchy)
    def _reset_version_numbers(self):
        AbstractNode._version = 0
        MemoryController._version = 0

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board: AbstractBoard) -> None:
        super().incorporate_cache(board)
        self.ruby_system = RubySystem()
        self.ruby_system.network = SimplePt2Pt(self.ruby_system)
        self.ruby_system.number_of_virtual_networks = 4
        self.ruby_system.network.number_of_virtual_networks = 4

        # Create L1 clusters
        self.core_clusters = [
            self._create_core_cluster(core, i, board)
            for i, core in enumerate(board.get_processor().get_cores())
        ]

        # OurL2Middleware (Home Node)
        network = self.ruby_system.network
        self.directory = OurL2Middleware(
            version=AbstractNode._version,
            data_channel_size=32,
        )
        self.directory.ruby_system = self.ruby_system

        # MessageBuffers
        self.directory.reqOut = MessageBuffer()
        self.directory.rspOut = MessageBuffer()
        self.directory.snpOut = MessageBuffer()
        self.directory.datOut = MessageBuffer()
        self.directory.reqIn = MessageBuffer()
        self.directory.rspIn = MessageBuffer()
        self.directory.snpIn = MessageBuffer()
        self.directory.datIn = MessageBuffer()

        self.directory.reqOut.out_port = network.in_port
        self.directory.rspOut.out_port = network.in_port
        self.directory.snpOut.out_port = network.in_port
        self.directory.datOut.out_port = network.in_port
        self.directory.reqIn.in_port = network.out_port
        self.directory.rspIn.in_port = network.out_port
        self.directory.snpIn.in_port = network.out_port
        self.directory.datIn.in_port = network.out_port
        self.directory.clk_domain = board.get_clock_domain()

        # L1 downstream -> OurL2Middleware
        for cluster in self.core_clusters:
            cluster.dcache.downstream_destinations = [self.directory]
            cluster.icache.downstream_destinations = [self.directory]

        # Memory Controller
        self.memory_controllers = self._create_memory_controllers(board)
        self.directory.downstream_destinations = self.memory_controllers

        self.ruby_system.num_of_sequencers = len(self.core_clusters) * 2

        # Connect network
        self.ruby_system.network.connectControllers(
            list(
                chain.from_iterable(
                    [
                        (cluster.dcache, cluster.icache)
                        for cluster in self.core_clusters
                    ]
                )
            )
            + self.memory_controllers
            + [self.directory]
        )
        self.ruby_system.network.setup_buffers()

        # System port proxy
        self.ruby_system.sys_port_proxy = RubyPortProxy(
            ruby_system=self.ruby_system
        )
        board.connect_system_port(self.ruby_system.sys_port_proxy.in_ports)

    def _create_core_cluster(self, core, core_num, board):
        cluster = SubSystem()
        cluster.dcache = PrivateL1MOESICache(
            size="32KiB", assoc=8,
            network=self.ruby_system.network,
            core=core,
            cache_line_size=board.get_cache_line_size(),
            target_isa=board.get_processor().get_isa(),
            clk_domain=board.get_clock_domain(),
        )
        cluster.icache = PrivateL1MOESICache(
            size="32KiB", assoc=8,
            network=self.ruby_system.network,
            core=core,
            cache_line_size=board.get_cache_line_size(),
            target_isa=board.get_processor().get_isa(),
            clk_domain=board.get_clock_domain(),
        )

        cluster.icache.sequencer = RubySequencer(
            version=core_num, dcache=NULL,
            clk_domain=cluster.icache.clk_domain,
            ruby_system=self.ruby_system,
        )
        cluster.dcache.sequencer = RubySequencer(
            version=core_num, dcache=cluster.dcache.cache,
            clk_domain=cluster.dcache.clk_domain,
            ruby_system=self.ruby_system,
        )

        if board.has_io_bus():
            cluster.dcache.sequencer.connectIOPorts(board.get_io_bus())

        cluster.dcache.ruby_system = self.ruby_system
        cluster.icache.ruby_system = self.ruby_system

        core.connect_icache(cluster.icache.sequencer.in_ports)
        core.connect_dcache(cluster.dcache.sequencer.in_ports)
        core.connect_walker_ports(
            cluster.dcache.sequencer.in_ports,
            cluster.icache.sequencer.in_ports,
        )
        core.connect_interrupt()
        return cluster

    def _create_memory_controllers(self, board):
        memory_controllers = []
        for rng, port in board.get_mem_ports():
            mc = MemoryController(self.ruby_system.network, [rng], port)
            mc.ruby_system = self.ruby_system
            memory_controllers.append(mc)
        return memory_controllers


# --- Main ---
requires(
    isa_required=ISA.ARM,
    coherence_protocol_required=CoherenceProtocol.CHI,
)

cache_hierarchy = SingleCoreL2Hierarchy()
memory = SingleChannelDDR3_1600(size="256MiB")

processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING,
    isa=ISA.ARM,
    num_cores=1,
)

board = SimpleBoard(
    clk_freq="2GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Path to test binary (relative to gem5 root)
import os as _os
_script_dir = _os.path.dirname(_os.path.abspath(__file__))
_binary = _os.path.normpath(_os.path.join(_script_dir, "..", "build-aarch64", "test_128kb"))
from gem5.resources.resource import BinaryResource

board.set_se_binary_workload(binary=BinaryResource(_binary))

print("=" * 60)
print("Single-core test_128kb via OurL2Middleware (CHI Ruby)")
print(f"Binary: {_binary}")
print("Architecture: CPU -> L1(CHI/MOESI) -> Network -> OurL2Middleware -> Memory")
print("=" * 60)

simulator = Simulator(board=board, max_ticks=50_000_000_000_000)
simulator.run()

print("=" * 60)
print("Simulation complete.")
print("=" * 60)
