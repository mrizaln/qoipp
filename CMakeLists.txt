cmake_minimum_required(VERSION 3.12)

project(
  qoipp
  VERSION 0.0.0
  LANGUAGES CXX
  DESCRIPTION "QOI codec written in C++20")

add_library(qoipp STATIC source/qoipp.cpp)
target_include_directories(qoipp PUBLIC include source)
target_compile_features(qoipp PUBLIC cxx_std_20)
set_target_properties(qoipp PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(qoipp PRIVATE -march=native -mtune=native -fno-rtti)
endif()

# build qoiconv only when qoipp is built directly not as a subproject
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(qoiconv source/qoiconv.cpp)
  target_link_libraries(qoiconv PRIVATE qoipp)
  target_compile_features(qoiconv PRIVATE cxx_std_20)
  set_target_properties(qoiconv PROPERTIES CXX_EXTENSIONS OFF)
endif()
