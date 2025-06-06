cmake_minimum_required(VERSION 3.16)
project(example VERSION 0.5.0)

include(cmake/prelude.cmake)
include(cmake/mold.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_COLOR_DIAGNOSTICS ON) # You might want to enable this (CMake 3.24+)

find_package(fmt REQUIRED)
find_package(stb REQUIRED)
find_package(range-v3 REQUIRED)
find_package(PerlinNoise REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

include(cmake/fetched-lib.cmake) # emits: qoi, qoixx

target_compile_features(range-v3::range-v3 INTERFACE cxx_std_20)

add_subdirectory(lib/qoipp) # emits qoipp target
add_subdirectory(lib/fpng) # emits fpng target

set(DEPS
    CLI11::CLI11
    fmt::fmt
    stb::stb
    fpng
    range-v3::range-v3
    siv::PerlinNoise
    qoipp
    qoi
    qoixx
)
set(COMPILE_FLAGS -Wall -Wextra -Wconversion -Wswitch-enum)

# ~~~
# set(LINK_FLAGS -fsanitize=address,leak,undefined)
# set(COMPILE_FLAGS -fsanitize=address,leak,undefined)

function(create_executable)
    cmake_parse_arguments(ARGS "" "NAME" "DEPS" ${ARGN})

    add_executable(${ARGS_NAME} source/${ARGS_NAME}.cpp)
    target_include_directories(${ARGS_NAME} PRIVATE source)
    target_link_libraries(${ARGS_NAME} PRIVATE ${ARGS_DEPS})
    target_compile_options(${ARGS_NAME} PRIVATE ${COMPILE_FLAGS})
    target_link_options(${ARGS_NAME} PRIVATE ${LINK_FLAGS})
endfunction()

create_executable(
    NAME qoigen
    DEPS CLI11::CLI11 fmt::fmt siv::PerlinNoise range-v3::range-v3 qoipp
)
create_executable(
    NAME qoiconv
    DEPS CLI11::CLI11 fmt::fmt stb::stb qoipp
)
create_executable(
    NAME qoibench
    DEPS CLI11::CLI11 fmt::fmt stb::stb fpng qoi qoixx qoipp
)
create_executable(
    NAME qoiswap
    DEPS CLI11::CLI11 fmt::fmt qoipp
)

# build fuzz on clang only
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    create_executable(NAME qoifuzz DEPS qoipp)
    target_compile_options(qoifuzz PRIVATE -fsanitize=address,fuzzer)
    target_link_options(qoifuzz PRIVATE -fsanitize=address,fuzzer)
endif()
