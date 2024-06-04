#include "qoipp.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace sv = std::views;

// utils ana aliases
namespace qoipp
{
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using usize = std::size_t;
    using isize = std::ptrdiff_t;

    using f32 = float;
    using f64 = double;

    using Byte = std::byte;

    template <usize N>
    using ByteArr = std::array<Byte, N>;
    using ByteVec = std::vector<Byte>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <typename T>
    concept ByteRange = std::ranges::range<T> and std::same_as<std::ranges::range_value_t<T>, Byte>;

    template <typename T, usize N>
    consteval ByteArr<N> toBytes(const T (&value)[N])
    {
        ByteArr<N> result;
        for (usize i : sv::iota(0u, N)) {
            result[i] = static_cast<Byte>(value[i]);
        }
        return result;
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T toBigEndian(const T& value) noexcept
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        } else {
            T result  = 0;
            result   |= (value & 0x000000FF) << 24;
            result   |= (value & 0x0000FF00) << 8;
            result   |= (value & 0x00FF0000) >> 8;
            result   |= (value & 0xFF000000) >> 24;
            return result;
        }
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T fromBigEndian(const T& value) noexcept
    {
        return toBigEndian(value);
    }

    struct Pixel
    {
        u8 m_r;
        u8 m_g;
        u8 m_b;
        u8 m_a;

        constexpr auto operator<=>(const Pixel&) const = default;
    };

    static_assert(std::is_trivial_v<Pixel>);
}

namespace qoipp::constants
{
    constexpr std::array<char, 4> magic = { 'q', 'o', 'i', 'f' };

    constexpr usize      headerSize       = 14;
    constexpr usize      runningArraySize = 64;
    constexpr ByteArr<8> endMarker        = toBytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

    constexpr i8 biasOpRun    = -1;
    constexpr i8 biasOpDiff   = 2;
    constexpr i8 biasOpLumaG  = 32;
    constexpr i8 biasOpLumaRB = 8;
    constexpr i8 runLimit     = 62;
    constexpr i8 minDiff      = -2;
    constexpr i8 maxDiff      = 1;
    constexpr i8 minLumaG     = -32;
    constexpr i8 maxLumaG     = 31;
    constexpr i8 minLumaRB    = -8;
    constexpr i8 maxLumaRB    = 7;

    constexpr Pixel start = { 0x00, 0x00, 0x00, 0xFF };
}

namespace qoipp::data
{
    // clang-format off
    template <typename T, typename R>
    concept DataChunk = std::ranges::range<R> and requires(const T t, R& r, usize& i) {
        { t.write(r, i) } noexcept -> std::same_as<void>;
    };
    // clang-format on

    template <typename T>
    concept DataChunkVec = DataChunk<T, ByteVec>;

    inline void write32(ByteVec& vec, usize& index, u32 value) noexcept
    {
        vec[index++] = static_cast<Byte>((value >> 24) & 0xFF);
        vec[index++] = static_cast<Byte>((value >> 16) & 0xFF);
        vec[index++] = static_cast<Byte>((value >> 8) & 0xFF);
        vec[index++] = static_cast<Byte>(value & 0xFF);
    }

    struct QoiHeader
    {
        std::array<char, 4> m_magic = constants::magic;

        u32 m_width;
        u32 m_height;
        u8  m_channels;
        u8  m_colorspace;

        void write(ByteVec& vec, usize& index) const noexcept
        {
            for (char c : m_magic) {
                vec[index++] = static_cast<Byte>(c);
            }

            write32(vec, index, m_width);
            write32(vec, index, m_height);

            vec[index++] = static_cast<Byte>(m_channels);
            vec[index++] = static_cast<Byte>(m_colorspace);
        }
    };
    static_assert(DataChunkVec<QoiHeader>);

    struct EndMarker
    {
        static void write(ByteVec& vec, usize& index) noexcept
        {
            for (auto byte : constants::endMarker) {
                vec[index++] = byte;
            }
        }
    };
    static_assert(DataChunkVec<EndMarker>);

    namespace op
    {
        enum Tag : u8
        {
            OP_RGB   = 0b11111110,
            OP_RGBA  = 0b11111111,
            OP_INDEX = 0b00000000,
            OP_DIFF  = 0b01000000,
            OP_LUMA  = 0b10000000,
            OP_RUN   = 0b11000000,
        };

