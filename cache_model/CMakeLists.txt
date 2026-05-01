add_library(cache_model STATIC
    src/simple_l2_cache.cc
)
target_include_directories(cache_model PUBLIC include)
target_link_libraries(cache_model PUBLIC chi_model)
