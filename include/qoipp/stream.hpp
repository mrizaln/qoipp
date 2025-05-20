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
         * @return StreamResult with `processed` field set to number of pixels processed and `written` as
         * number of bytes written into `out`.
         *
         * The length of `out_buf` must be no less than 5.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` or `in_buf` is zero,
         * - `Error::TooShort` if the length of the buffer is less than 5, or
         * - `Error::NotInitialized` if the encoder hasn't initialized yet.
         */
        Result<StreamResult> encode(ByteSpan out_buf, ByteCSpan in_buf) noexcept;

        /**
         * @brief Finalize the encoding process for this particular stream.
         *
         * @param out_buf Output buffer
         *
         * This function will both reset the internal state of the encoder and fill the `out_buf` with end
         * marker. Also, if there is still `OP_RUN` left it will be written to the buffer before the end
         * marker. You can use `has_run_count` to check whether you need to reserve extra 1 byte for `OP_RUN`
         * or not on top of end marker length.
         *
         * This function returns
         * - `Error::Empty` if the length of the `out_buf` is zero, or
         * - `Error::TooShort` if the length of the buffer is less than header length, or
         * - `Error::NotInitialized` if the encoder is already initialized.
         */
        Result<void> finalize(ByteSpan out_buf) noexcept;

        /**
         * @brief Check whether an `OP_RUN` count stored is non-zero.
         */
        bool has_run_count() const noexcept { return m_run > 0; }

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
        StreamDecoder(Channels channels) noexcept;

        /**
         * @brief Encode the pixel data into out.
         *
         * @param out The output buffer.
         * @param in Input pixels.
         * @return StreamResult with `processed` field set to number of bytes processed and `written` as
         * number of pixels written into `out`.
         */
        StreamResult decode(ByteSpan out, ByteCSpan in) noexcept;

        /**
         * @brief Reset the state of the encoder to initial state.
         */
        void reset();

    private:
        using RunningArray = PixelArr<constants::running_array_size>;

        Channels     m_channels;
        Byte         m_run;
        Pixel        m_prev;
        RunningArray m_seen;
    };
}

#endif
