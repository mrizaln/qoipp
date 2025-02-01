#ifndef QOIPP_HPP_O4A387W5ER6OW7E
#define QOIPP_HPP_O4A387W5ER6OW7E

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>
#include <functional>

namespace qoipp
{
    using ByteVec  = std::vector<std::byte>;
    using ByteSpan = std::span<const std::byte>;

    enum class Colorspace : int
    {
        sRGB   = 0,
        Linear = 1,
    };

    enum class Channels : int
    {
        RGB  = 3,
        RGBA = 4,
    };

    struct PixelRepr
    {
        std::byte m_r;
        std::byte m_g;
        std::byte m_b;
        std::byte m_a;

        constexpr auto operator<=>(const PixelRepr&) const = default;
    };

    struct ImageDesc
    {
        unsigned int m_width;
        unsigned int m_height;
        Channels     m_channels;
        Colorspace   m_colorspace;

        constexpr auto operator<=>(const ImageDesc&) const = default;
    };

    struct Image
    {
        ByteVec   m_data;
        ImageDesc m_desc;
    };

    using PixelGenFun = std::function<PixelRepr(std::size_t pixelIndex)>;

    /**
     * @brief Read the header of a QOI image
     *
     * @param data The data to read the header from
     * @return std::optional<ImageDesc> The description of the image if it is a valid QOI image
     */
    std::optional<ImageDesc> readHeader(ByteSpan data) noexcept;

    /**
     * @brief Encode the given data into a QOI image
     *
     * @param data The data to encode
     * @param desc The description of the image
     * @return ByteVec The encoded image
     * @throw std::invalid_argument If there is a mismatch between the data and the description
     *
     * This function assume that the raw data is in the format of RGB888 or RGBA8888.
     */
    ByteVec encode(ByteSpan data, ImageDesc desc) noexcept(false);

    /**
     * @brief Encode the given data into a QOI image
     *
     * @param data The data to encode
     * @param size The size of the data
     * @param desc The description of the image
     * @return ByteVec The encoded image
     *
     * This function assume that the raw data is in the format of RGB888 or RGBA8888.
     */
    inline ByteVec encode(const void* data, std::size_t size, ImageDesc desc) noexcept(false)
    {
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data), size };
        return encode(byteData, desc);
    }

    /**
     * @brief Encode the data generated by the given function into a QOI image
     *
     * @param func The function to generate the data
     * @param desc The description of the image
     * @return ByteVec The encoded image
     *
     * The function should return the pixel at the given location ((0, 0) is at the top-left corner) in the
     * format of RGBA8888 (the alpha channel is discarded if the ImageDesc specifies RGB channels only).
     */
    ByteVec encodeFromFunction(PixelGenFun func, ImageDesc desc) noexcept(false);

    /**
     * @brief Decode the given QOI image
     *
     * @param data The QOI image to decode
     * @param target The target channels to extract; if std::nullopt, the original channels will be used
     * @return Image The decoded image
     * @throw std::invalid_argument If the data is not a valid QOI image
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     */
    Image decode(
        ByteSpan                data,
        std::optional<Channels> target         = std::nullopt,
        bool                    flipVertically = false
    ) noexcept(false);

    /**
     * @brief Decode the given data into a QOI image
     *
     * @param data The data to encode
     * @param size The size of the data
     * @param target The target channels to extract; if std::nullopt, the original channels will be used
     * @param flipVertically If true, the image will be flip vertically
     * @return Image The decoded image
     * @throw std::invalid_argument If the data is not a valid QOI image
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     */
    inline Image decode(
        const void*             data,
        std::size_t             size,
        std::optional<Channels> target         = std::nullopt,
        bool                    flipVertically = false
    ) noexcept(false)
    {
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data), size };
        return decode(byteData, target, flipVertically);
    }

    /**
     * @brief Read the header of a QOI image from a file
     * @param path The path to the file
     * @return std::optional<ImageDesc> The description of the image (std::nullopt if it's invalid)
     */
    std::optional<ImageDesc> readHeaderFromFile(const std::filesystem::path& path) noexcept;

    /**
     * @brief Encode the given data into a QOI image and write it to a file
     * @param path The path to the file
     * @param data The data to encode
     * @param desc The description of the image
     * @param overwrite If true, the file will be overwritten if it already exists
     * @throw std::invalid_argument If the file already exists and overwrite is false or if there is a
     * mismatch between the data and the description
     */
    void encodeToFile(
        const std::filesystem::path& path,
        ByteSpan                     data,
        ImageDesc                    desc,
        bool                         overwrite = false
    ) noexcept(false);

    /**
     * @brief Decode a QOI image from a file
     *
     * @param path The path to the file
     * @param target The target channels to extract; if std::nullopt, the original channels will be used
     * @return Image The decoded image
     * @throw std::invalid_argument If the file is not exist or not a valid QOI image
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     */
    Image decodeFromFile(
        const std::filesystem::path& path,
        std::optional<Channels>      target         = std::nullopt,
        bool                         flipVertically = false
    ) noexcept(false);
}

#endif /* end of include guard: QOIPP_HPP_O4A387W5ER6OW7E */
