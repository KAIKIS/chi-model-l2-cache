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
# When loaded via gem5's SourceFileLoader, __file__ may not be defined,
# so we search for the gem5 ruby config in known locations.
_gem5_ruby_config = None
_candidates = []

try:
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    _candidates.append(os.path.join(os.path.dirname(_script_dir), 'gem5', 'configs', 'ruby'))
except NameError:
    pass

# Fallback: search from CWD
_candidates.append(os.path.join(os.getcwd(), 'gem5', 'configs', 'ruby'))
# Fallback: environment variable
_chi_root = os.environ.get('CHI_NEW_ROOT')
if _chi_root:
    _candidates.append(os.path.join(_chi_root, 'gem5', 'configs', 'ruby'))

for _p in _candidates:
    if os.path.isfile(os.path.join(_p, 'CHI_config.py')):
        _gem5_ruby_config = _p
        break

if _gem5_ruby_config is None:
    raise RuntimeError("Cannot find gem5 CHI_config.py. Searched: " + str(_candidates))

sys.path.insert(0, _gem5_ruby_config)
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
            self.connectController(cpu.l2)

            # L1 controllers now send downstream to OurL2
            for c in cpu._ll_cntrls:
                c.downstream_destinations = [cpu.l2]
            cpu._ll_cntrls = [cpu.l2]
            self._ll_cntrls.append(cpu.l2)


# Override CHI_RNF so create_system uses OurL2
CHI_RNF = OurL2_CHI_RNF
