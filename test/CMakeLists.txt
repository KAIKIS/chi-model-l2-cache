add_executable(test_channel test_channel.cc)
target_link_libraries(test_channel PRIVATE chi_model pthread)

add_executable(test_hn_node test_hn_node.cc)
target_link_libraries(test_hn_node PRIVATE chi_model pthread)

add_executable(test_128kb test_128kb.cc)
target_link_libraries(test_128kb PRIVATE chi_model cache_model)
