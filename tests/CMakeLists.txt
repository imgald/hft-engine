add_executable(hft_tests
  test_types.cpp
  test_order.cpp
  test_price_level.cpp
  test_order_book.cpp
  test_matching_engine.cpp
)

target_link_libraries(hft_tests
  PRIVATE
    engine
    GTest::gtest_main
)

target_include_directories(hft_tests PRIVATE ${CMAKE_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(hft_tests)
