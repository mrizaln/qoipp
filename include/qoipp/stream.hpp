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
         * @return Number of bytes written into output buffer (always header length).
         *
         * This function will both prepare the internal state of the encoder and fill the `out_buf` with the
         * header information for QOI. You should not call this function twice. To reuse this encoder and
         * prepare for use for other image, you must call `finalize()` first then you can `initialize()`
         * again.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than header length,
         * - `Error::TooBig` if the image is too big to process,
         * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value, or
         * - `Error::AlreadyInitialized` if the encoder is already initialized.
         */
        Result<std::size_t> initialize(ByteSpan out_buf, Desc desc) noexcept;

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
         * marker. Also, if there is still `QOI_OP_RUN` left it will be written to the buffer before the end
         * marker. You can use `has_run_count()` to check whether you need to reserve extra 1 byte for
         * `QOI_OP_RUN` or not on top of end marker length (8 bytes).
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero, or
         * - `Error::TooShort` if the length of the buffer is less than header length, or
         * - `Error::NotInitialized` if the encoder is not initialized.
         */
        Result<std::size_t> finalize(ByteSpan out_buf) noexcept;

        /**
         * @brief Reset the internal state of the encoder.
         *
         * Useful if you want to abort the stream and start over starting with clean encoder. This function
         * will do nothing if the encoder is not initialized.
         */
        void reset() noexcept;

        /**
         * @brief Check whether an `QOI_OP_RUN` count stored is non-zero.
         */
        bool has_run_count() const noexcept { return m_run > 0; }

        /**
         * @brief Get the output channels.
         *
         * This function will return `std::nullopt` if the encoder is not initialized.
         */
        std::optional<Channels> channels() const noexcept { return m_channels; }

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
         * @brief Prepare decoder and parse header data.
         *
         * @param in_buf Buffer representing the header of QOI image.
         * @param target The target channels to extract; if `std::nullopt` the original channels will be used.
         *
         * If the underlying data is RGB and the target is RGBA, the alpha channel will be set to 0xFF. To
         * reuse this decoder and prepare for use for other image, you must call `reset()` first then you can
         * `initialize()` again.
         *
         * This function returns
         * - `Error::Empty` if the length of the `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than header length,
         * - `Error::TooBig` if the image is too big to process,
         * - `Error::InvalidDesc` if any of the field of `Desc` contains invalid value, or
         * - `Error::AlreadyInitialized` if the decoder is already initialized.
         */
        Result<Desc> initialize(ByteCSpan in_buf, std::optional<Channels> target = std::nullopt) noexcept;

        /**
         * @brief Encode the pixel data into out.
         *
         * @param out The output buffer.
         * @param in Input pixels in bytes.
         * @return StreamResult with `processed` field set to number of bytes processed and `written` as
         * number of bytes written into `out`.
         *
         * The length of `out_buf` must be at least 4 bytes for RGBA target and 3 bytes for RGB target. You
         * are meant to call this function repeatedly until the decoding process complete. It is the caller's
         * responsibility to keep track whether the decoding process is completed and stop calling this
         * function. The decoder won't keep track of the number of the pixels produced from this function
         * calls. If the caller has exhausted all the QOI input data, the decoder may still not written all
         * the pixels from `QOI_OP_RUN`, thus you should call `drain_run()` before resetting the decoder.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` or `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than 4 (RGBA) or 3 (RGB), or
         * - `Error::NotInitialized` if the decoder is not initialized.
         */
        Result<StreamResult> decode(ByteSpan out_buf, ByteCSpan in_buf) noexcept;

        /**
         * @brief Drain remaining `QOI_OP_RUN` count if exist.
         *
         * @param out_buf Output buffer.
         * @return Number of written bytes.
         *
         * You might need to call this function multiple times if your buffer is not enough to drain the
         * `QOI_OP_RUN` instruction. `QOI_OP_RUN` can produce at most 62 pixels, so that would be 186 bytes
         * for RGB and 248 bytes for RGBA. You can check whether the stored run count is non-zero using
         * `has_run_count` member function.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero, or
         * - `Error::NotInitialized` if the decoder is not initialized.
         */
        Result<std::size_t> drain_run(ByteSpan out_buf) noexcept;

        /**
         * @brief Reset the internal state of the decoder.
         *
         * Useful if you want to abort the stream and start over starting with clean decoder. You are required
         * to call this function first before calling `initialize()` to start over. This function will do
         * nothing if the decoder is not initialized.
         */
        void reset() noexcept;

        /**
         * @brief Check whether an `QOI_OP_RUN` count stored is non-zero.
         */
        bool has_run_count() const noexcept { return m_run > 0; }

        /**
         * @brief Get `QOI_OP_RUN` count.
         *
         * The count won't exceed 62.
         */
        Byte run_count() const noexcept { return m_run; }

        /**
         * @brief Get the input channels.
         *
         * This function will return `std::nullopt` if the decoder is not initialized.
         */
        std::optional<Channels> channels() const noexcept { return m_channels; }

        /**
         * @brief Get the target channels.
         *
         * This function will return `std::nullopt` if the decoder is not initialized.
         */
        std::optional<Channels> target() const noexcept { return m_target; }

        /**
         * @brief Check if decoder is already initialized.
         */
        bool is_initialized() const noexcept { return m_channels.has_value(); }

    private:
        using RunningArray = PixelArr<constants::running_array_size>;

        std::optional<Channels> m_channels;
        std::optional<Channels> m_target;
        Byte                    m_run;
        Pixel                   m_prev;
        RunningArray            m_seen;
    };
}

#endif
