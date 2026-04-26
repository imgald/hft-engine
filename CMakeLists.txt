cmake_minimum_required(VERSION 3.20)
project(hft_engine VERSION 0.1.0 LANGUAGES CXX)

# ─── C++ Standard ────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build type default ──────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ─── Compiler flags ──────────────────────────────────────────
# Release: maximum optimization + native CPU ISA (AVX2 etc.)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -flto")
# Debug: sanitizers for correctness checking
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter        # too noisy during early dev
)

# ─── Output directories ──────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── Core engine library ─────────────────────────────────────
# Everything in engine/ compiles into a static lib so both
# the server binary and the test binary can link against it.
add_library(engine STATIC
  engine/order.cpp
  engine/price_level.cpp
  engine/order_book.cpp
  engine/matching_engine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR})

# ─── Google Test (fetched at configure time) ──────────────────
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Prevent GTest from overriding our compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ─── Tests ───────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ─── Benchmarks ──────────────────────────────────────────────
add_subdirectory(bench)

# ─── Print configuration summary ─────────────────────────────
message(STATUS "")
message(STATUS "═══ HFT Engine Build Configuration ═══")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std    : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "═══════════════════════════════════════")
message(STATUS "")
