#ifndef QOIPP_HPP_O4A387W5ER6OW7E
#define QOIPP_HPP_O4A387W5ER6OW7E

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <variant>
#include <vector>
#include <version>

#if defined(__cpp_lib_expected)
#    include <expected>
#endif

namespace qoipp
{
    using Vec   = std::vector<std::uint8_t>;
    using Span  = std::span<std::uint8_t>;
    using CSpan = std::span<const std::uint8_t>;

    enum class Colorspace : std::uint8_t
    {
        sRGB   = 0,
        Linear = 1,
    };

    enum class Channels : std::uint8_t
    {
        RGB  = 3,
        RGBA = 4,
    };

    enum class Error
    {
        Empty = 1,
        FileExists,
        FileNotExists,
        InvalidDesc,
        IoError,
        MismatchedDesc,
        NotQoi,
        NotRegularFile,
        TooShort,
    };

    struct Pixel
    {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;

        constexpr auto operator<=>(const Pixel&) const = default;
    };

    struct Desc
    {
        std::uint32_t width;
        std::uint32_t height;
        Channels      channels;
        Colorspace    colorspace;

        constexpr auto operator<=>(const Desc&) const = default;
    };

    struct Image
    {
        Vec  data;
        Desc desc;
    };

#if defined(__cpp_lib_expected)
    template <typename T>
    using Result = std::expected<T, Error>;
#else
    template <typename T>
    class Result
    {
    public:
        Result() = default;

        template <typename TT>
        Result(TT&& tt)
            : m_variant{ std::forward<TT>(tt) }
        {
        }

        T&&      value() && { return std::get<T>(std::move(m_variant)); }
        T&       value() & { return std::get<T>(m_variant); }
        const T& value() const& { return std::get<T>(m_variant); }

        Error&&      error() && { return std::get<Error>(std::move(m_variant)); }
        Error&       error() & { return std::get<Error>(m_variant); }
        const Error& error() const& { return std::get<Error>(m_variant); }

        bool has_value() const noexcept { return std::holds_alternative<T>(m_variant); }

        explicit operator bool() const noexcept { return has_value(); }

        T&&      operator*() && noexcept { return std::move(value()); }
        T&       operator*() & noexcept { return value(); }
        const T& operator*() const& noexcept { return value(); }

        T*       operator->() noexcept { return &value(); }
        const T* operator->() const noexcept { return &value(); }

    private:
        std::variant<T, Error> m_variant;
    };

    template <>
    class Result<void>
    {
    public:
        Result() = default;

        Result(Error error)
            : m_variant{ error }
        {
        }

        Error&&      error() && { return std::get<Error>(std::move(m_variant)); }
        Error&       error() & { return std::get<Error>(m_variant); }
        const Error& error() const& { return std::get<Error>(m_variant); }

        bool has_value() const noexcept { return std::holds_alternative<Void>(m_variant); }

        explicit operator bool() const noexcept { return has_value(); }

    private:
        struct Void
        {
        };

        std::variant<Void, Error> m_variant;
    };
#endif

    using PixelGenFun = std::function<Pixel(std::size_t pixel_index)>;

    /**
     * @brief Get a human readable description for an error value.
     *
     * @param error Error value.
     */
    std::string_view to_string(Error error) noexcept;

    /**
     * @brief Helper function to convert a number of channels to the Channels enum.
     *
     * @param channels The number of channels.
     * @return The corresponding Channels, or std::nullopt if number is not valid.
     */
    template <std::integral T>
    inline constexpr std::optional<Channels> to_channels(T channels) noexcept
    {
        switch (channels) {
        case 3: return Channels::RGB;
        case 4: return Channels::RGBA;
        default: return std::nullopt;
        }
    }

    /**
     * @brief Helper function to convert a number of colorspace to the Colorspace enum.
     *
     * @param colorspace The number of colorspace.
     * @return The corresponding Colorspace, or std::nullopt if number is not valid.
     */
    template <std::integral T>
    inline constexpr std::optional<Colorspace> to_colorspace(T colorspace) noexcept
    {
        switch (colorspace) {
        case 0: return Colorspace::sRGB;
        case 1: return Colorspace::Linear;
        default: return std::nullopt;
        }
    }

    /**
     * @brief Read the header of a QOI image.
     *
     * @param data The data to read the header from.
     * @return The description of QOI image if it is a valid QOI header.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::TooShort` if the length of the data passed in less than header length,
     * - `Error::NotQoi` if the the data does not describe a QOI header, or
     * - `Error::InvalidDesc` if any of the parsed field of `Desc` contains invalid value.
     */
    Result<Desc> read_header(CSpan data) noexcept;

    /**
     * @brief Read the header of a QOI image from a file.
     * @param path The path to the file.
     * @return The description of the image (std::nullopt if it's invalid).
     *
     * This function returns
     * - `Error::Empty` if the data read from file empty,
     * - `Error::TooShort` if the length of the data read from file less than header length,
     * - `Error::NotQoi` if the the data read from file does not describe a QOI header, or
     * - `Error::InvalidDesc` if any of the parsed field of `Desc` contains invalid value.
     * - `Error::FileNotExists` if file pointed by path not exists,
     * - `Error::NotRegularFile` if file pointed by path is not a regular file, or
     * - `Error::IoError` if file can't be opened or read.
     */
    Result<Desc> read_header(const std::filesystem::path& path) noexcept;

