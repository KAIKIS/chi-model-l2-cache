add_library(cache_model STATIC
    src/l2_cache.cc
    src/chi_protocol_engine.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
