#include "test_entry.h"
#include <meta>
#include <print>
#include <string>

namespace MyNameSpace {

    struct MyStructBase {};

    struct MyStruct : public MyStructBase {
        public:
            int m_pub_i = 1;
            static int s_pub_i;
        protected:
            double m_pri_d = 2.0;
        private:
            std::string m_pro_s = "Hello";
    };

    int MyStruct::s_pub_i = 3;
}

enum class MyEnum{
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    Sunday
};

union MyUnion {
    int m_i;
    void* m_p;
};

#define MEMBER_LIST(QUERY_NAME, OBJECT) \
    std::println("  {}:", #QUERY_NAME); \
    template for (constexpr std::meta::info mem : std::define_static_array( \
        std::meta::QUERY_NAME(std::meta::type_of(^^OBJECT), std::meta::access_context::unchecked()))) { \
        if constexpr (std::meta::is_static_member(mem) || std::meta::is_nonstatic_data_member(mem)) { \
            std::println("    {}:{}", std::meta::display_string_of(mem), OBJECT.[:mem:]); \
        } else { \
            std::println("    {}", std::meta::display_string_of(mem)); \
        } \
    }; \
    std::println()

void test_entry::list_of_meta_functions()
{
    std::println("[List of Meta Functions] ----------- START -----------");

    {
        // list of name queries
        std::println("--------- List of name queries ---------");
        std::println("  identifier_of(MyStruct) -> std::string_view = {}", std::meta::identifier_of(^^MyNameSpace::MyStruct).data());
        std::println("  display_string_of(MyStruct) -> std::string_view = {}", std::meta::display_string_of(^^MyNameSpace::MyStruct));
    }

    {
        // list of type queries
        std::println("--------- List of type queries ---------");
        MyNameSpace::MyStruct my_struct;
        std::println("  type_of(my_struct) -> std::meta::info = {}", std::meta::display_string_of(std::meta::type_of(^^my_struct)));
        std::println("  parent_of(MyStruct) -> std::meta::info = {}", std::meta::display_string_of(std::meta::parent_of(^^MyNameSpace::MyStruct)));
        std::println("  is_type(MyStruct) -> bool = {}", std::meta::is_type(^^MyNameSpace::MyStruct));
        std::println("  is_namespace(MyStruct) -> bool = {}", std::meta::is_namespace(^^MyNameSpace::MyStruct));
    }

    {
        // list of member queries
        std::println("--------- List of member queries ---------");
        MyNameSpace::MyStruct my_struct;
        MyNameSpace::MyStruct::s_pub_i = 3;

        // Member list
        MEMBER_LIST(members_of, my_struct);
        MEMBER_LIST(nonstatic_data_members_of, my_struct);
        MEMBER_LIST(static_data_members_of, my_struct);
        MEMBER_LIST(bases_of, my_struct);

        // Enumerator list
        std::println("  enumerators_of:");
        template for (constexpr std::meta::info e : std::define_static_array(
            std::meta::enumerators_of(^^MyEnum))) {
                constexpr MyEnum value = std::meta::extract<MyEnum>(std::meta::constant_of(e));
            std::println("    {}:{}", std::meta::display_string_of(e), (int)value);
        };
        std::println();
    }

    {
        // List of classification queries
        MyNameSpace::MyStruct my_struct;

        std::println("  is_class_type(MyStruct) -> bool = {}", std::meta::is_class_type(^^MyNameSpace::MyStruct));
        std::println("  is_enum_type(MyEnum) -> bool = {}", std::meta::is_enum_type(^^MyEnum));
        //std::println("  is_enum(MyEnum) -> bool = {}", std::meta::is_enum(^^MyEnum));
        //std::println("  is_union(MyUnion) -> bool = {}", std::meta::is_union(^^MyUnion));
        //std::println("  is_nonstatic_data_member(MyStruct::m_pub_i) -> bool = {}", std::meta::is_nonstatic_data_member(^^MyNameSpace::MyStruct::m_pub_i));
        //std::println("  is_static_member(MyStruct::s_pub_i) -> bool = {}", std::meta::is_static_member(^^MyNameSpace::MyStruct::s_pub_i));
    }

    std::println("[List of Meta Functions] ----------- END -----------");
    std::println();
}
