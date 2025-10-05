#include "qoipp/stream.hpp"

#include "util.hpp"

#include <cassert>
#include <utility>

namespace qoipp::impl
{
    class StreamByteReader
    {
    public:
        StreamByteReader(ByteCSpan bytes)
            : m_bytes{ bytes }
        {
        }

        // return empty span if out of bound
        ByteCSpan read(u32 num)
        {
            if (not can_read(m_index + num - 1)) {
                return {};
            }
            auto span  = m_bytes.subspan(m_index, num);
            m_index   += num;
            return span;
        }

        void decr(usize amount)
        {
            if (m_index < amount) {
                assert(false and "should not happen, logic error");
                return;
            }
            m_index -= amount;
        }

        usize count() const { return m_index; }
        bool  ok() const { return m_ok; }

    private:
        bool can_read(usize index)
        {
            if (not m_ok or index >= m_bytes.size()) {
                return m_ok = false;
            }
            return true;
        }

        ByteCSpan m_bytes;
        usize     m_index = 0;
        bool      m_ok    = true;
    };

    class StreamPixelReader
    {
    public:
        StreamPixelReader(ByteCSpan bytes, Channels channels)
            : m_bytes{ bytes.begin(), bytes.size() - bytes.size() % static_cast<u8>(channels) }
            , m_channels{ channels }
        {
        }

        std::optional<Pixel> read()
        {
            if (not can_read()) {
                return std::nullopt;
            }
            auto offset = m_pixel_index * static_cast<u8>(m_channels);
            auto alpha  = m_channels == Channels::RGBA ? m_bytes[offset + 3] : static_cast<Byte>(0xFF);

            ++m_pixel_index;
            return Pixel{ m_bytes[offset], m_bytes[offset + 1], m_bytes[offset + 2], alpha };
        }

        void decr()
        {
            if (m_pixel_index > 0) {
                --m_pixel_index;
            }
        }

        usize count() const { return m_pixel_index * static_cast<u8>(m_channels); }
        bool  ok() const { return m_ok; }

    private:
        bool can_read()
        {
            auto offset = m_pixel_index * static_cast<u8>(m_channels);
            if (not m_ok or offset >= m_bytes.size()) {
                return m_ok = false;
            }
            return true;
        }

        ByteCSpan m_bytes;
        Channels  m_channels;
        usize     m_pixel_index = 0;
        bool      m_ok          = true;
    };
}

namespace qoipp
{
    StreamEncoder::StreamEncoder() noexcept
        : m_channels{}
        , m_run{ 0 }
        , m_prev{ constants::start }
        , m_seen{}
    {
    }

    Result<std::size_t> StreamEncoder::initialize(ByteSpan out_buf, Desc desc) noexcept
    {
        if (m_channels) {
            return make_error<std::size_t>(Error::AlreadyInitialized);
        }

        if (out_buf.size() == 0) {
            return make_error<std::size_t>(Error::Empty);
        } else if (out_buf.size() < constants::header_size) {
            return make_error<std::size_t>(Error::TooShort);
        } else if (auto bytes = count_bytes(desc); not bytes) {
            return make_error<std::size_t>(bytes.error());
        }

        auto [width, height, channels, colorspace] = desc;

        auto writer      = util::SimpleByteWriter{ out_buf };
        auto chunk_array = util::ChunkArray<decltype(writer), false>{ writer };

        chunk_array.write_header(width, height, channels, colorspace);

        m_channels = desc.channels;
        return constants::header_size;
    }

