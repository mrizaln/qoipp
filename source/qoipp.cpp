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
#include <stdexcept>
#include <type_traits>
#include <utility>

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

    template <usize N>
    using Arr = std::array<u8, N>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <typename T, usize N>
    consteval Arr<N> toBytes(const T (&value)[N])
    {
        Arr<N> result;
        for (usize i : sv::iota(0u, N)) {
            result[i] = static_cast<u8>(value[i]);
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
        u8 r;
        u8 g;
        u8 b;
        u8 a;

        constexpr auto operator<=>(const Pixel&) const = default;
    };
    static_assert(std::is_trivial_v<Pixel>);

    template <typename T>
    concept PixelReader = requires(const T ct, usize idx) {
        { ct.read(idx) } -> std::same_as<Pixel>;
    };
}

namespace qoipp::constants
{
    constexpr std::array<char, 4> magic = { 'q', 'o', 'i', 'f' };

    constexpr usize  headerSize       = 14;
    constexpr usize  runningArraySize = 64;
    constexpr Arr<8> endMarker        = toBytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

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
        { t.write(r, i) } noexcept -> std::same_as<usize>;
    };
    // clang-format on

    template <typename T>
    concept DataChunkVec = DataChunk<T, Vec>;

    inline usize writeMagic(Vec& vec, usize index) noexcept
    {
        for (char c : constants::magic) {
            vec[index++] = static_cast<u8>(c);
        }
        return index;
    }

    inline usize write32(Vec& vec, usize index, u32 value) noexcept
    {
        vec[index + 0] = static_cast<u8>((value >> 24) & 0xFF);
        vec[index + 1] = static_cast<u8>((value >> 16) & 0xFF);
        vec[index + 2] = static_cast<u8>((value >> 8) & 0xFF);
        vec[index + 3] = static_cast<u8>(value & 0xFF);
        return index + 4;
    }

    struct QoiHeader
    {
        u32 width;
        u32 height;
        u8  channels;
        u8  colorspace;

        usize write(Vec& vec, usize index) const noexcept
        {
            index = writeMagic(vec, index);
            index = write32(vec, index, width);
            index = write32(vec, index, height);

            vec[index + 0] = static_cast<u8>(channels);
            vec[index + 1] = static_cast<u8>(colorspace);

            return index + 2;
        }
    };
    static_assert(DataChunkVec<QoiHeader>);

    struct EndMarker
    {
        static usize write(Vec& vec, usize index) noexcept
        {
            for (auto byte : constants::endMarker) {
                vec[index++] = byte;
            }
            return index;
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
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = static_cast<u8>(OP_RGB);
                vec[index + 1] = static_cast<u8>(r);
                vec[index + 2] = static_cast<u8>(g);
                vec[index + 3] = static_cast<u8>(b);
                return index + 4;
            }
        };
        static_assert(DataChunkVec<Rgb>);

        struct Rgba
        {
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;
            u8 a = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = static_cast<u8>(OP_RGBA);
                vec[index + 1] = static_cast<u8>(r);
                vec[index + 2] = static_cast<u8>(g);
                vec[index + 3] = static_cast<u8>(b);
                vec[index + 4] = static_cast<u8>(a);
                return index + 5;
            }
        };
        static_assert(DataChunkVec<Rgba>);

        struct Index
        {
            u32 index = 0;

            usize write(Vec& vec, usize idx) const noexcept
            {
                vec[idx + 0] = static_cast<u8>(OP_INDEX | index);
                return idx + 1;
            }
        };
        static_assert(DataChunkVec<Index>);

        struct Diff
        {
            i8 dr = 0;
            i8 dg = 0;
            i8 db = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                constexpr auto bias = constants::biasOpDiff;

                auto val       = OP_DIFF | (dr + bias) << 4 | (dg + bias) << 2 | (db + bias);
                vec[index + 0] = static_cast<u8>(val);

                return index + 1;
            }
        };
        static_assert(DataChunkVec<Diff>);

        struct Luma
        {
            i8 dg    = 0;
            i8 dr_dg = 0;
            i8 db_dg = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                constexpr auto biasG  = constants::biasOpLumaG;
                constexpr auto biasRB = constants::biasOpLumaRB;

                vec[index + 0] = static_cast<u8>(OP_LUMA | (dg + biasG));
                vec[index + 1] = static_cast<u8>((dr_dg + biasRB) << 4 | (db_dg + biasRB));

                return index + 2;
            }
        };
        static_assert(DataChunkVec<Luma>);

        struct Run
        {
            i8 run = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = static_cast<u8>(OP_RUN | (run + constants::biasOpRun));
                return index + 1;
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
            m_index = t.write(m_bytes, m_index);
        }

        Vec get() noexcept
        {
            m_bytes.resize(m_index);
            return std::exchange(m_bytes, {});
        }

    private:
        Vec   m_bytes;
        usize m_index = 0;
    };

    template <Channels Chan>
    struct PixelWriter
    {
        std::span<u8> dest;

        void operator()(usize index, const Pixel& pixel) noexcept
        {
            const usize dataIndex = index * static_cast<usize>(Chan);
            if constexpr (Chan == Channels::RGB) {
                std::memcpy(dest.data() + dataIndex, &pixel, 3);
            } else {
                std::memcpy(dest.data() + dataIndex, &pixel, 4);
            }
        }
    };

    template <Channels Chan>
    struct SimplePixelReader
    {
        std::span<const u8> data;

        Pixel read(usize index) const noexcept
        {
            const usize dataIndex = index * static_cast<usize>(Chan);

            if constexpr (Chan == Channels::RGBA) {
                return {
                    .r = data[dataIndex + 0],
                    .g = data[dataIndex + 1],
                    .b = data[dataIndex + 2],
                    .a = data[dataIndex + 3],
                };
            } else {
                return {
                    .r = data[dataIndex + 0],
                    .g = data[dataIndex + 1],
                    .b = data[dataIndex + 2],
                    .a = 0xFF,
                };
            }
        }
    };
    static_assert(qoipp::PixelReader<SimplePixelReader<Channels::RGB>>);

    template <Channels Chan>
    struct FuncPixelReader
    {
        PixelGenFun func;
        u32         width;

        Pixel read(usize index) const noexcept
        {
            auto repr = func(index);
            if constexpr (Chan == Channels::RGBA) {
                return {
                    .r = repr.r,
                    .g = repr.g,
                    .b = repr.b,
                    .a = repr.a,
                };
            } else {
                return {
                    .r = repr.r,
                    .g = repr.g,
                    .b = repr.b,
                    .a = 0xFF,
                };
            }
        }
    };

    inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }

    template <Channels Chan, template <Channels> typename Reader>
    Vec encode(Reader<Chan> reader, u32 width, u32 height, bool srgb)
    {
        // worst possible scenario is when no data is compressed + header + endMarker + tag (rgb/rgba)
        const usize maxSize = width * height * (static_cast<usize>(Chan) + 1) + constants::headerSize
                            + constants::endMarker.size();

        DataChunkArray chunks{ maxSize };    // the encoded data goes here
        RunningArray   seenPixels{};
        seenPixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        chunks.push(data::QoiHeader{
            .width      = width,
            .height     = height,
            .channels   = static_cast<u8>(Chan),
            .colorspace = static_cast<u8>(srgb ? 0 : 1),
        });

        auto prevPixel = constants::start;
        auto currPixel = constants::start;
        i32  run       = 0;

        for (const auto pixelIndex : sv::iota(0u, static_cast<usize>(width * height))) {
            currPixel = reader.read(pixelIndex);

            if (prevPixel == currPixel) {
                run++;

                const bool runLimit  = run == constants::runLimit;
                const bool lastPixel = pixelIndex == static_cast<usize>(width * height) - 1;
                if (runLimit || lastPixel) {
                    chunks.push(data::op::Run{ .run = static_cast<i8>(run) });
                    run = 0;
                }
            } else {
                if (run > 0) {
                    // ends of OP_RUN
                    chunks.push(data::op::Run{ .run = static_cast<i8>(run) });
                    run = 0;
                }

                const u8 index = hash(currPixel) % constants::runningArraySize;

                // OP_INDEX
                if (seenPixels[index] == currPixel) {
                    chunks.push(data::op::Index{ .index = index });
                } else {
                    seenPixels[index] = currPixel;

                    if constexpr (Chan == Channels::RGBA) {
                        if (prevPixel.a != currPixel.a) {
                            // OP_RGBA
                            chunks.push(data::op::Rgba{
                                .r = currPixel.r,
                                .g = currPixel.g,
                                .b = currPixel.b,
                                .a = currPixel.a,
                            });

                            prevPixel = currPixel;
                            continue;
                        }
                    }

                    // OP_DIFF and OP_LUMA
                    const i8 dr = currPixel.r - prevPixel.r;
                    const i8 dg = currPixel.g - prevPixel.g;
                    const i8 db = currPixel.b - prevPixel.b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (data::op::shouldDiff(dr, dg, db)) {
                        chunks.push(data::op::Diff{
                            .dr = dr,
                            .dg = dg,
                            .db = db,
                        });
                    } else if (data::op::shouldLuma(dg, dr_dg, db_dg)) {
                        chunks.push(data::op::Luma{
                            .dg    = dg,
                            .dr_dg = dr_dg,
                            .db_dg = db_dg,
                        });
                    } else {
                        // OP_RGB
                        chunks.push(data::op::Rgb{
                            .r = currPixel.r,
                            .g = currPixel.g,
                            .b = currPixel.b,
                        });
                    }
                }
            }

            prevPixel = currPixel;
        }

        chunks.push(data::EndMarker{});

        return chunks.get();
    }

    template <Channels Src, Channels Dest = Src>
    Vec decode(std::span<const u8> data, usize width, usize height, bool flipVertically) noexcept(false)
    {
        Vec decodedData(static_cast<usize>(width * height * static_cast<i32>(Dest)));

        RunningArray seenPixels{};
        seenPixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        Pixel prevPixel = constants::start;

        const auto        get = [&](usize index) -> u8 { return index < data.size() ? data[index] : 0x00; };
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
                currPixel.r = get(dataIndex++);
                currPixel.g = get(dataIndex++);
                currPixel.b = get(dataIndex++);
                if constexpr (Src == Channels::RGBA) {
                    currPixel.a = prevPixel.a;
                }
            } break;
            case T::OP_RGBA: {
                currPixel.r = get(dataIndex++);
                currPixel.g = get(dataIndex++);
                currPixel.b = get(dataIndex++);
                if constexpr (Src == Channels::RGBA) {
                    currPixel.a = get(dataIndex++);
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

                    currPixel.r = static_cast<u8>(dr + prevPixel.r);
                    currPixel.g = static_cast<u8>(dg + prevPixel.g);
                    currPixel.b = static_cast<u8>(db + prevPixel.b);
                    if constexpr (Src == Channels::RGBA) {
                        currPixel.a = static_cast<u8>(prevPixel.a);
                    }
                } break;
                case T::OP_LUMA: {
                    const auto redBlue = get(dataIndex++);

                    const u8 dg    = (tag & 0b00111111) - constants::biasOpLumaG;
                    const u8 dr_dg = ((redBlue & 0b11110000) >> 4) - constants::biasOpLumaRB;
                    const u8 db_dg = (redBlue & 0b00001111) - constants::biasOpLumaRB;

                    currPixel.r = static_cast<u8>(dg + dr_dg + prevPixel.r);
                    currPixel.g = static_cast<u8>(dg + prevPixel.g);
                    currPixel.b = static_cast<u8>(dg + db_dg + prevPixel.b);
                    if constexpr (Src == Channels::RGBA) {
                        currPixel.a = static_cast<u8>(prevPixel.a);
                    }
                } break;
                case T::OP_RUN: {
                    auto run = (tag & 0b00111111) - constants::biasOpRun;
                    while (run-- > 0 and pixelIndex < width * height) {
                        write(pixelIndex++, prevPixel);
                    }
                    --pixelIndex;
                    if (pixelIndex >= width * height) {
                        break;
                    }
                    continue;
                } break;
                default: [[unlikely]] /* invalid tag (is this even possible?)*/;
                }
            }

            write(pixelIndex, currPixel);
            seenPixels[hash(currPixel) % constants::runningArraySize] = currPixel;
            prevPixel                                                 = currPixel;
        }

        if (flipVertically) {
            auto linesize = width * static_cast<usize>(Dest);
            for (auto y : sv::iota(0u, height / 2)) {
                auto* line1 = decodedData.data() + y * linesize;
                auto* line2 = decodedData.data() + (height - y - 1) * linesize;

                std::swap_ranges(line1, line1 + linesize, line2);
            }
        }

        return decodedData;
    }
}

