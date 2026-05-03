add_executable(test_channel test_channel.cc)
target_link_libraries(test_channel PRIVATE chi_model pthread)

add_executable(test_hn_node test_hn_node.cc)
target_link_libraries(test_hn_node PRIVATE chi_model pthread)

add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)

add_executable(test_l2_cache test_l2_cache.cc)
target_link_libraries(test_l2_cache PRIVATE cache_model)

add_executable(test_dual_core test_dual_core.cc)
target_link_libraries(test_dual_core PRIVATE cache_model)
