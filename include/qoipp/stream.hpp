#ifndef QOIPP_STREAM_HPP_43EWR7VETY
#define QOIPP_STREAM_HPP_43EWR7VETY

#include "qoipp/common.hpp"

namespace qoipp
{
    Result<void> write_header(ByteSpan out_buf, Desc desc) noexcept;

    Result<void> write_end_marker(ByteSpan out_buf) noexcept;

    struct StreamResult
    {
        std::size_t processed;
        std::size_t written;
    };

    class StreamEncoder
    {
    public:
        StreamEncoder(Channels channels) noexcept;

        /**
         * @brief Encode the pixel data into out.
         *
         * @param out The output buffer.
         * @param in Input pixels.
         * @return StreamResult with `processed` field set to number of pixels processed and `written` as
         * number of bytes written into `out`.
         */
        StreamResult encode(ByteSpan out, ByteCSpan in) noexcept;

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
