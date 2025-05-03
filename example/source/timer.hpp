#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>
#include <concepts>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>

// #define TIMER_FORCE_PRINT

namespace timer_detail
{
    template <typename Type>
    concept Duration = std::same_as<Type, std::chrono::seconds>
                    || std::same_as<Type, std::chrono::milliseconds>
                    || std::same_as<Type, std::chrono::microseconds>
                    || std::same_as<Type, std::chrono::nanoseconds>;
}

template <timer_detail::Duration D = std::chrono::milliseconds>
class                                                                                     //
    [[nodiscard("Value will be destroyed immediately (will outputs incorrect time)")]]    //
    Timer
{
public:
    // type aliases
    using clock_type  = std::chrono::steady_clock;
    using second_type = D;

    inline static bool s_doPrint{ true };

public:
    static void once(std::function<void()> func, const std::string& name = "[unnamed]")
    {
        Timer timer{ name };
        func();
    }

public:
    Timer(const std::string& name, bool doAutoPrint = true)
        : m_name{ name }
        , m_doAutoPrint{ doAutoPrint }
    {
    }

    // #if (!defined(NDEBUG) or defined(TIMER_FORCE_PRINT)) and !defined(TIMER_SUPPRESS_PRINT)
    ~Timer()
    {
        if (s_doPrint && m_doAutoPrint) {
            print();
        }
    }
    // #endif

    void reset() { m_beginning = clock_type::now(); }

    auto elapsed() const
    {
        return std::chrono::duration_cast<second_type>(clock_type::now() - m_beginning).count();
    }

    double elapsedAndReset()
    {
        auto time{ elapsed() };
        reset();
        return time;
    }

    double elapsedAndStop()
    {
        m_doAutoPrint = false;
        return elapsed();
    }

    void print()
    {
        if constexpr (std::is_same_v<D, std::chrono::seconds>) {
            std::cout << m_name << ": " << elapsed() << " au\n";
        } else if constexpr (std::is_same_v<D, std::chrono::milliseconds>) {
            std::cout << m_name << ": " << elapsed() << " ms\n";
        } else if constexpr (std::is_same_v<D, std::chrono::microseconds>) {
            std::cout << m_name << ": " << elapsed() << " us\n";
        } else if constexpr (std::is_same_v<D, std::chrono::nanoseconds>) {
            std::cout << m_name << ": " << elapsed() << " ns\n";
        } else {
            std::cout << m_name << ": " << elapsed() << " au\n";
        }
    }

private:
    const std::string                   m_name;
    bool                                m_doAutoPrint;
    std::chrono::time_point<clock_type> m_beginning{ clock_type::now() };
};

namespace timer_detail
{
    template <typename D>
    struct TimerCallerDummy
    {
        const char* name;

        template <std::invocable F>
        friend auto operator*(TimerCallerDummy&& d, F&& f) -> std::invoke_result_t<F>
        {
            Timer<D> timer{ d.name };
            return f();
        }
    };
}

#define DO_TIME_US(Cstr) timer_detail::TimerCallerDummy<std::chrono::microseconds>{ "[DO_TIME] " Cstr }* [&]()
#define DO_TIME_MS(Cstr) timer_detail::TimerCallerDummy<std::chrono::milliseconds>{ "[DO_TIME] " Cstr }* [&]()
#define DO_TIME(Cstr)    DO_TIME_MS (Cstr)

#endif /* ifndef TIMER_HPP */
