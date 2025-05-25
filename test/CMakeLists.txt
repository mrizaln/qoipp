cmake_minimum_required(VERSION 3.16)
project(qoipp-test VERSION 0.5.0)

include(cmake/prelude.cmake)
include(cmake/mold.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_COLOR_DIAGNOSTICS ON) # You might want to enable this (CMake 3.24+)

find_package(fmt REQUIRED)
# find_package(ut REQUIRED)
find_package(range-v3 REQUIRED)
find_package(stb REQUIRED)

include(cmake/fetched-lib.cmake)

# The library
add_subdirectory(lib/qoipp)

function(create_test_exe NAME)
    add_executable(${NAME} source/${NAME}.cpp)
    target_include_directories(${NAME} PRIVATE resources)
    target_link_libraries(
        ${NAME}
        PRIVATE
            fmt::fmt
            Boost::ut
            range-v3::range-v3
            stb::stb
            qoipp
            qoi
            dtl-modern
    )
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wconversion)
    target_compile_options(${NAME} PRIVATE -fsanitize=address,leak,undefined)
    target_link_options(${NAME} PRIVATE -fsanitize=address,leak,undefined)

    add_test(NAME ${NAME} COMMAND $<TARGET_FILE:${NAME}>)
    # add_custom_command(TARGET ${NAME} POST_BUILD COMMAND ${NAME})
endfunction()

enable_testing()

create_test_exe(simple_test)
create_test_exe(stream_test)

add_custom_command(
    TARGET simple_test
    PRE_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/qoi_test_images
        ${CMAKE_CURRENT_BINARY_DIR}/resources/qoi_test_images
)

# copy the test data to the build directory
