#include "test_entry.h"
#include <meta>
#include <print>
#include <string>
#include <concepts>
#include <optional>

enum class WeekDay{
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    Sunday
};

enum class BitEnum : uint32_t {
    kRead = 0x01 ,
    kWrite = 0x02,
    kXn = 0x04
};

const char* impl_classic_(WeekDay _week_day)
{
    switch (_week_day) {
        case WeekDay::Monday:
            return "Monday";
        case WeekDay::Tuesday:
            return "Tuesday";
        case WeekDay::Wednesday:
            return "Wednesday";
        case WeekDay::Thursday:
            return "Thursday";
        case WeekDay::Friday:
            return "Friday";
        case WeekDay::Saturday:
            return "Saturday";
        case WeekDay::Sunday:
            return "Sunday";
        default:
            return "Unknown";
    }
}

template <typename E>
requires std::is_enum_v<E>
const char* impl_reflection_(E _value)
{
    template for (constexpr std::meta::info e : std::define_static_array(
        std::meta::enumerators_of(^^E))
    )
    {
        if (_value == [:e:]) {
            return std::meta::display_string_of(e).data();
        }
    }
    return "Unknown";
}

template <typename E>
requires std::is_enum_v<E>
constexpr std::optional<E> impl_reflection_(std::string_view _str)
{
    template for (constexpr std::meta::info e : std::define_static_array(
        std::meta::enumerators_of(^^E))
    )
    {
        if (_str == std::meta::identifier_of(e)) {
            return [:e:];
        }
    }
    return std::nullopt;
}

template <typename E>
requires std::is_enum_v<E>
consteval auto impl_reflection_enum_list_val()
{
    constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    std::array<E, enumerators.size()> values;
    size_t index = 0;
    template for (constexpr std::meta::info e : enumerators)
    {
        values[index++] = [:e:];
    }
    return values;
}

template <typename E>
requires std::is_enum_v<E>
consteval auto impl_reflection_enum_list_str()
{
    constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    std::array<std::string_view, enumerators.size()> values;
    size_t index = 0;
    template for (constexpr std::meta::info e : enumerators)
    {
        values[index++] = std::meta::identifier_of(e);
    }
    return values;
}

template <typename E>
requires std::is_enum_v<E>
std::string impl_reflection_enum_bits_str(E _value)
{
    std::string str;
    auto raw = static_cast<std::underlying_type_t<E>>(_value);

    constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    template for (constexpr std::meta::info e : enumerators)
    {
        auto bit = static_cast<std::underlying_type_t<E>>([:e:]);
        if (raw & bit) {
            if (!str.empty()) {
                str += " | ";
            }
            str += std::meta::identifier_of(e);
        }
    }
    return str;
}

void test_entry::implement_enumeration_reflection()
{
    std::println("[Implement Enumeration Reflection] ----------- START -----------");

    std::println("impl_classic_(WeekDay::Monday) -> {}", impl_classic_(WeekDay::Monday));

    {
        std::println("impl_reflection_(WeekDay::Monday) -> {}", impl_reflection_(WeekDay::Monday));
        std::string str = "Thursday";
        std::println("impl_reflection_<WeekDay>(\"Thursday\") -> {}", (int)(impl_reflection_<WeekDay>(str).value()));
    }

    {
        std::println("impl_reflection_enum_list_val<WeekDay>() ->");
        auto list_val = impl_reflection_enum_list_val<WeekDay>();
        std::string str;
        for (const auto& val : list_val) {
            str += std::to_string((int)(val)) + ", ";
        }
        std::println("impl_reflection_enum_list_val<WeekDay>() -> {}", str);

        auto list_str = impl_reflection_enum_list_str<WeekDay>();
        str = "";
        for (const auto& s : list_str) {
            str += std::string(s) + ", ";
        }
        std::println("impl_reflection_enum_list_str<WeekDay>() -> {}", str);
    }

    {
        auto bits = static_cast<BitEnum>((int)(BitEnum::kRead) | (int)(BitEnum::kWrite));
        std::println("impl_reflection_enum_bits_str<BitEnum>(BitEnum::kRead | BitEnum::kWrite) -> {}", impl_reflection_enum_bits_str(bits));
    }

    std::println("[Implement Enumeration Reflection] ----------- END -----------");
}