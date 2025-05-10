include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  qoi
  GIT_REPOSITORY https://github.com/phoboslab/qoi
  GIT_TAG master)
FetchContent_MakeAvailable(qoi)

add_library(qoi INTERFACE)
target_include_directories(qoi SYSTEM INTERFACE ${qoi_SOURCE_DIR})

FetchContent_Declare(
  dtl-modern
  GIT_REPOSITORY https://github.com/mrizaln/dtl-modern
  GIT_TAG v1.0.0)
FetchContent_MakeAvailable(dtl-modern)
