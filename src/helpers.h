#pragma once

#include <cstddef>
#include <meta>
#include <print>
#include <string>
#include <string_view>

// Shared helpers for the reflection entry points. Kept out of "test_entry.h" so
// that file stays a plain table of contents.
namespace test_entry {

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
        std::println("{} {} {}", std::string(kPad, '='), _name, std::string(kPad, '='));
    }

    ~section()
    {
        std::println("{}", std::string(m_rule_width, '='));
        std::println();
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

} // namespace test_entry
