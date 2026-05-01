add_library(chi_model STATIC
    src/chi_transaction.cc
    src/chi_hn_node.cc
)
target_include_directories(chi_model PUBLIC include)