        struct Rgb
        {
            u8 m_r = 0;
            u8 m_g = 0;
            u8 m_b = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RGB);
                vec[index++] = static_cast<Byte>(m_r);
                vec[index++] = static_cast<Byte>(m_g);
                vec[index++] = static_cast<Byte>(m_b);
            }
        };
        static_assert(DataChunkVec<Rgb>);

        struct Rgba
        {
            u8 m_r = 0;
            u8 m_g = 0;
            u8 m_b = 0;
            u8 m_a = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RGBA);
                vec[index++] = static_cast<Byte>(m_r);
                vec[index++] = static_cast<Byte>(m_g);
                vec[index++] = static_cast<Byte>(m_b);
                vec[index++] = static_cast<Byte>(m_a);
            }
        };
        static_assert(DataChunkVec<Rgba>);

        struct Index
        {
            u32 m_index = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_INDEX | m_index);
            }
        };
        static_assert(DataChunkVec<Index>);

        struct Diff
        {
            i8 m_dr = 0;
            i8 m_dg = 0;
            i8 m_db = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                constexpr auto bias = constants::biasOpDiff;

                vec[index++] = static_cast<Byte>(
                    OP_DIFF | (m_dr + bias) << 4 | (m_dg + bias) << 2 | (m_db + bias)
                );
            }
        };
        static_assert(DataChunkVec<Diff>);

        struct Luma
        {
            i8 m_dg    = 0;
            i8 m_dr_dg = 0;
            i8 m_db_dg = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                constexpr auto biasG  = constants::biasOpLumaG;
                constexpr auto biasRB = constants::biasOpLumaRB;

                vec[index++] = static_cast<Byte>(OP_LUMA | (m_dg + biasG));
                vec[index++] = static_cast<Byte>((m_dr_dg + biasRB) << 4 | (m_db_dg + biasRB));
            }
        };
        static_assert(DataChunkVec<Luma>);

        struct Run
        {
            i8 m_run = 0;

            void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RUN | (m_run + constants::biasOpRun));
            }
        };
        static_assert(DataChunkVec<Run>);

        template <typename T>
        concept Op = AnyOf<T, Rgb, Rgba, Index, Diff, Luma, Run>;

        inline bool shouldDiff(i8 dr, i8 dg, i8 db) noexcept
        {
            return dr >= constants::minDiff && dr <= constants::maxDiff    //
                && dg >= constants::minDiff && dg <= constants::maxDiff    //
                && db >= constants::minDiff && db <= constants::maxDiff;
        }

        inline bool shouldLuma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
        {
            return dr_dg >= constants::minLumaRB && dr_dg <= constants::maxLumaRB    //
                && db_dg >= constants::minLumaRB && db_dg <= constants::maxLumaRB    //
                && dg >= constants::minLumaG && dg <= constants::maxLumaG;
        }
    }
}

namespace qoipp::impl
{
    using RunningArray = std::array<Pixel, constants::runningArraySize>;

    class DataChunkArray
    {
    public:
        DataChunkArray(usize size)
            : m_bytes(size)
        {
        }

        template <typename T>
            requires(data::op::Op<T> or AnyOf<T, data::QoiHeader, data::EndMarker>)
        void push(T&& t) noexcept
        {
            t.write(m_bytes, m_index);
        }

        ByteVec get() noexcept
        {
            m_bytes.resize(m_index);
            return std::exchange(m_bytes, {});
        }

    private:
        ByteVec m_bytes;
        usize   m_index = 0;
    };

    template <Channels Chan>
    struct PixelWriter
    {
        std::span<Byte> m_dest;

        void operator()(usize index, const Pixel& pixel) noexcept
        {
            const usize dataIndex = index * static_cast<usize>(Chan);
            if constexpr (Chan == Channels::RGB) {
                std::memcpy(m_dest.data() + dataIndex, &pixel, 3);
            } else {
                std::memcpy(m_dest.data() + dataIndex, &pixel, 4);
            }
        }
    };

    template <Channels Chan>
    struct PixelReader
    {
        std::span<const Byte> m_data;

