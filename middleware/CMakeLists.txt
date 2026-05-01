add_library(middleware STATIC
    src/chi_middleware.cc
)
target_include_directories(middleware PUBLIC include)
target_link_libraries(middleware PUBLIC chi_model)
