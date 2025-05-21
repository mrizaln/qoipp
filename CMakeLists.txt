cmake_minimum_required(VERSION 3.12)

project(
  qoipp
  VERSION 0.3.0
  LANGUAGES CXX
  DESCRIPTION "QOI codec written in C++20")

add_library(qoipp STATIC source/simple.cpp source/stream.cpp)
target_include_directories(qoipp PUBLIC include)
target_compile_features(qoipp PUBLIC cxx_std_20)
set_target_properties(qoipp PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(qoipp PRIVATE -march=native -mtune=native)
endif()
