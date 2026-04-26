cmake_minimum_required(VERSION 3.20)
project(hft_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -flto")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── Engine library ───────────────────────────────────────────
add_library(engine STATIC
  engine/order.cpp
  engine/price_level.cpp
  engine/order_book.cpp
  engine/matching_engine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR})

# ─── Network library ──────────────────────────────────────────
add_library(network STATIC
  network/fix_parser.cpp
  network/tcp_server.cpp
)
target_include_directories(network PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(network PUBLIC engine)

# ─── Server binary ────────────────────────────────────────────
add_executable(hft_server main.cpp)
target_link_libraries(hft_server PRIVATE network engine)

# ─── Google Test ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
add_subdirectory(bench)

message(STATUS "")
message(STATUS "═══ HFT Engine Build Configuration ═══")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std    : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "═══════════════════════════════════════")
message(STATUS "")
