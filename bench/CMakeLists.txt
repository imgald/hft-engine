add_executable(hft_bench
  bench_order_book.cpp
)

target_link_libraries(hft_bench PRIVATE engine)
target_include_directories(hft_bench PRIVATE ${CMAKE_SOURCE_DIR})
