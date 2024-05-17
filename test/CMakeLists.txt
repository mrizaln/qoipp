cmake_minimum_required(VERSION 3.16)
project(qoipp-test VERSION 0.0.1)

include(cmake/prelude.cmake)
include(cmake/mold.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_COLOR_DIAGNOSTICS ON) # You might want to enable this (CMake 3.24+)

find_package(fmt REQUIRED)
find_package(ut REQUIRED)
find_package(range-v3 REQUIRED)

# The library
add_subdirectory(lib/qoipp)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(qoipp PRIVATE QOIPP_DEBUG)
endif()

enable_testing()

add_executable(qoipp_test qoipp_test.cpp)
target_link_libraries(qoipp_test PRIVATE fmt::fmt Boost::ut range-v3::range-v3 qoipp)
target_compile_options(qoipp_test PRIVATE -Wall -Wextra -Wconversion)
target_compile_options(qoipp_test PRIVATE -fsanitize=address,leak,undefined)
target_link_options(qoipp_test PRIVATE -fsanitize=address,leak,undefined)

add_test(NAME qoipp_test COMMAND $<TARGET_FILE:qoipp_test>)
add_custom_command(
  TARGET qoipp_test
  POST_BUILD
  COMMAND qoipp_test)

# # sanitizer
# ~~~
