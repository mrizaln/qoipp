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
    GIT_TAG v0.5.0
) # or use commit hash
FetchContent_MakeAvailable(qoipp)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE qoipp)
```

> No exception; all operation that can fail returns a `Result<T>`. On C++23 this is an `std::expected<T, Error>`.

```cpp
#include <qoipp.hpp>

int main()
{
    {    // read directly from file
        auto image = qoipp::decode_from_file("./path/to/file.qoi");
        if (not image) {
            std::cout << to_string(image.error()) << '\n';    // using ADL
            return 1;
        }

        // image->data     is the raw image bytes decoded from the file
        // image->desc     is the image description (width/height/channels/colorspace)

        // do something with the image data...
    }

    {    // decode data already in memory
        auto data = /* qoi image bytes read from file for example */;

        auto image = qoipp::decode(data);
        if (not image) {
            std::cout << to_string(image.error()) << '\n';    // using ADL
            return 1;
        }

        // do something with the image data...
    }

    {    // there's also an overload that can take a buffer as an out parameter
        auto data = /* qoi image bytes read from file for example */;

        auto buffer = Vec(/* width * height * channels */);
        auto desc   = qoipp::decode_into(buffer, data);
        if (not desc) {
            std::cout << to_string(desc.error()) << '\n';    // using ADL
            return 1;
        }

        // do something with the data in the buffer...
    }

    // those above are all decode* functions, the opposite encode* functions are also present
}
```

> See [example](./example) for more usage or read the [header](./include/qoipp.hpp) directly.
