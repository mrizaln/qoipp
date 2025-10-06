#ifndef TIMER_HPP_FGYUGWEH
#define TIMER_HPP_FGYUGWEH

#include <fmt/base.h>
#include <fmt/std.h>

#include <chrono>

namespace timer
{
    using Clock = std::chrono::steady_clock;
    using Sec   = std::chrono::duration<double>;
    using MSec  = std::chrono::duration<double, std::milli>;
    using USec  = std::chrono::microseconds;
    using NSec  = std::chrono::nanoseconds;

    template <typename Dur = MSec>
    auto time(std::invocable auto fn)
    {
        if constexpr (std::same_as<void, std::invoke_result_t<decltype(fn)>>) {
            auto start = Clock::now();
            std::move(fn)();
            auto duration = std::chrono::duration_cast<Dur>(Clock::now() - start);
            return std::pair{ std::move(std::monostate{}), duration };
        } else {
            auto start    = Clock::now();
            auto result   = std::move(fn)();
            auto duration = std::chrono::duration_cast<Dur>(Clock::now() - start);
            return std::pair{ std::move(result), duration };
        }
    }

    auto time_s(std::invocable auto fn)
    {
        return time<Sec>(std::move(fn));
    }

    auto time_ms(std::invocable auto fn)
    {
        return time<MSec>(std::move(fn));
    }

    auto time_us(std::invocable auto fn)
    {
        return time<USec>(std::move(fn));
    }

    auto time_ns(std::invocable auto fn)
    {
        return time<NSec>(std::move(fn));
    }

    template <typename Dur = MSec>
    auto time_print(std::string_view prefix, std::invocable auto fn)
    {
        fmt::print("[time] {}: ...", prefix);
        std::fflush(stdout);
        auto [res, dur] = time<Dur>(std::move(fn));
        fmt::println("\r[time] {}: {}", prefix, dur);
        return std::move(res);
    }

    auto time_print_s(std::string_view prefix, std::invocable auto fn)
    {
        return time_print<Sec>(prefix, std::move(fn));
    }

    auto time_print_ms(std::string_view prefix, std::invocable auto fn)
    {
        return time_print<MSec>(prefix, std::move(fn));
    }

    auto time_print_us(std::string_view prefix, std::invocable auto fn)
    {
        return time_print<USec>(prefix, std::move(fn));
    }

    auto time_print_ns(std::string_view prefix, std::invocable auto fn)
    {
        return time_print<NSec>(prefix, std::move(fn));
    }
}

#endif /* ifndef TIMER_HPP_FGYUGWEH */
