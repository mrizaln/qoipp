#ifndef QOIPP_COMMON_HPP_AW4EODSHFJ4U
#define QOIPP_COMMON_HPP_AW4EODSHFJ4U

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#if defined(__cpp_lib_expected)
#    include <expected>
#endif

namespace qoipp::constants
{
    constexpr std::string_view magic              = "qoif";
    constexpr std::size_t      header_size        = 14;
    constexpr std::size_t      end_marker_size    = 8;
    constexpr std::size_t      running_array_size = 64;
}

namespace qoipp
{
    struct Pixel;

    using Byte      = std::uint8_t;
    using ByteVec   = std::vector<Byte>;
    using ByteSpan  = std::span<Byte>;
    using ByteCSpan = std::span<const Byte>;

    template <std::size_t N>
    using ByteArr = std::array<Byte, N>;

    using PixelVec   = std::vector<Pixel>;
    using PixelSpan  = std::span<Pixel>;
    using PixelCSpan = std::span<const Pixel>;

    template <std::size_t N>
    using PixelArr = std::array<Pixel, N>;

    using PixelGenFun  = std::function<Pixel(std::size_t index)>;
    using PixelSinkFun = std::function<void(Pixel pixel)>;
    using ByteSinkFun  = std::function<void(std::uint8_t byte)>;

    /**
     * @enum Colorspace
     * @brief Image colorspace.
     *
     * This won't affect encoding process.
     */
    enum class Colorspace : Byte
    {
        sRGB   = 0,
        Linear = 1,
    };

    /**
     * @enum Channels
     * @brief Image type (also number of bytes per pixel).
     *
     * This will affect encoding process.
     */
    enum class Channels : Byte
    {
        RGB  = 3,
        RGBA = 4,
    };

    /**
     * @enum Error
     * @brief Error enumeration of qoipp operations.
     *
     * Use `to_string()` function to get the human-readable description of the enumeration.
     */
    enum class Error
    {
        Empty = 1,             // data length == 0
        TooShort,              // e.g. data length < header size
        TooBig,                // byte count > std::numeric_limits<size_t>::max() [overflow]
        NotQoi,                // header is not QOI
        InvalidDesc,           // Desc has invalid value
        MismatchedDesc,        // data has mismatch with Desc
        NotEnoughSpace,        // buffer not enough
        NotInitialized,        // only relevant for stream encoder/decoder
        AlreadyInitialized,    // only relevant for stream encoder/decoder
        NotRegularFile,        // not regular file
        FileExists,            // file exists
        FileNotExists,         // file not exists
        IoError,               // file open/read/write error
        BadAlloc,              // bad alloc [like out of memory]
    };

    /**
     * @class Pixel
     * @brief Represent a pixel on an image.
     */
    struct Pixel
    {
        Byte r;
        Byte g;
        Byte b;
        Byte a;

        constexpr auto operator<=>(const Pixel&) const = default;
    };

    /**
     * @class Desc
     * @brief QOI image description.
     */
    struct Desc
    {
        std::uint32_t width;
        std::uint32_t height;
        Channels      channels;
        Colorspace    colorspace;

        constexpr auto operator<=>(const Desc&) const = default;
    };

    /**
     * @class Image
     * @brief Raw image data (whether in `RGB` or `RGBA` which is specified in `desc`).
     */
    struct Image
    {
        ByteVec data;
        Desc    desc;
    };

    /**
     * @class EncodeStatus
     * @brief Result of encode operation.
     *
     * This struct is used mainly with `encode_into()` functions. In the case of the output buffer is not
     * enough, the function will only decode up to the number of bytes available (no partial data chunk). This
     * struct is used to indicate whether the encode operation fully complete or only partially complete.
     */
    struct EncodeStatus
    {
        std::size_t written;
        bool        complete;
    };

    /**
     * @class StreamResult
     * @brief Result of stream-based encode/decode operation.
     *
     * The unit is in bytes; `processed` field corresponds to number of bytes from input buffer has been
     * processed while `written` field corresponds to number of bytes written to output buffer.
     */
    struct StreamResult
    {
        std::size_t processed;
        std::size_t written;
    };

#if defined(__cpp_lib_expected)
    template <typename T>
    using Result = std::expected<T, Error>;
#else
    /**
     * @class Result
     * @brief An imitation of `std::expected()` :D.
     *
     * Use `make_result()` or `make_error()` helper function to construct this class.
     */
    template <typename T>
    class [[nodiscard]] Result
    {
    public:
        Result() = default;

        template <typename TT>
            requires std::constructible_from<T, TT> or std::same_as<std::decay_t<TT>, Error>
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

    template <typename T, typename... Args>
    Result<T> make_result(Args&&... args)
    {
#if defined(__cpp_lib_expected)
        return Result<T>{ std::in_place, std::forward<Args>(args)... };
#else
        return Result<T>{ std::forward<Args>(args)... };
#endif
    }

    template <typename T>
    Result<T> make_error(Error error)
    {
#if defined(__cpp_lib_expected)
        return Result<T>{ std::unexpect, error };
#else
        return Result<T>{ error };
#endif
    }

    /**
     * @brief Get a human-readable description for an error value.
     *
     * @param error Error value.
     */
    inline std::string_view to_string(Error error) noexcept
    {
        switch (error) {
        case Error::Empty: return "Data is empty";
        case Error::TooShort: return "Data is too short";
        case Error::TooBig: return "Image is too big to process";
        case Error::NotQoi: return "Not a QOI file";
        case Error::InvalidDesc: return "Image description is invalid";
        case Error::MismatchedDesc: return "Image description does not match the data";
        case Error::NotEnoughSpace: return "Buffer does not have enough space";
        case Error::NotRegularFile: return "Not a regular file";
        case Error::FileExists: return "File already exists";
        case Error::FileNotExists: return "File does not exist";
        case Error::IoError: return "Unable to do read or write operation";
        case Error::BadAlloc: return "Failed to allocate memory";
        case Error::NotInitialized: return "Stream encoder/decoder is not initialized yet";
        case Error::AlreadyInitialized: return "Stream encoder/decoder already initialized";
        }

        return "Unknown";
    }

