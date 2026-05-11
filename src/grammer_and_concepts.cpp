#include "test_entry.h"
#include <meta>
#include <print>
#include <string>
#include <typeinfo>

struct PodStruct {
    int m_mem_int;
    double m_mem_real;
    std::string m_mem_str;

    PodStruct operator+(const PodStruct& _other) const
    {
        return {};
    }
};

namespace MyNameSpace {
    namespace Woops {
        
    }
}

template <typename T>
consteval auto memeber_static_array(const T& _object)
{
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
}


namespace {

template <std::meta::info R>
void print_meta_info()
{
    if constexpr (std::meta::has_identifier(R)) {
        std::println(
            "identifier={} display={} (has_identifier={})",
            std::meta::identifier_of(R),
            std::meta::display_string_of(R),
            std::meta::has_identifier(R));
    } else {
        std::println(
            "display={} (has_identifier={})",
            std::meta::display_string_of(R),
            std::meta::has_identifier(R));
    }
}

}  // namespace

void test_entry::grammar_and_concepts()
{
    std::println("[Grammar and Concepts] ----------- START -----------");

    {
        // Reflection operators
        // Can only reflect entities that known at compile time.

        print_meta_info<^^int>();  // reflect a built-in type
        print_meta_info<^^PodStruct>();  // reflect a struct/class type
        print_meta_info<^^PodStruct::operator+>(); // reflect a member function

        // What the hell ?
        print_meta_info<^^MyNameSpace::Woops>(); // reflect a namespace

        // Impressive !
        double pi = 3.14;
        print_meta_info<^^pi>(); // reflect a variable

        // can't reflect a run time expression
        // print_meta_info<^^(pi + 1.0)>();
    }

    {
        // Splice operator

        constexpr auto type_info = ^^int;
        typename[:type_info:] x = 1;  // splice a built-in type
        std::println("x = {}", x);

        constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^PodStruct, std::meta::access_context::unchecked()));
        constexpr auto first_member = members[0];
        PodStruct test = {.m_mem_int = 3, .m_mem_real = 2.0, .m_mem_str = "Hello"};
        std::println("test.m_mem_int = {}", test.[:first_member:]); // splice a member of a struct/class

    }


    // ------------------------------------------------------------
    // int  ------- ^^ -------> info{int} ----------[: :] ------> int
    // PodStruct ------- ^^ -------> info{PodStruct} ----------[: :] ------> PodStruct
    // PodStruct::operator+ ------- ^^ -------> info{PodStruct::operator+} ----------[: :] ------> .operator+

    {
        // Meta compare
        constexpr bool ii_same = ^^int == ^^int;
        std::println("^^int == ^^int = {}", ii_same);

        constexpr bool id_same = ^^int == ^^double;
        std::println("^^int == ^^double = {}", id_same);
    }

    {
        // template for
        // type is not fixed like generic programing
        template for (constexpr std::meta::info mem : memeber_static_array(PodStruct{})) {
            using MemberType = typename[:std::meta::type_of(mem):];

            std::println("Template for loop for memberType = {}", typeid(MemberType).name());
        }

        std::vector<int> v = {1, 2, 3};
        for (const int& i : v) {
            using MemberType = decltype(i);
            std::println("For loop for memberType = {}", typeid(MemberType).name());
        }
    }

    std::println("[Grammar and Concepts] ----------- END -----------");
    std::println();
}