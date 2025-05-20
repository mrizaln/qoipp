#include "qoipp/stream.hpp"
#include "util.hpp"
#include <iostream>
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

        std::optional<u8> read_one()
        {
            if (not can_read(m_index)) {
                return std::nullopt;
            }
            return m_bytes[m_index++];
        }

        std::optional<ByteArr<3>> read_three()
        {
            if (not can_read(m_index + 3 - 1)) {
                return std::nullopt;
            }
            auto idx  = m_index;
            auto arr  = ByteArr{ m_bytes[idx], m_bytes[idx + 1], m_bytes[idx + 2] };
            m_index  += 3;
            return arr;
        }

        std::optional<ByteArr<4>> read_four()
        {
            if (not can_read(m_index + 4 - 1)) {
                return std::nullopt;
            }
            auto idx  = m_index;
            auto arr  = ByteArr{ m_bytes[idx], m_bytes[idx + 1], m_bytes[idx + 2], m_bytes[idx + 3] };
            m_index  += 4;
            return arr;
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
        usize     m_index;
        bool      m_ok = true;
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

    Result<void> StreamEncoder::initialize(ByteSpan out_buf, Desc desc) noexcept
    {
        if (out_buf.size() == 0) {
            return make_error<void>(Error::Empty);
        } else if (out_buf.size() < constants::header_size) {
            return make_error<void>(Error::TooShort);
        } else if (auto bytes = count_bytes(desc); not bytes) {
            return make_error<void>(bytes.error());
        }

        auto [width, height, channels, colorspace] = desc;

        auto writer      = util::SimpleByteWriter{ out_buf };
        auto chunk_array = util::ChunkArray<decltype(writer), false>{ writer };

        chunk_array.write_header(width, height, channels, colorspace);

        m_channels = desc.channels;
        return Result<void>{};
    }

    Result<StreamResult> StreamEncoder::encode(ByteSpan out_buf, ByteCSpan in_buf) noexcept
    {
        if (not m_channels) {
            return make_error<StreamResult>(Error::NotInitialized);
        } else if (out_buf.empty() or in_buf.empty()) {
            return make_error<StreamResult>(Error::Empty);
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

    Result<void> StreamEncoder::finalize(ByteSpan out_buf) noexcept
    {
        if (out_buf.size() == 0) {
            return make_error<void>(Error::Empty);
        } else if (out_buf.size() < constants::end_marker_size + has_run_count()) {
            return make_error<void>(Error::TooShort);
        }

        auto writer      = util::SimpleByteWriter{ out_buf };
        auto chunk_array = util::ChunkArray<decltype(writer), false>{ writer };

        if (has_run_count()) {
            chunk_array.write_run(m_run);
        }
        chunk_array.write_end_marker();

        m_channels.reset();
        m_run  = 0;
        m_prev = constants::start;
        m_seen.fill(Pixel{});

        return Result<void>{};
    }
}

namespace qoipp
{
    StreamDecoder::StreamDecoder(Channels channels) noexcept
        : m_channels{ channels }
        , m_run{ 0 }
        , m_prev{ constants::start }
        , m_seen{}
    {
    }

    StreamResult StreamDecoder::decode(ByteSpan out, ByteCSpan in) noexcept
    {
        auto reader  = impl::StreamByteReader{ in };
        auto writer  = util::SimplePixelWriter<true>{ out, m_channels };
        auto out_idx = 0u;

        if (m_run > 0) {
            while (m_run-- > 0 and writer.ok()) {
                writer.write(out_idx++, m_prev);
            }
            --out_idx;
            if (not writer.ok()) {
                return { 0, out_idx };
            }
        }

        while (reader.ok() and writer.ok()) {
            auto tag = reader.read_one();
            if (not tag) {
                break;
            }

            auto curr = m_prev;

            switch (*tag) {
            case util::Tag::OP_RGB: {
                auto arr = reader.read_three();
                if (not arr) {
                    break;
                }
                curr.r = arr->at(0);
                curr.g = arr->at(1);
                curr.b = arr->at(2);
            } break;
            case util::Tag::OP_RGBA: {
                auto arr = reader.read_four();
                if (not arr) {
                    break;
                }
                curr.r = arr->at(0);
                curr.g = arr->at(1);
                curr.b = arr->at(2);
                curr.a = arr->at(3);
            } break;
            default:
                switch (*tag & 0b11000000) {
                case util::Tag::OP_INDEX: {
                    auto& pixel = m_seen[*tag & 0b00111111];
                    curr        = pixel;
                } break;
                case util::Tag::OP_DIFF: {
                    const i8 dr = ((*tag & 0b00110000) >> 4) - constants::bias_op_diff;
                    const i8 dg = ((*tag & 0b00001100) >> 2) - constants::bias_op_diff;
                    const i8 db = ((*tag & 0b00000011)) - constants::bias_op_diff;

                    curr.r = static_cast<u8>(dr + m_prev.r);
                    curr.g = static_cast<u8>(dg + m_prev.g);
                    curr.b = static_cast<u8>(db + m_prev.b);
                } break;
                case util::Tag::OP_LUMA: {
                    const auto red_blue = reader.read_one();
                    if (not red_blue) {
                        break;
                    }

                    const u8 dg    = (*tag & 0b00111111) - constants::bias_op_luma_g;
                    const u8 dr_dg = ((*red_blue & 0b11110000) >> 4) - constants::bias_op_luma_rb;
                    const u8 db_dg = (*red_blue & 0b00001111) - constants::bias_op_luma_rb;

                    curr.r = static_cast<u8>(dg + dr_dg + m_prev.r);
                    curr.g = static_cast<u8>(dg + m_prev.g);
                    curr.b = static_cast<u8>(dg + db_dg + m_prev.b);
                } break;
                case util::Tag::OP_RUN: {
                    m_run = static_cast<u8>((*tag & 0b00111111) - constants::bias_op_run);
                    while (m_run-- > 0 and writer.ok()) {
                        writer.write(out_idx++, m_prev);
                    }
                    --out_idx;
                    if (not writer.ok()) {
                        break;
                    }
                    continue;
                } break;
                default: [[unlikely]] /* invalid tag (is this even possible?)*/;
                }
            }

            writer.write(out_idx, curr);
            m_prev = m_seen[util::hash(curr) % constants::running_array_size] = curr;
        }

        return { reader.count(), out_idx };
    }
}