    /**
     * @brief Helper function to convert a number of channels to the Channels enum.
     *
     * @param channels The number of channels.
     * @return The corresponding Channels, or std::nullopt if number is not valid.
     *
     * For the inverse of the operation, you can use `std::to_underlying` or `static_cast` to an integer.
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
     * @brief Helper function to convert an inger to the Colorspace enum.
     *
     * @param colorspace The number of colorspace.
     * @return The corresponding Colorspace, or std::nullopt if number is not valid.
     *
     * For the inverse of the operation, you can use `std::to_underlying` or `static_cast` to an integer.
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
     * @brief Converts a pointer to an array of type T into a span of bytes (std::uint8_t).
     *
     * @tparam T The type of the elements pointed to (must be trivially copyable or void).
     * @param t Pointer to the first element of the array.
     * @param size Number of elements if T is a type, or number of bytes if T is void.
     * @return A `ByteCSpan` representing the byte view of the input array.
     */
    template <typename T>
        requires std::same_as<T, void> or std::is_trivially_copyable_v<T>
    ByteCSpan to_span(T* t, std::size_t size)
    {
        if constexpr (std::same_as<T, void>) {
            return { reinterpret_cast<const Byte*>(t), size };
        } else {
            return { reinterpret_cast<const Byte*>(t), size * sizeof(T) };
        }
    }

    /**
     * @brief Check if image description is valid.
     *
     * @param desc The image description.
     * @return True if valid, else false.
     *
     * This function doesn't check whether the number of bytes an image created with this `Desc` is
     * `Error::TooBig` to fit into `std::size_t`. Use `count_bytes()` for that.
     */
    inline bool is_valid(const Desc& desc)
    {
        const auto& [width, height, channels, colorspace] = desc;
        return width > 0 and height > 0    //
           and (channels == Channels::RGBA or channels == Channels::RGB)
           and (colorspace == Colorspace::Linear or colorspace == Colorspace::sRGB);
    }

    /**
     * @brief Count number of bytes produced by image described by desc.
     *
     * @param desc The description of the image.
     * @return The number of bytes.
     *
     * This function returns
     * - `Error::InvalidDesc` if the description is invalid, or
     * - `Error::TooBig` if the number of bytes exceed `std::size_t` limits.
     */
    inline Result<std::size_t> count_bytes(const Desc& desc)
    {
        if (not is_valid(desc)) {
            return make_error<std::size_t>(Error::InvalidDesc);
        }

        // detect overflow: https://stackoverflow.com/a/1815371/16506263
        auto overflows = [](std::size_t a, std::size_t b) {
            const auto c = a * b;
            return a != 0 and c / a != b;
        };

        const auto& [width, height, channels, _] = desc;
        if (overflows(width, height)) {
            return make_error<std::size_t>(Error::TooBig);
        }

        const auto pixel_count = static_cast<std::size_t>(width) * height;
        const auto chan        = static_cast<std::size_t>(channels);
        if (overflows(pixel_count, chan)) {
            return make_error<std::size_t>(Error::TooBig);
        }

        return pixel_count * chan;
    }

    /**
     * @brief Calculate the number of bytes on the worst case encoding scenario.
     *
     * @param desc The description of the image.
     * @return The number of bytes.
     *
     * Worst possible scenario is when no data is compressed + header + end_marker + tag (rgb/rgba).
     *
     * This function returns
     * - `Error::InvalidDesc` if the description is invalid, or
     * - `Error::TooBig` if the number of bytes exceed `std::size_t` limits.
     */
    inline Result<std::size_t> worst_size(const Desc& desc)
    {
        if (const auto bytes = count_bytes(desc); not bytes) {
            return make_error<std::size_t>(bytes.error());
        }

        const auto& [width, height, channels, _] = desc;

        return (static_cast<std::size_t>(channels) + 1) * width * height    // channels + 1 tag
             + constants::header_size + constants::end_marker_size;
    }

    /**
     * @brief Read the header of a QOI image.
     *
     * @param in_data The data to read the header from.
     * @return The description of QOI image if it is a valid QOI header.
     *
     * This function returns
     * - `Error::Empty` if the length of the data is zero,
     * - `Error::TooShort` if the length of the data passed in less than header length,
     * - `Error::NotQoi` if the the data does not describe a QOI header, or
     * - `Error::InvalidDesc` if any of the parsed field of `Desc` contains invalid value.
     */
    Result<Desc> read_header(ByteCSpan in_data) noexcept;

    /**
     * @brief Read the header of a QOI image from a file.
     *
     * @param in_path The path to the file.
     * @return The description of QOI image if it is a valid QOI header.
     *
     * This function returns
     * - `Error::Empty` if the data read from file empty,
     * - `Error::TooShort` if the length of the data read from file less than header length,
     * - `Error::NotQoi` if the the data read from file does not describe a QOI header,
     * - `Error::InvalidDesc` if any of the parsed field of `Desc` contains invalid value,
     * - `Error::NotRegularFile` if file pointed by path is not a regular file,
     * - `Error::FileNotExists` if file pointed by path not exists, or
     * - `Error::IoError` if file can't be opened or read.
     */
    Result<Desc> read_header(const std::filesystem::path& in_path) noexcept;
}

#endif
