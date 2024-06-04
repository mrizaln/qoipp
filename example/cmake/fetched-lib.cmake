include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  qoi
  GIT_REPOSITORY https://github.com/phoboslab/qoi
  GIT_TAG bf7b41c2ff3f24a2031193b62aa76d35e8842b5a)
FetchContent_MakeAvailable(qoi)

FetchContent_Declare(
  qoixx
  GIT_REPOSITORY https://github.com/wx257osn2/qoixx
  GIT_TAG 5f4bd086c2c245a11ac82ee74a2bc9d0b07c9bf4)
FetchContent_MakeAvailable(qoixx)

add_library(qoi INTERFACE)
target_include_directories(qoi INTERFACE ${qoi_SOURCE_DIR})

add_library(qoixx INTERFACE)
target_include_directories(qoixx INTERFACE ${qoixx_SOURCE_DIR}/include)
