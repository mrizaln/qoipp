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
find_package(stb REQUIRED)

include(cmake/fetched-lib.cmake)

# The library
add_subdirectory(lib/qoipp)

enable_testing()

add_executable(qoipp_test qoipp_test.cpp)
target_link_libraries(
    qoipp_test
    PRIVATE fmt::fmt Boost::ut range-v3::range-v3 stb::stb qoipp qoi
)
target_compile_options(qoipp_test PRIVATE -Wall -Wextra -Wconversion)
target_compile_options(qoipp_test PRIVATE -fsanitize=address,leak,undefined)
target_link_options(qoipp_test PRIVATE -fsanitize=address,leak,undefined)

add_test(NAME qoipp_test COMMAND $<TARGET_FILE:qoipp_test>)

add_custom_command(
    TARGET qoipp_test
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/qoi_test_images
        ${CMAKE_CURRENT_BINARY_DIR}/qoi_test_images
)

add_custom_command(TARGET qoipp_test POST_BUILD COMMAND qoipp_test)

# copy the test data to the build directory