    Result<StreamResult> StreamEncoder::encode(ByteSpan out_buf, ByteCSpan in_buf) noexcept
    {
        if (not m_channels) {
            return make_error<StreamResult>(Error::NotInitialized);
        } else if (out_buf.empty() or in_buf.empty()) {
            return make_error<StreamResult>(Error::Empty);
        } else if (out_buf.size() < 5) {    // OP_RGBA need 5 bytes
            return make_error<StreamResult>(Error::TooShort);
        }

        auto reader = impl::StreamPixelReader{ in_buf, *m_channels };
        auto writer = util::SimpleByteWriter{ out_buf };
        auto chunks = util::ChunkArray<decltype(writer), true>{ writer };

        auto index        = u8{ 0 };
        auto seen_prev    = Pixel{};
        auto seen_engaged = false;
        auto last_op      = util::Tag{};

        while (auto may_curr = reader.read()) {
            auto curr = *may_curr;

            if (m_prev == curr) {
                ++m_run;
                if (m_run == constants::run_limit) {
                    last_op = util::Tag::OP_RUN;
                    chunks.write_run(m_run);
                    if (not chunks.ok()) {
                        --m_run;
                        break;
                    }
                    m_run = 0;
                }
            } else {
                if (m_run > 0) {
                    last_op = util::Tag::OP_RUN;
                    chunks.write_run(m_run);
                    if (not chunks.ok()) {
                        break;
                    }
                    m_run = 0;
                }

                index = util::hash(curr) % constants::running_array_size;

                // OP_INDEX
                if (m_seen[index] == curr) {
                    last_op = util::Tag::OP_INDEX;
                    chunks.write_index(index);
                } else {
                    seen_prev    = std::exchange(m_seen[index], curr);
                    seen_engaged = true;

                    if (m_channels == Channels::RGBA and m_prev.a != curr.a) {
                        last_op = util::Tag::OP_RGBA;
                        chunks.write_rgba(curr);
                        if (not chunks.ok()) {
                            break;
                        }
                        m_prev = curr;
                        continue;
                    }

                    // OP_DIFF and OP_LUMA
                    const i8 dr = curr.r - m_prev.r;
                    const i8 dg = curr.g - m_prev.g;
                    const i8 db = curr.b - m_prev.b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (util::should_diff(dr, dg, db)) {
                        last_op = util::Tag::OP_DIFF;
                        chunks.write_diff(dr, dg, db);
                    } else if (util::should_luma(dg, dr_dg, db_dg)) {
                        last_op = util::Tag::OP_LUMA;
                        chunks.write_luma(dg, dr_dg, db_dg);
                    } else {
                        last_op = util::Tag::OP_RGB;
                        chunks.write_rgb(curr);
                    }
                }
            }

            if (not chunks.ok()) {
                break;
            }
            m_prev = curr;
        }

        if (not chunks.ok() and reader.ok()) {
            // revert seen pixels
            if (seen_engaged and last_op != util::Tag::OP_RUN and last_op != util::Tag::OP_INDEX) {
                m_seen[index] = seen_prev;
            }

            // revert reader count
            reader.decr();
        }

        return StreamResult{ reader.count(), chunks.count() };
    }

    Result<usize> StreamEncoder::finalize(ByteSpan out_buf) noexcept
    {
        if (not m_channels) {
            return make_error<usize>(Error::NotInitialized);
        } else if (out_buf.size() == 0) {
            return make_error<usize>(Error::Empty);
        } else if (out_buf.size() < constants::end_marker_size + has_run_count()) {
            return make_error<usize>(Error::TooShort);
        }

        auto writer      = util::SimpleByteWriter{ out_buf };
        auto chunk_array = util::ChunkArray<decltype(writer), false>{ writer };

        if (has_run_count()) {
            chunk_array.write_run(m_run);
        }
        chunk_array.write_end_marker();

        auto write = constants::end_marker_size + has_run_count();

        m_channels.reset();
        m_run  = 0;
        m_prev = constants::start;
        m_seen.fill(Pixel{});

        return write;
    }

    void StreamEncoder::reset() noexcept
    {
        if (m_channels.has_value()) {
            m_channels.reset();
            m_run  = 0;
            m_prev = constants::start;
            m_seen.fill(Pixel{});
        }
    }
}

namespace qoipp
{
    StreamDecoder::StreamDecoder() noexcept
        : m_channels{}
        , m_run{ 0 }
        , m_prev{ constants::start }
        , m_seen{}
    {
    }

    Result<Desc> StreamDecoder::initialize(ByteCSpan in_buf, std::optional<Channels> target) noexcept
    {
        if (m_channels) {
            return make_error<Desc>(Error::AlreadyInitialized);
        }

        auto desc = read_header(in_buf);
        if (desc) {
            if (auto bytes = count_bytes(*desc); not bytes) {
                return make_error<Desc>(bytes.error());
            }

            m_target       = target.value_or(desc->channels);
            m_channels     = m_target;
            desc->channels = m_channels.value();

            m_seen[util::hash(m_prev) % constants::running_array_size] = m_prev;
        }

        return desc;
    }

