#include "test_entry.h"
#include "helpers.h"
#include <meta>
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

template <typename T, typename R = double>
class MyClassTemplate {
    public:
        T m_t;
};

#define MEMBER_LIST(QUERY_NAME, OBJECT) \
    test_entry::log()->trace("  {}:", #QUERY_NAME); \
    template for (constexpr std::meta::info mem : std::define_static_array( \
        std::meta::QUERY_NAME(std::meta::type_of(^^OBJECT), std::meta::access_context::unchecked()))) { \
        if constexpr (std::meta::is_static_member(mem) || std::meta::is_nonstatic_data_member(mem)) { \
            test_entry::log()->trace("    {}: {}", std::meta::display_string_of(mem), OBJECT.[:mem:]); \
        } else { \
            test_entry::log()->trace("    {}", std::meta::display_string_of(mem)); \
        } \
    }; \
    test_entry::log()->trace("")

void test_entry::list_of_meta_functions()
{
    test_entry::section banner{"List of Meta Functions"};

    {
        // list of name queries
        test_entry::log()->debug("--- List of name queries ---");
        test_entry::log()->trace("  identifier_of(MyStruct) -> std::string_view = {}", std::meta::identifier_of(^^MyNameSpace::MyStruct).data());
        test_entry::log()->trace("  display_string_of(MyStruct) -> std::string_view = {}", std::meta::display_string_of(^^MyNameSpace::MyStruct));
    }

    {
        // list of type queries
        test_entry::log()->debug("--- List of type queries ---");
        MyNameSpace::MyStruct my_struct;
        test_entry::log()->trace("  type_of(my_struct) -> std::meta::info = {}", std::meta::display_string_of(std::meta::type_of(^^my_struct)));
        test_entry::log()->trace("  parent_of(MyStruct) -> std::meta::info = {}", std::meta::display_string_of(std::meta::parent_of(^^MyNameSpace::MyStruct)));
        test_entry::log()->trace("  is_type(MyStruct) -> bool = {}", std::meta::is_type(^^MyNameSpace::MyStruct));
        test_entry::log()->trace("  is_namespace(MyStruct) -> bool = {}", std::meta::is_namespace(^^MyNameSpace::MyStruct));
    }

    {
        // list of member queries
        test_entry::log()->debug("--- List of member queries ---");
        MyNameSpace::MyStruct my_struct;
        MyNameSpace::MyStruct::s_pub_i = 3;

        // Member list
        MEMBER_LIST(members_of, my_struct);
        MEMBER_LIST(nonstatic_data_members_of, my_struct);
        MEMBER_LIST(static_data_members_of, my_struct);
        MEMBER_LIST(bases_of, my_struct);

        // Enumerator list
        test_entry::log()->trace("  enumerators_of:");
        template for (constexpr std::meta::info e : std::define_static_array(
            std::meta::enumerators_of(^^MyEnum))) {
                constexpr MyEnum value = std::meta::extract<MyEnum>(std::meta::constant_of(e));
            test_entry::log()->trace("    {}: {}", std::meta::display_string_of(e), (int)value);
        };
        test_entry::log()->trace("");
    }

    {
        // List of classification queries
        test_entry::log()->debug("--- List of classification queries ---");
        MyNameSpace::MyStruct my_struct;

        test_entry::log()->trace("  is_class_type(MyStruct) -> bool = {}", std::meta::is_class_type(^^MyNameSpace::MyStruct));
        test_entry::log()->trace("  is_enum_type(MyEnum) -> bool = {}", std::meta::is_enum_type(^^MyEnum));
        test_entry::log()->trace("  is_union_type(MyUnion) -> bool = {}", std::meta::is_union_type(^^MyUnion));
        test_entry::log()->trace("  is_nonstatic_data_member(MyStruct::m_pub_i) -> bool = {}", std::meta::is_nonstatic_data_member(^^MyNameSpace::MyStruct::m_pub_i));
        test_entry::log()->trace("  is_static_member(MyStruct::s_pub_i) -> bool = {}", std::meta::is_static_member(^^MyNameSpace::MyStruct::s_pub_i));
        test_entry::log()->trace("  is_class_template(MyClassTemplate<int>) -> bool = {}", std::meta::is_class_template(^^MyClassTemplate<int>));
        test_entry::log()->trace("  is_class_template(MyClassTemplate) -> bool = {}", std::meta::is_class_template(^^MyClassTemplate));
        test_entry::log()->trace("  is_template(MyClassTemplate<int>) -> bool = {}", std::meta::is_template(^^MyClassTemplate<int>));
        test_entry::log()->trace("  is_template(MyClassTemplate) -> bool = {}", std::meta::is_template(^^MyClassTemplate));

        template for (constexpr std::meta::info mem : std::define_static_array(
            std::meta::template_arguments_of(^^MyClassTemplate<int>))
        )
        {
            test_entry::log()->trace("    template argument: {}", std::meta::display_string_of(mem));
        }

        // ... TODO: test other queries that you are intersted in.
    }

    {
        // Inheritance queries
        template for (constexpr std::meta::info mem : std::define_static_array(
            std::meta::bases_of(^^MyNameSpace::MyStruct, std::meta::access_context::unchecked()))
        )
        {
            test_entry::log()->trace("    base class: {}", std::meta::display_string_of(mem));
        }
    }
}
