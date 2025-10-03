#include "qoipp/common.hpp"

#include "util.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace qoipp
{
    namespace fs = std::filesystem;

    Result<Desc> read_header(ByteCSpan in_data) noexcept
    {
        if (in_data.size() == 0) {
            return make_error<Desc>(Error::Empty);
        } else if (in_data.size() < constants::header_size) {
            return make_error<Desc>(Error::TooShort);
        }

        auto magic = std::array<char, constants::magic.size()>{};
        std::memcpy(magic.data(), in_data.data(), magic.size());

        if (not std::ranges::equal(magic, constants::magic)) {
            return make_error<Desc>(Error::NotQoi);
        }

        auto index = constants::magic.size();

        u32 width, height;

        std::memcpy(&width, in_data.data() + index, sizeof(u32));
        index += sizeof(u32);
        std::memcpy(&height, in_data.data() + index, sizeof(u32));
        index += sizeof(u32);

        auto channels   = to_channels(in_data[index++]);
        auto colorspace = to_colorspace(in_data[index++]);

        if (not channels or not colorspace or width == 0 or height == 0) {
            return make_error<Desc>(Error::InvalidDesc);
        }

        return Desc{
            .width      = util::to_native_endian(width),
            .height     = util::to_native_endian(height),
            .channels   = *channels,
            .colorspace = *colorspace,
        };
    }

    Result<Desc> read_header(const fs::path& in_path) noexcept
    {
        if (not fs::exists(in_path)) {
            return make_error<Desc>(Error::FileNotExists);
        } else if (not fs::is_regular_file(in_path)) {
            return make_error<Desc>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ in_path, std::ios::binary };
        if (not file.is_open()) {
            return make_error<Desc>(Error::IoError);
        }

        auto data = ByteArr<constants::header_size>{};
        file.read(reinterpret_cast<char*>(data.data()), constants::header_size);
        if (not file) {
            return make_error<Desc>(Error::IoError);
        }

        return read_header(data);
    }
}
