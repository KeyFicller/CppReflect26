#pragma once

#include <cstddef>
#include <meta>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// Shared helpers for the reflection entry points. Kept out of "test_entry.h" so
// that file stays a plain table of contents.
namespace test_entry {

/// The demo's colored console logger, on stdout. The whole suite prints through
/// the levels below, so the visual language is the same everywhere:
///   debug -> cyan  : structure (section banners, "--- sub-headings ---")
///   trace -> plain : result lines (uncolored, follows the terminal theme)
///   info  -> green : a check that passed
///   error -> red   : a check that failed / a diagnostic
/// Color is forced on: spdlog's default only colors a TTY, and a debugger
/// console (LLDB / codelldb) is not one, so the demo would come out plain there.
/// Nothing on the run_request / --serve path logs here, so the stdout JSON
/// contract stays intact.
inline std::shared_ptr<spdlog::logger>& log()
{
    static std::shared_ptr<spdlog::logger> logger = [] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_color_mode(spdlog::color_mode::always);
        sink->set_color(spdlog::level::trace, "");
        auto instance = std::make_shared<spdlog::logger>("cppreflect", std::move(sink));
        instance->set_pattern("%^%v%$");
        instance->set_level(spdlog::level::trace);
        return instance;
    }();
    return logger;
}

/// Colored diagnostics on stderr (startup/error messages).
inline std::shared_ptr<spdlog::logger>& log_err()
{
    static std::shared_ptr<spdlog::logger> logger = [] {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        sink->set_color_mode(spdlog::color_mode::always);
        auto instance = std::make_shared<spdlog::logger>("cppreflect-err", std::move(sink));
        instance->set_pattern("%^%v%$");
        instance->set_level(spdlog::level::trace);
        return instance;
    }();
    return logger;
}

// RAII section banner: prints the header on construction and a rule line of
// matching width (plus a blank line) on destruction:
//   =============== Hello Reflection ================
//   ...
//   ==================================================
class section {
public:
    explicit section(std::string_view _name)
        : m_rule_width(kPad * 2 + _name.size() + 2)
    {
        log()->debug("{} {} {}", std::string(kPad, '='), _name, std::string(kPad, '='));
    }

    ~section()
    {
        log()->debug("{}", std::string(m_rule_width, '='));
        log()->debug("");
    }

    section(const section&) = delete;
    section& operator=(const section&) = delete;

private:
    static constexpr std::size_t kPad = 15;
    std::size_t m_rule_width;
};

/// The member reflections of T as a static array, for use as a `template for`
/// range. Shared by the entry points instead of being redefined per translation unit.
template <typename T>
consteval auto member_static_array([[maybe_unused]] const T& _object)
{
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
}

/// Renders a range as `_sep`-separated text, mapping each element with `_to_string`.
template <typename Range, typename ToString>
std::string join(const Range& _range, ToString _to_string, std::string_view _sep = ", ")
{
    std::string out;
    bool first = true;
    for (const auto& element : _range) {
        if (!first) {
            out += _sep;
        }
        first = false;
        out += _to_string(element);
    }
    return out;
}

} // namespace test_entry
