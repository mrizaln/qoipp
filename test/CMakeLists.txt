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
find_package(stb REQUIRED)

# The library
add_subdirectory(lib/qoipp)

add_executable(main source/main.cpp)
target_include_directories(main PRIVATE source)
target_link_libraries(main PRIVATE fmt::fmt Boost::ut stb::stb qoipp)
target_compile_options(main PRIVATE -Wall -Wextra -Wconversion -Wswitch-enum)

# sanitizer
target_compile_options(main PRIVATE -fsanitize=address,leak,undefined)
target_link_options(main PRIVATE -fsanitize=address,leak,undefined)
