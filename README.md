# qoipp

A Quite OK Image ([QOI](https://qoiformat.org/)) format encoder/decoder written in C++20.

## Dependencies

- C++20

## Usage

You can use CMake FetchContent to add this repo to your project.

```cmake
include(FetchContent)
FetchContent_Declare(
  qoipp
  GIT_REPOSITORY https://github.com/mrizaln/qoipp
  GIT_TAG v0.1.0)     # or use commit hash (recommended)
FetchContent_MakeAvailable(qoipp)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE qoipp)
```

```cpp
#include <qoipp.hpp>

int main()
{
    qoipp::Image image = qoipp::decode_from_file("./path/to/file.qoi");

    // image.m_data     is the raw image bytes decoded from the file
    // image.m_desc     is the image description (width/height/channels/colorspace)

    // do somthing with the image data...
}
```

> See [example](./example) for more usage