namespace qoipp
{
    std::optional<ImageDesc> readHeader(Span data) noexcept
    {
        if (data.size() < constants::headerSize) {
            return std::nullopt;
        }

        using Magic = decltype(constants::magic);
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
        channels    = data[index++];
        colorspace  = data[index++];

        return ImageDesc{
            .width      = fromBigEndian(width),
            .height     = fromBigEndian(height),
            .channels   = static_cast<Channels>(channels),
            .colorspace = static_cast<Colorspace>(colorspace),
        };
    }

    Vec encode(std::span<const u8> data, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;
        const auto maxSize = static_cast<usize>(width * height * static_cast<u32>(channels));

        if (width <= 0 || height <= 0) {
            throw std::invalid_argument{ std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, static_cast<i32>(channels)
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
            auto reader = impl::SimplePixelReader<Channels::RGB>{ data };
            return impl::encode(reader, width, height, isSrgb);
        } else {
            auto reader = impl::SimplePixelReader<Channels::RGBA>{ data };
            return impl::encode(reader, width, height, isSrgb);
        }
    }

    Vec encodeFromFunction(PixelGenFun func, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (width <= 0 || height <= 0) {
            throw std::invalid_argument{ std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, static_cast<i32>(channels)
            ) };
        }

        bool isSrgb = colorspace == Colorspace::sRGB;
        if (channels == Channels::RGB) {
            auto reader = impl::FuncPixelReader<Channels::RGB>{ std::move(func), width };
            return impl::encode(reader, width, height, isSrgb);
        } else {
            auto reader = impl::FuncPixelReader<Channels::RGBA>{ std::move(func), width };
            return impl::encode(reader, width, height, isSrgb);
        }
    }

