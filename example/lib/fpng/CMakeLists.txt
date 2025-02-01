cmake_minimum_required(VERSION 3.12)

project(
  fpng
  VERSION 1.0.6
  LANGUAGES CXX
  DESCRIPTION "fast png write/reader")

add_library(fpng STATIC fpng.cpp)
target_include_directories(fpng PUBLIC include)
target_compile_features(fpng PUBLIC cxx_std_20)
target_compile_options(fpng PRIVATE -msse4.1 -mpclmul)
set_target_properties(fpng PROPERTIES CXX_EXTENSIONS OFF)