    /**
     * @brief Encode the given data into a QOI image.
     *
     * @param data The data to encode.
     * @param desc The description of the image.
     * @return The encoded image or an error.
     *
     * This function assumes that the raw data is in the format of RGB888 or RGBA8888.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     * - `Error::MismatchedDesc` if the number of pixel data doesn't match the image description.
     */
    Result<Vec> encode(CSpan data, Desc desc) noexcept;

    /**
     * @brief Encode the given data into a QOI image.
     *
     * @param data The data to encode.
     * @param size The size of the data.
     * @param desc The description of the image.
     * @return The encoded image or an error.
     *
     * This function assume that the raw data is in the format of RGB888 or RGBA8888.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     * - `Error::MismatchedDesc` if the number of pixel data doesn't match the image description.
     */
    inline Result<Vec> encode(const void* data, std::size_t size, Desc desc) noexcept
    {
        auto span = CSpan{ reinterpret_cast<const std::uint8_t*>(data), size };
        return encode(span, desc);
    }

    /**
     * @brief Encode data generated by the given function into a QOI image.
     *
     * @param func The function to generate the data.
     * @param desc The description of the image.
     * @return The encoded image or an error.
     *
     * The function should return the pixel at the given pixel index. The index 0 starts at top-left corner of
     * an image and increasing to the right and then down. The alpha channel value is discarded if the Desc
     * specifies RGB channels only.
     *
     * This function returns
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     */
    Result<Vec> encode(PixelGenFun func, Desc desc) noexcept;

    /**
     * @brief Decode the given QOI image.
     *
     * @param data The QOI image to decode.
     * @param target The target channels to extract; if std::nullopt, the original channels will be used.
     * @param flip_vertically If true, the image will be flip vertically.
     * @return The decoded image or an error.
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::TooShort` if the length of the data passed in less than header length,
     * - `Error::NotQoi` if the the data does not describe a QOI header, or
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     */
    Result<Image> decode(
        CSpan                   data,
        std::optional<Channels> target          = std::nullopt,
        bool                    flip_vertically = false
    ) noexcept;

    /**
     * @brief Decode the given data into a QOI image.
     *
     * @param data The data to encode.
     * @param size The size of the data.
     * @param target The target channels to extract; if std::nullopt, the original channels will be used.
     * @param flip_vertically If true, the image will be flip vertically.
     * @return The decoded image or an error.
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::TooShort` if the length of the data passed in less than header length,
     * - `Error::NotQoi` if the the data does not describe a QOI header, or
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     */
    inline Result<Image> decode(
        const void*             data,
        std::size_t             size,
        std::optional<Channels> target          = std::nullopt,
        bool                    flip_vertically = false
    ) noexcept
    {
        auto span = CSpan{ reinterpret_cast<const std::uint8_t*>(data), size };
        return decode(span, target, flip_vertically);
    }

    /**
     * @brief Encode the given data into a QOI image and write it to a file.
     *
     * @param path The path to the file.
     * @param data The data to encode.
     * @param desc The description of the image.
     * @param overwrite If true, the file will be overwritten if it already exists.
     * @return Error if an error happen, otherwise nothing.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::FileExists` if file pointed by path exists and overwrite is set to false,
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     * - `Error::IoError` if file can't be opened or written.
     * - `Error::MismatchedDesc` if the number of pixel data doesn't match the image description.
     * - `Error::NotRegularFile` if overwrite is set to true and file is not a regular file, or
     */
    Result<void> encode_to_file(
        const std::filesystem::path& path,
        CSpan                        data,
        Desc                         desc,
        bool                         overwrite = false
    ) noexcept;

    /**
     * @brief Decode a QOI image from a file.
     *
     * @param path The path to the file.
     * @param target The target channels to extract; if std::nullopt, the original channels will be used.
     * @return Image The decoded image.
     * @throw std::invalid_argument If the file is not exist or not a valid QOI image.
     *
     * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF.
     *
     * This function returns
     * - `Error::Empty` if the length of the data read from file is zero,
     * - `Error::FileNotExists` if file pointed by path not exists,
     * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value.
     * - `Error::IoError` if file can't be opened or read.
     * - `Error::NotQoi` if the the data does not describe a QOI header, or
     * - `Error::NotRegularFile` if file pointed by path is not a regular file, or
     * - `Error::TooShort` if the file size is less than QOI header length,
     * - `Error::TooShort` if the length of the data passed in less than header length,
     */
    Result<Image> decode_from_file(
        const std::filesystem::path& path,
        std::optional<Channels>      target          = std::nullopt,
        bool                         flip_vertically = false
    ) noexcept;
}

#endif /* end of include guard: QOIPP_HPP_O4A387W5ER6OW7E */
