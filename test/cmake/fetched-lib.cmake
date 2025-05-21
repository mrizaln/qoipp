include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    qoi
    GIT_REPOSITORY https://github.com/phoboslab/qoi
    GIT_TAG master
)
FetchContent_MakeAvailable(qoi)

add_library(qoi INTERFACE)
target_include_directories(qoi SYSTEM INTERFACE ${qoi_SOURCE_DIR})

FetchContent_Declare(
    dtl-modern
    GIT_REPOSITORY https://github.com/mrizaln/dtl-modern
    GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(dtl-modern)

FetchContent_Declare(
    ut
    URL https://github.com/boost-ext/ut/archive/refs/tags/v2.3.1.tar.gz
    URL_HASH MD5=e5be92edaeab228d1d4ccc87fed3faae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ut)
