#ifndef QOIPP_HPP_O4A387W5ER6OW7E
#define QOIPP_HPP_O4A387W5ER6OW7E

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace qoipp
{
    using ByteVec  = std::vector<std::byte>;
    using ByteSpan = std::span<const std::byte>;

    template <typename C>
    concept CharLike = std::same_as<C, char> or std::same_as<C, unsigned char>
                    or std::same_as<C, signed char>;

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
     */
    ByteVec encode(ByteSpan data, ImageDesc desc) noexcept(false);

    template <CharLike Char>
    inline ByteVec encode(std::span<const Char> data, ImageDesc desc) noexcept(false)
    {
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data.data()), data.size() };
        return encode(byteData, desc);
    }

    /**
     * @brief Decode the given QOI image
     *
     * @param data The QOI image to decode
     * @param rgbOnly If true, only the RGB channels will be extracted
     * @return Image The decoded image
     * @throw std::invalid_argument If the data is not a valid QOI image
     */
    Image decode(ByteSpan data, bool rgbOnly = false) noexcept(false);

    template <CharLike Char>
    inline Image decode(std::span<const Char> data, bool rgbOnly = false) noexcept(false)
    {
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data.data()), data.size() };
        return decode(byteData, rgbOnly);
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
     * @return Image The decoded image
     * @throw std::invalid_argument If the file is not exist or not a valid QOI image
     */
    Image decodeFromFile(const std::filesystem::path& path, bool rgbOnly = false) noexcept(false);
}

#endif /* end of include guard: QOIPP_HPP_O4A387W5ER6OW7E */
