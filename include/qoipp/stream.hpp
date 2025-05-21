#ifndef QOIPP_STREAM_HPP_43EWR7VETY
#define QOIPP_STREAM_HPP_43EWR7VETY

#include "qoipp/common.hpp"

namespace qoipp
{
    struct StreamResult
    {
        std::size_t processed;
        std::size_t written;
    };

    class StreamEncoder
    {
    public:
        StreamEncoder() noexcept;

        /**
         * @brief Prepare encoder and fill the out buffer with header data.
         *
         * @param out_buf The buffer to be written.
         * @param desc The description of the image to be encoded later.
         *
         * This function will both prepare the internal state of the encoder and fill the `out_buf` with the
         * header information for qoi. You should not call this function twice. To reuse this encoder and
         * prepare for use for other image, you should call `finalize` first then you can `initialize` again.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than header length,
         * - `Error::TooBig` if the image is too big to process,
         * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value, or
         * - `Error::AlreadyInitialized` if the encoder is already initialized.
         */
        Result<void> initialize(ByteSpan out_buf, Desc desc) noexcept;

        /**
         * @brief Encode the pixel data into out.
         *
         * @param out_buf The output buffer.
         * @param in Input pixels.
         * @return StreamResult with `processed` field set to number of bytes processed and `written` as
         * number of bytes written into `out`.
         *
         * The length of `out_buf` must be no less than 5.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` or `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than 5, or
         * - `Error::NotInitialized` if the encoder is not initialized.
         */
        Result<StreamResult> encode(ByteSpan out_buf, ByteCSpan in_buf) noexcept;

        /**
         * @brief Finalize the encoding process for this particular stream.
         *
         * @param out_buf Output buffer.
         * @return Number of written bytes.
         *
         * This function will both reset the internal state of the encoder and fill the `out_buf` with end
         * marker. Also, if there is still `OP_RUN` left it will be written to the buffer before the end
         * marker. You can use `has_run_count` to check whether you need to reserve extra 1 byte for `OP_RUN`
         * or not on top of end marker length (8 bytes).
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero, or
         * - `Error::TooShort` if the length of the buffer is less than header length, or
         * - `Error::NotInitialized` if the encoder is not initialized.
         */
        Result<std::size_t> finalize(ByteSpan out_buf) noexcept;

        /**
         * @brief Reset the internal state of the decoder.
         *
         * Useful if you want to abort the stream and start over starting with clean encoder. This function
         * will do nothing if the encoder is not initialized.
         */
        void reset() noexcept;

        /**
         * @brief Check whether an `OP_RUN` count stored is non-zero.
         */
        bool has_run_count() const noexcept { return m_run > 0; }

        /**
         * @brief Check if encoder is already initialized.
         */
        bool is_initialized() const noexcept { return m_channels.has_value(); }

    private:
        using RunningArray = PixelArr<constants::running_array_size>;

        std::optional<Channels> m_channels;
        Byte                    m_run;
        Pixel                   m_prev;
        RunningArray            m_seen;
    };

    class StreamDecoder
    {
    public:
        StreamDecoder() noexcept;

        /**
         * @brief Prepare encoder and parse header data.
         *
         * @param in_buf Buffer representing the header of qoi image.
         *
         * This function returns
         * - `Error::Empty` if the length of the `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than header length,
         * - `Error::TooBig` if the image is too big to process,
         * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value, or
         * - `Error::AlreadyInitialized` if the encoder is already initialized.
         */
        Result<Desc> initialize(ByteCSpan in_buf) noexcept;

        /**
         * @brief Encode the pixel data into out.
         *
         * @param out The output buffer.
         * @param in Input pixels in bytes.
         * @return StreamResult with `processed` field set to number of bytes processed and `written` as
         * number of bytes written into `out`.
         *
         * The length of `out_buf` must be at least 4 bytes for RGBA and 3 bytes for RGB.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` or `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than 4 (RGBA) or 3 (RGB), or
         * - `Error::NotInitialized` if the encoder is not initialized.
         */
        Result<StreamResult> decode(ByteSpan out_buf, ByteCSpan in_buf) noexcept;

        /**
         * @brief Drain remaining `OP_RUN` count if exist.
         *
         * @param out_buf Output buffer.
         * @return Number of written bytes.
         *
         * You might need to call this function multiple times if your buffer is not enough to drain the all
         * of the instruction. `OP_RUN` can produce at most 62 pixels, so that would be 186 bytes for RGB and
         * 248 bytes for RGBA. You can check whether the stored run count is non-zero using `has_run_count`.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero, or
         * - `Error::NotInitialized` if the encoder is not initialized.
         */
        Result<std::size_t> drain(ByteSpan out_buf) noexcept;

        /**
         * @brief Reset the internal state of the decoder.
         *
         * You are required to call this function to reset the internal state of the decoder. This function
         * will do nothing if the encoder is not initialized.
         */
        void reset() noexcept;

        /**
         * @brief Check whether an `OP_RUN` count stored is non-zero.
         */
        bool has_run_count() const noexcept { return m_run > 0; }

        /**
         * @brief Check if encoder is already initialized.
         */
        bool is_initialized() const noexcept { return m_channels.has_value(); }

    private:
        using RunningArray = PixelArr<constants::running_array_size>;

        std::optional<Channels> m_channels;
        Byte                    m_run;
        Pixel                   m_prev;
        RunningArray            m_seen;
    };
}

#endif