        void operator()(Pixel& pixel, usize index)
        {
            const usize dataIndex = index * static_cast<usize>(Chan);

            pixel.m_r = static_cast<u8>(m_data[dataIndex + 0]);
            pixel.m_g = static_cast<u8>(m_data[dataIndex + 1]);
            pixel.m_b = static_cast<u8>(m_data[dataIndex + 2]);

            if constexpr (Chan == Channels::RGBA) {
                pixel.m_a = static_cast<u8>(m_data[dataIndex + 3]);
            } else {
                pixel.m_a = 0xFF;
            }
        }
    };

    inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }

    template <Channels Chan>
    ByteVec encode(std::span<const Byte> data, u32 width, u32 height, bool srgb)
    {
        // worst possible scenario is when no data is compressed + header + endMarker + tag (rgb/rgba)
        const usize maxSize = width * height * (static_cast<usize>(Chan) + 1) + constants::headerSize
                            + constants::endMarker.size();

        DataChunkArray chunks{ maxSize };    // the encoded data goes here
        RunningArray   seenPixels{};
        seenPixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        chunks.push(data::QoiHeader{
            .m_width      = width,
            .m_height     = height,
            .m_channels   = static_cast<u8>(Chan),
            .m_colorspace = static_cast<u8>(srgb ? 0 : 1),
        });

        PixelReader<Chan> reader{ data };

        auto prevPixel = constants::start;
        auto currPixel = constants::start;
        i32  run       = 0;

        for (const auto pixelIndex : sv::iota(0u, static_cast<usize>(width * height))) {
            reader(currPixel, pixelIndex);

            if (prevPixel == currPixel) {
                run++;

                const bool runLimit  = run == constants::runLimit;
                const bool lastPixel = pixelIndex == static_cast<usize>(width * height) - 1;
                if (runLimit || lastPixel) {
                    chunks.push(data::op::Run{ .m_run = static_cast<i8>(run) });
                    run = 0;
                }
            } else {
                if (run > 0) {
                    // ends of OP_RUN
                    chunks.push(data::op::Run{ .m_run = static_cast<i8>(run) });
                    run = 0;
                }

                const u8 index = hash(currPixel) % constants::runningArraySize;

                // OP_INDEX
                if (seenPixels[index] == currPixel) {
                    chunks.push(data::op::Index{ .m_index = index });
                } else {
                    seenPixels[index] = currPixel;

                    if constexpr (Chan == Channels::RGBA) {
                        if (prevPixel.m_a != currPixel.m_a) {
                            // OP_RGBA
                            chunks.push(data::op::Rgba{
                                .m_r = currPixel.m_r,
                                .m_g = currPixel.m_g,
                                .m_b = currPixel.m_b,
                                .m_a = currPixel.m_a,
                            });

                            prevPixel = currPixel;
                            continue;
                        }
                    }

                    // OP_DIFF and OP_LUMA
                    const i8 dr = currPixel.m_r - prevPixel.m_r;
                    const i8 dg = currPixel.m_g - prevPixel.m_g;
                    const i8 db = currPixel.m_b - prevPixel.m_b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (data::op::shouldDiff(dr, dg, db)) {
                        chunks.push(data::op::Diff{
                            .m_dr = dr,
                            .m_dg = dg,
                            .m_db = db,
                        });
                    } else if (data::op::shouldLuma(dg, dr_dg, db_dg)) {
                        chunks.push(data::op::Luma{
                            .m_dg    = dg,
                            .m_dr_dg = dr_dg,
                            .m_db_dg = db_dg,
                        });
                    } else {
                        // OP_RGB
                        chunks.push(data::op::Rgb{
                            .m_r = currPixel.m_r,
                            .m_g = currPixel.m_g,
                            .m_b = currPixel.m_b,
                        });
                    }
                }
            }

            prevPixel = currPixel;
        }

        chunks.push(data::EndMarker{});

        return chunks.get();
    }

    // TODO: implement
    template <Channels Src, Channels Dest = Src>
    ByteVec decode(std::span<const Byte> data, usize width, usize height) noexcept(false)
    {
        ByteVec decodedData(static_cast<usize>(width * height * static_cast<i32>(Dest)));

        RunningArray seenPixels{};
        seenPixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        Pixel prevPixel = constants::start;

        const auto        get = [&](usize index) -> u8 { return std::to_integer<u8>(data[index]); };
        PixelWriter<Dest> write{ decodedData };

        seenPixels[hash(prevPixel) % constants::runningArraySize] = prevPixel;

        usize chunksSize = data.size() - constants::headerSize - constants::endMarker.size();
        for (usize pixelIndex = 0, dataIndex = constants::headerSize;
             dataIndex < chunksSize || pixelIndex < width * height;
             ++pixelIndex) {

            const auto tag       = get(dataIndex++);
            auto       currPixel = prevPixel;

            using T = data::op::Tag;
            switch (tag) {
            case T::OP_RGB: {
                currPixel.m_r = get(dataIndex++);
                currPixel.m_g = get(dataIndex++);
                currPixel.m_b = get(dataIndex++);
                if constexpr (Src == Channels::RGBA) {
                    currPixel.m_a = prevPixel.m_a;
                }
            } break;
            case T::OP_RGBA: {
                currPixel.m_r = get(dataIndex++);
                currPixel.m_g = get(dataIndex++);
                currPixel.m_b = get(dataIndex++);
                if constexpr (Src == Channels::RGBA) {
                    currPixel.m_a = get(dataIndex++);
                }
            } break;
            default:
                switch (tag & 0b11000000) {
                case T::OP_INDEX: {
                    auto& pixel = seenPixels[tag & 0b00111111];
                    currPixel   = pixel;
                } break;
                case T::OP_DIFF: {
                    const i8 dr = ((tag & 0b00110000) >> 4) - constants::biasOpDiff;
                    const i8 dg = ((tag & 0b00001100) >> 2) - constants::biasOpDiff;
                    const i8 db = ((tag & 0b00000011)) - constants::biasOpDiff;

                    currPixel.m_r = static_cast<u8>(dr + prevPixel.m_r);
                    currPixel.m_g = static_cast<u8>(dg + prevPixel.m_g);
                    currPixel.m_b = static_cast<u8>(db + prevPixel.m_b);
                    if constexpr (Src == Channels::RGBA) {
                        currPixel.m_a = static_cast<u8>(prevPixel.m_a);
                    }
                } break;
                case T::OP_LUMA: {
                    const auto redBlue = get(dataIndex++);

                    const u8 dg    = (tag & 0b00111111) - constants::biasOpLumaG;
                    const u8 dr_dg = ((redBlue & 0b11110000) >> 4) - constants::biasOpLumaRB;
                    const u8 db_dg = (redBlue & 0b00001111) - constants::biasOpLumaRB;

                    currPixel.m_r = static_cast<u8>(dg + dr_dg + prevPixel.m_r);
                    currPixel.m_g = static_cast<u8>(dg + prevPixel.m_g);
                    currPixel.m_b = static_cast<u8>(dg + db_dg + prevPixel.m_b);
                    if constexpr (Src == Channels::RGBA) {
                        currPixel.m_a = static_cast<u8>(prevPixel.m_a);
                    }
                } break;
                case T::OP_RUN: {
                    auto run = (tag & 0b00111111) - constants::biasOpRun;
                    while (run-- > 0) {
                        write(pixelIndex++, prevPixel);
                    }
                    --pixelIndex;
                } break;
                default: [[unlikely]] /* invalid tag (is this eve possible?)*/;
                }
            }

            write(pixelIndex, currPixel);
            seenPixels[hash(currPixel) % constants::runningArraySize] = currPixel;
            prevPixel                                                 = currPixel;
        }

        return decodedData;
    }
}

