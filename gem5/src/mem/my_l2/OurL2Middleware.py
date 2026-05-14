from m5.objects import CHIGenericController
from m5.params import *


class OurL2Middleware(CHIGenericController):
    type = "OurL2Middleware"
    cxx_header = "mem/my_l2/our_l2_middleware.hh"
    cxx_class = "gem5::ruby::OurL2Middleware"

    l2_num_sets = Param.Int(512, "Number of cache sets")
    l2_assoc    = Param.Int(8,   "Cache associativity (ways per set)")
