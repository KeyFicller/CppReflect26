#include "test_entry.h"
#include "helpers.h"
#include <meta>
#include <string>
#include <typeinfo>

namespace {

struct PodStruct {
    int m_mem_int;
    double m_mem_real;
    std::string m_mem_str;

    PodStruct operator+([[maybe_unused]] const PodStruct& _other) const
    {
        return {};
    }
};

} // namespace

namespace MyNameSpace {
    namespace Woops {
        
    }
}

namespace {

template <std::meta::info R>
void print_meta_info()
{
    if constexpr (std::meta::has_identifier(R)) {
        test_entry::log()->trace("identifier={} display={} has_identifier=true",
                     std::meta::identifier_of(R), std::meta::display_string_of(R));
    } else {
        test_entry::log()->trace("identifier=- display={} has_identifier=false",
                     std::meta::display_string_of(R));
    }
}

}  // namespace

void test_entry::grammar_and_concepts()
{
    test_entry::section banner{"Grammar and Concepts"};

    {
        // Reflection operators. Only entities known at compile time can be
        // reflected.

        print_meta_info<^^int>();  // reflect a built-in type
        print_meta_info<^^PodStruct>();  // reflect a struct/class type
        print_meta_info<^^PodStruct::operator+>(); // reflect a member function
        print_meta_info<^^MyNameSpace::Woops>(); // reflect a namespace
        print_meta_info<^^banner>(); // reflect a variable

        // An expression is not an entity, so it cannot be reflected:
        // print_meta_info<^^(1 + 1)>();
    }

    {
        // Splice operator

        constexpr auto type_info = ^^int;
        typename[:type_info:] x = 1;  // splice a built-in type
        test_entry::log()->trace("x = {}", x);

        constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^PodStruct, std::meta::access_context::unchecked()));
        constexpr auto first_member = members[0];
        PodStruct test = {.m_mem_int = 3, .m_mem_real = 2.0, .m_mem_str = "Hello"};
        test_entry::log()->trace("test.m_mem_int = {}", test.[:first_member:]); // splice a member of a struct/class

    }


    // ------------------------------------------------------------
    // int  ------- ^^ -------> info{int} ----------[: :] ------> int
    // PodStruct ------- ^^ -------> info{PodStruct} ----------[: :] ------> PodStruct
    // PodStruct::operator+ ------- ^^ -------> info{PodStruct::operator+} ----------[: :] ------> .operator+

    {
        // Meta compare
        constexpr bool ii_same = ^^int == ^^int;
        test_entry::log()->trace("^^int == ^^int = {}", ii_same);

        constexpr bool id_same = ^^int == ^^double;
        test_entry::log()->trace("^^int == ^^double = {}", id_same);
    }

    {
        // template for: the loop body is instantiated once per reflected member.
        template for (constexpr std::meta::info mem : member_static_array(PodStruct{})) {
            using MemberType = typename[:std::meta::type_of(mem):];

            test_entry::log()->trace("Template for loop for memberType = {}", typeid(MemberType).name());
        }

        std::vector<int> v = {1, 2, 3};
        for (const int& i : v) {
            using MemberType = decltype(i);
            test_entry::log()->trace("For loop for memberType = {}", typeid(MemberType).name());
        }
    }
}