namespace qoipp
{
    std::optional<ImageDesc> readHeader(ByteSpan data) noexcept
    {
        if (data.size() < constants::headerSize) {
            return std::nullopt;
        }

        using Magic = decltype(data::QoiHeader::m_magic);
        auto* magic = reinterpret_cast<const Magic*>(data.data());

        if (std::memcmp(magic, constants::magic.data(), constants::magic.size()) != 0) {
            return std::nullopt;
        }

        usize index = constants::magic.size();
        u32   width, height;
        u32   channels, colorspace;

        std::memcpy(&width, data.data() + index, sizeof(u32));
        index += sizeof(u32);
        std::memcpy(&height, data.data() + index, sizeof(u32));
        index      += sizeof(u32);
        channels    = std::to_integer<u8>(data[index++]);
        colorspace  = std::to_integer<u8>(data[index++]);

        return ImageDesc{
            .m_width      = fromBigEndian(width),
            .m_height     = fromBigEndian(height),
            .m_channels   = static_cast<Channels>(channels),
            .m_colorspace = static_cast<Colorspace>(colorspace),
        };
    }

    std::vector<Byte> encode(std::span<const Byte> data, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;
        const auto maxSize = static_cast<usize>(width * height * static_cast<u32>(channels));

        if (width <= 0 || height <= 0) {
            throw std::invalid_argument{ std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, static_cast<i32>(channels)
            ) };
        }

