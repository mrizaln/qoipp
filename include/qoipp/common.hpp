#ifndef QOIPP_COMMON_HPP_AW4EODSHFJ4U
#define QOIPP_COMMON_HPP_AW4EODSHFJ4U

#include <cstdint>
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

    enum class Colorspace : Byte
    {
        sRGB   = 0,
        Linear = 1,
    };

    enum class Channels : Byte
    {
        RGB  = 3,
        RGBA = 4,
    };

    enum class Error
    {
        Empty = 1,         // data length == 0
        TooShort,          // data length < header size
        TooBig,            // byte count > std::numeric_limits<size_t>::max() [overflow]
        NotQoi,            // header is not qoi
        InvalidDesc,       // Desc has invalid value
        MismatchedDesc,    // data has mismatch with Desc
        NotEnoughSpace,    // buffer not enough
        NotRegularFile,    // not regular file
        FileExists,        // file exists
        FileNotExists,     // file not exists
        IoError,           // file open/read/write error
        BadAlloc,          // bad alloc [like out of memory]
    };

    struct Pixel
    {
        Byte r;
        Byte g;
        Byte b;
        Byte a;

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
        ByteVec data;
        Desc    desc;
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
     * @brief Check if iamge description is valid.
     *
     * @param desc The image description.
     * @return True if valid, else false.
     *
     * This function doesn't check whether the number of bytes an image created with this `Desc` is
     * `Error::TooBig` to fit into `std::size_t`. Use `count_bytes` for that.
     */
    inline bool is_valid(const Desc& desc)
    {
        const auto& [width, height, channels, colorspace] = desc;
        return width > 0 and height > 0    //
           and (channels == Channels::RGBA or channels == Channels::RGB)
           and (colorspace == Colorspace::Linear or colorspace == Colorspace::sRGB);
    }

    /**
     * @brief Count number of bytes produced by imaage described on desc.
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
            return make_result<std::size_t>(Error::InvalidDesc);
        }

        // detect overflow: https://stackoverflow.com/a/1815371/16506263
        auto overflows = [](std::size_t a, std::size_t b) {
            const auto c = a * b;
            return a != 0 and c / a != b;
        };

        const auto& [width, height, channels, _] = desc;
        if (overflows(width, height)) {
            return make_result<std::size_t>(Error::TooBig);
        }

        const auto pixel_count = static_cast<std::size_t>(width) * height;
        const auto chan        = static_cast<std::size_t>(channels);
        if (overflows(pixel_count, chan)) {
            return make_result<std::size_t>(Error::TooBig);
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
            return make_result<std::size_t>(bytes.error());
        }

        const auto& [width, height, channels, _] = desc;

        return (static_cast<std::size_t>(channels) + 1) * width * height    // channels + 1 tag
             + constants::header_size + constants::end_marker_size;
    }
}

#endif
