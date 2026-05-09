#include "test_entry.h"
#include <experimental/meta>
#include <print>
#include <string>

struct PodStruct {
    int m_mem_int;
    double m_mem_real;
    std::string m_mem_str;
};

template <typename T>
void print_each_field(const T& _object)
{
    constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info mem : members) {
        std::println("  {}:{}", std::meta::identifier_of(mem), _object.[:mem:]);
    }
}

void test_entry::hello_reflection()
{
    PodStruct object{.m_mem_int = 1, .m_mem_real = 2.0, .m_mem_str = "Hello, Reflection!"};
    print_each_field(object);
}