    Image decode(Span data, std::optional<Channels> target, bool flipVertically) noexcept(false)
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

        const auto& [width, height, channels, colorspace] = desc;

        auto want = target.value_or(channels);

        if (channels == Channels::RGBA and want == Channels::RGBA) {
            return {
                .data = impl::decode<Channels::RGBA, Channels::RGBA>(data, width, height, flipVertically),
                .desc = desc,
            };
        } else if (channels == Channels::RGB and want == Channels::RGB) {
            return {
                .data = impl::decode<Channels::RGB, Channels::RGB>(data, width, height, flipVertically),
                .desc = desc,
            };
        } else if (channels == Channels::RGBA and want == Channels::RGB) {
            desc.channels = Channels::RGB;
            return {
                .data = impl::decode<Channels::RGBA, Channels::RGB>(data, width, height, flipVertically),
                .desc = desc,
            };
        } else {
            desc.channels = Channels::RGBA;
            return {
                .data = impl::decode<Channels::RGB, Channels::RGBA>(data, width, height, flipVertically),
                .desc = desc,
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

        Vec data(constants::headerSize);
        file.read(reinterpret_cast<char*>(data.data()), constants::headerSize);

        return readHeader(data);
    }

    void encodeToFile(
        const std::filesystem::path& path,
        std::span<const u8>          data,
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

    Image decodeFromFile(
        const std::filesystem::path& path,
        std::optional<Channels>      target,
        bool                         flipVertically
    ) noexcept(false)
    {
        namespace fs = std::filesystem;

        if (!fs::exists(path) || !fs::is_regular_file(path)) {
            throw std::invalid_argument{ "Path does not exist or is not a regular file" };
        }

        std::ifstream file{ path, std::ios::binary };
        if (!file.is_open()) {
            throw std::invalid_argument{ "Could not open file for reading" };
        }

        Vec data(fs::file_size(path));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

        return decode(data, target, flipVertically);
    }
}
