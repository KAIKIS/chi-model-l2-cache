"""
Run test_128kb through gem5 with OurL2Middleware + OurL3Middleware (CHI Ruby).

Architecture:
    ARM CPU -> L1 (CHI/MOESI) -> L2 (OurL2Middleware, 256KB)
                               -> L3 (OurL3Middleware, 2MB)
                               -> MemoryController -> DDR

Usage:
    cd CHI-new/gem5
    scons build/ARM/gem5.opt -j$(nproc)
    ./build/ARM/gem5.opt ../test/gem5_run_l3_single_core.py
"""

from itertools import chain

import m5
from m5.objects import (
    MessageBuffer,
    OurL2Middleware,
    OurL3Middleware,
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
from gem5.components.cachehierarchies.chi.nodes.l1_cache import (
    L1CacheController,
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


def _setup_controller_ports(ctrl, network):
    """Create MessageBuffers and connect to network for a CHI controller."""
    for port_name in ("reqOut", "rspOut", "snpOut", "datOut",
                      "reqIn", "rspIn", "snpIn", "datIn"):
        setattr(ctrl, port_name, MessageBuffer())
    ctrl.reqOut.out_port = network.in_port
    ctrl.rspOut.out_port = network.in_port
    ctrl.snpOut.out_port = network.in_port
    ctrl.datOut.out_port = network.in_port
    ctrl.reqIn.in_port = network.out_port
    ctrl.rspIn.in_port = network.out_port
    ctrl.snpIn.in_port = network.out_port
    ctrl.datIn.in_port = network.out_port


class SingleCoreL3Hierarchy(AbstractRubyCacheHierarchy):
    """CHI hierarchy: L1 -> L2(256KB) -> L3(2MB) -> DDR."""

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

        network = self.ruby_system.network

        # L2 (OurL2Middleware, 256KB: 512 sets × 8 ways)
        self.l2 = OurL2Middleware(
            version=AbstractNode._version,
            data_channel_size=32,
            mandatoryQueue=MessageBuffer(),
        )
        self.l2.ruby_system = self.ruby_system
        _setup_controller_ports(self.l2, network)
        self.l2.clk_domain = board.get_clock_domain()

        # Bump version so L3 gets a distinct MachineID from L2
        AbstractNode._version += 1

        # L3 (OurL3Middleware, 2MB: 2048 sets × 16 ways)
        self.l3 = OurL3Middleware(
            version=AbstractNode._version,
            data_channel_size=32,
            mandatoryQueue=MessageBuffer(),
        )
        self.l3.ruby_system = self.ruby_system
        _setup_controller_ports(self.l3, network)
        self.l3.clk_domain = board.get_clock_domain()

        # L1 downstream -> L2
        for cluster in self.core_clusters:
            cluster.dcache.downstream_destinations = [self.l2]
            cluster.icache.downstream_destinations = [self.l2]

        # L2 downstream -> L3
        self.l2.downstream_destinations = [self.l3]

        # Memory Controller
        self.memory_controllers = self._create_memory_controllers(board)

        # L3 downstream -> Memory Controller
        self.l3.downstream_destinations = self.memory_controllers

        self.ruby_system.num_of_sequencers = len(self.core_clusters) * 2

        self.ruby_system.network.connectControllers(
            list(
                chain.from_iterable(
                    [(c.dcache, c.icache) for c in self.core_clusters]
                )
            )
            + self.memory_controllers
            + [self.l2, self.l3]
        )
        self.ruby_system.network.setup_buffers()

        self.ruby_system.sys_port_proxy = RubyPortProxy(
            ruby_system=self.ruby_system
        )
        board.connect_system_port(self.ruby_system.sys_port_proxy.in_ports)

    def _create_core_cluster(self, core, core_num, board):
        cluster = SubSystem()
        requires_evict = core.requires_send_evicts()
        cluster.dcache = L1CacheController(
            size="32KiB", assoc=8,
            network=self.ruby_system.network,
            requires_send_evicts=requires_evict,
            cache_line_size=board.get_cache_line_size(),
            target_isa=board.get_processor().get_isa(),
            clk_domain=board.get_clock_domain(),
        )
        cluster.icache = L1CacheController(
            size="32KiB", assoc=8,
            network=self.ruby_system.network,
            requires_send_evicts=requires_evict,
            cache_line_size=board.get_cache_line_size(),
            target_isa=board.get_processor().get_isa(),
            clk_domain=board.get_clock_domain(),
        )

        cluster.icache.sequencer = RubySequencer(
            version=core_num, dcache=NULL,
            clk_domain=cluster.icache.clk_domain,
            ruby_system=self.ruby_system,
            deadlock_threshold=50000000,
        )
        cluster.dcache.sequencer = RubySequencer(
            version=core_num, dcache=cluster.dcache.cache,
            clk_domain=cluster.dcache.clk_domain,
            ruby_system=self.ruby_system,
            deadlock_threshold=50000000,
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
        mcs = []
        for rng, port in board.get_mem_ports():
            mc = MemoryController(self.ruby_system.network, [rng], port)
            mc.ruby_system = self.ruby_system
            mcs.append(mc)
        return mcs


# --- Main ---
requires(
    isa_required=ISA.ARM,
    coherence_protocol_required=CoherenceProtocol.CHI,
)

cache_hierarchy = SingleCoreL3Hierarchy()
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

import os as _os
from gem5.resources.resource import BinaryResource

BP = _os.path.normpath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "build-aarch64"))
board.set_se_binary_workload(
    binary=BinaryResource(f"{BP}/test_128kb"),
)

print("=" * 60)
print("Single-core L1+L2+L3 via OurL2Middleware + OurL3Middleware (CHI Ruby)")
print("Architecture: CPU -> L1 -> L2(256KB) -> L3(2MB) -> DDR")
print("=" * 60)

simulator = Simulator(board=board, max_ticks=50_000_000_000_000)
simulator.run()

print("=" * 60)
print("Simulation complete.")
print("=" * 60)