    Result<StreamResult> StreamDecoder::decode(ByteSpan out_buf, ByteCSpan in_buf) noexcept
    {
        if (not m_channels) {
            return make_error<StreamResult>(Error::NotInitialized);
        } else if (out_buf.size() == 0) {
            return make_error<StreamResult>(Error::Empty);
        } else if (out_buf.size() < static_cast<u8>(*m_channels)) {
            return make_error<StreamResult>(Error::TooShort);
        }

        auto channels = static_cast<u8>(*m_channels);
        auto write    = [&](Pixel pixel, u64 index) {
            const auto chan = static_cast<u8>(*m_target);
            const auto off  = index * chan;
            std::memcpy(out_buf.data() + off, &pixel, chan);
        };

        auto reader = impl::StreamByteReader{ in_buf };

        usize last_read   = 0u;
        usize pixel_index = 0;

        for (; pixel_index < out_buf.size() / channels; ++pixel_index) {
            if (m_run > 0) {
                --m_run;
                write(m_prev, pixel_index);
                continue;
            }

            auto may_tag = reader.read(1);
            if (may_tag.empty()) {
                break;
            }
            last_read = may_tag.size();
            auto curr = m_prev;

            switch (auto tag = static_cast<util::Tag>(may_tag[0]); tag) {
            case util::Tag::OP_RGB: {
                auto arr = reader.read(3);
                if (arr.empty()) {
                    reader.decr(last_read);
                    goto exit_loop;
                }
                last_read += arr.size();

                curr.r = arr[0];
                curr.g = arr[1];
                curr.b = arr[2];
            } break;
            case util::Tag::OP_RGBA: {
                auto arr = reader.read(4);
                if (arr.empty()) {
                    reader.decr(last_read);
                    goto exit_loop;
                }
                last_read += arr.size();

                curr.r = arr[0];
                curr.g = arr[1];
                curr.b = arr[2];
                curr.a = arr[3];
            } break;
            default:
                switch (tag & 0b11000000) {
                case util::Tag::OP_INDEX: {
                    auto& pixel = m_seen[tag & 0b00111111];
                    curr        = pixel;
                } break;
                case util::Tag::OP_DIFF: {
                    const i8 dr = ((tag & 0b00110000) >> 4) - constants::bias_op_diff;
                    const i8 dg = ((tag & 0b00001100) >> 2) - constants::bias_op_diff;
                    const i8 db = ((tag & 0b00000011)) - constants::bias_op_diff;

                    curr.r = static_cast<u8>(dr + m_prev.r);
                    curr.g = static_cast<u8>(dg + m_prev.g);
                    curr.b = static_cast<u8>(db + m_prev.b);
                } break;
                case util::Tag::OP_LUMA: {
                    const auto red_blue = reader.read(1);
                    if (red_blue.empty()) {
                        reader.decr(last_read);
                        goto exit_loop;
                    }
                    last_read += red_blue.size();

                    const u8 dg    = (tag & 0b00111111) - constants::bias_op_luma_g;
                    const u8 dr_dg = ((red_blue[0] & 0b11110000) >> 4) - constants::bias_op_luma_rb;
                    const u8 db_dg = (red_blue[0] & 0b00001111) - constants::bias_op_luma_rb;

                    curr.r = static_cast<u8>(dg + dr_dg + m_prev.r);
                    curr.g = static_cast<u8>(dg + m_prev.g);
                    curr.b = static_cast<u8>(dg + db_dg + m_prev.b);
                } break;
                case util::Tag::OP_RUN: {
                    m_run     = static_cast<u8>((tag & 0b00111111) - constants::bias_op_run);
                    last_read = 0;    // since run stored independently, no need to backtrack if write fail
                    --m_run;
                } break;
                default: [[unlikely]] /* invalid tag (is this even possible?)*/;
                }
            }

            write(curr, pixel_index);
            m_prev = m_seen[util::hash(curr) % constants::running_array_size] = curr;
        }
    exit_loop:

        if (pixel_index > out_buf.size()) {
            reader.decr(last_read);
        }

        return StreamResult{ reader.count(), pixel_index * channels };
    }

    Result<std::size_t> StreamDecoder::drain_run(ByteSpan out_buf) noexcept
    {
        if (not m_channels) {
            return make_error<std::size_t>(Error::NotInitialized);
        } else if (out_buf.size() == 0) {
            return make_error<std::size_t>(Error::Empty);
        }

        auto writer  = util::SimplePixelWriter<true>{ out_buf, *m_channels };
        auto out_idx = 0u;

        while (m_run > 0) {
            writer.write(out_idx, m_prev);
            if (not writer.ok()) {
                break;
            }
            ++out_idx;
            --m_run;
        }

        return out_idx * static_cast<u8>(*m_channels);
    }

    void StreamDecoder::reset() noexcept
    {
        if (m_channels.has_value()) {
            m_channels.reset();
            m_target.reset();
            m_run  = 0;
            m_prev = constants::start;
            m_seen.fill(Pixel{});
        }
    }
}
