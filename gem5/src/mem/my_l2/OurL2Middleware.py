from m5.objects import CHIGenericController
from m5.params import *


class OurL2Middleware(CHIGenericController):
    type = "OurL2Middleware"
    cxx_header = "mem/my_l2/our_l2_middleware.hh"
    cxx_class = "gem5::ruby::OurL2Middleware"