        if (static_cast<i32>(channels) != 3 && static_cast<i32>(channels) != 4) {
            throw std::invalid_argument{ std::format(
                "Invalid number of channels: expected 3 (RGB) or 4 (RGBA), got {}", static_cast<i32>(channels)
            ) };
        }

        if (data.size() != maxSize) {
            throw std::invalid_argument{ std::format(
                "Data size does not match the image description: expected {} x {} x {} = {}, got {}",
                width,
                height,
                static_cast<i32>(channels),
                maxSize,
                data.size()
            ) };
        }

        if (channels == Channels::RGB && data.size() % 3 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 3 bytes"
            };
        } else if (channels == Channels::RGBA && data.size() % 4 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 4 bytes"
            };
        }

        bool isSrgb = colorspace == Colorspace::sRGB;
        if (channels == Channels::RGB) {
            return impl::encode<Channels::RGB>(data, width, height, isSrgb);
        } else {
            return impl::encode<Channels::RGBA>(data, width, height, isSrgb);
        }
    }

    // TODO: implement
    Image decode(ByteSpan data, bool rgbOnly) noexcept(false)
    {
        if (data.size() == 0) {
            throw std::invalid_argument{ "Data is empty" };
        }

        auto desc = [&] {
            if (auto header = readHeader(data); header.has_value()) {
                return header.value();
            } else {
                throw std::invalid_argument{ "Invalid header" };
            }
        }();

        if (desc.m_channels == Channels::RGB) {
            return {
                .m_data = impl::decode<Channels::RGB>(data, desc.m_width, desc.m_height),
                .m_desc = desc,
            };
        } else if (rgbOnly) {
            desc.m_channels = Channels::RGB;
            return {
                .m_data = impl::decode<Channels::RGBA, Channels::RGB>(data, desc.m_width, desc.m_height),
                .m_desc = desc,
            };
        } else {
            return {
                .m_data = impl::decode<Channels::RGBA>(data, desc.m_width, desc.m_height),
                .m_desc = desc,
            };
        }
    }

    std::optional<ImageDesc> readHeaderFromFile(const std::filesystem::path& path) noexcept
    {
        namespace fs = std::filesystem;

        if (!fs::exists(path) || fs::file_size(path) < constants::headerSize || !fs::is_regular_file(path)) {
            return std::nullopt;
        }

        std::ifstream file{ path, std::ios::binary };
        if (!file.is_open()) {
            return std::nullopt;
        }

        ByteVec data(constants::headerSize);
        file.read(reinterpret_cast<char*>(data.data()), constants::headerSize);

        return readHeader(data);
    }

    void encodeToFile(
        const std::filesystem::path& path,
        std::span<const Byte>        data,
        ImageDesc                    desc,
        bool                         overwrite
    ) noexcept(false)
    {
        namespace fs = std::filesystem;

        if (fs::exists(path) && !overwrite) {
            throw std::invalid_argument{ "File already exists and overwrite is false" };
        }

        if (fs::exists(path) && !fs::is_regular_file(path)) {
            throw std::invalid_argument{ "Path is not a regular file, cannot overwrite" };
        }

        auto encodedData = encode(data, desc);

        std::ofstream file{ path, std::ios::binary | std::ios::trunc };
        if (!file.is_open()) {
            throw std::invalid_argument{ "Could not open file for writing" };
        }

        file.write(
            reinterpret_cast<const char*>(encodedData.data()),
            static_cast<std::streamsize>(encodedData.size())
        );
    }

    Image decodeFromFile(const std::filesystem::path& path, bool rgbOnly) noexcept(false)
    {
        namespace fs = std::filesystem;

        if (!fs::exists(path) || !fs::is_regular_file(path)) {
            throw std::invalid_argument{ "Path does not exist or is not a regular file" };
        }

        std::ifstream file{ path, std::ios::binary };
        if (!file.is_open()) {
            throw std::invalid_argument{ "Could not open file for reading" };
        }

        ByteVec data(fs::file_size(path));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

        return decode(data, rgbOnly);
    }
}
