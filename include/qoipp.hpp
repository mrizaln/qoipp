#ifndef QOIPP_HPP_O4A387W5ER6OW7E
#define QOIPP_HPP_O4A387W5ER6OW7E

#include <cstddef>
#include <vector>
#include <span>

namespace qoipp
{
    using ByteVec  = std::vector<std::byte>;
    using ByteSpan = std::span<const std::byte>;
    using CharSpan = std::span<const char>;

    struct ImageDesc
    {
        int m_width;
        int m_height;
        int m_channels;
    };

    struct QoiImage
    {
        ByteVec   m_data;
        ImageDesc m_desc;
    };

    /**
     * @brief Encode the given data into a QOI image
     *
     * @param data The data to encode
     * @param desc The description of the image
     * @return ByteVec The encoded image
     * @throw std::runtime_error If the arguments provided are invalid
     */
    ByteVec encode(ByteSpan data, ImageDesc desc) noexcept(false);

    inline ByteVec encode(CharSpan data, ImageDesc desc) noexcept(false)
    {
        // I believe this is safe
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data.data()), data.size() };
        return encode(byteData, std::move(desc));
    }

    inline ByteVec encode(const char* data, std::size_t size, ImageDesc desc) noexcept(false)
    {
        return encode(CharSpan{ data, size }, std::move(desc));
    }

    /**
     * @brief Decode the given QOI image
     *
     * @param data The QOI image to decode
     * @return QoiImage The decoded image
     * @throw std::runtime_error If the image is invalid
     */
    QoiImage decode(ByteSpan data) noexcept(false);

    inline QoiImage decode(CharSpan data) noexcept(false)
    {
        // I believe this is safe
        auto byteData = ByteSpan{ reinterpret_cast<const std::byte*>(data.data()), data.size() };
        return decode(byteData);
    }

    inline QoiImage decode(const char* data, std::size_t size) noexcept(false)
    {
        return decode(CharSpan{ data, size });
    }
}

#endif /* end of include guard: QOIPP_HPP_O4A387W5ER6OW7E */
