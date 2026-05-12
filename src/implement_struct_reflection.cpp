#include "test_entry.h"
#include <meta>
#include <print>
#include <string>
#include <yaml-cpp/yaml.h>

struct MyStruct {
    int m_pub_i;
    double m_pub_d;
    std::string m_pub_s;
};

template <typename T>
consteval auto impl_reflection_member_count()
{
    return std::size(std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())));
}

template <typename T, size_t N>
consteval auto impl_reflection_member()
{
    static_assert(N < impl_reflection_member_count<T>(), "N is out of range");
    constexpr auto member = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))[N];
    return std::meta::identifier_of(member);
}

template <typename T>
consteval auto impl_reflection_has_member(const char* _str)
{
    constexpr auto fileds = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info field : fileds) {
        if (std::meta::identifier_of(field) == _str) {
            return true;
        }
    }
    return false;
}

template <typename T>
void impl_reflection_dump(const T& _object, std::string* _cache)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info field : fields) {
        const std::string key(std::meta::identifier_of(field));
        out << YAML::Key << key << YAML::Value << _object.[:field:];
    }
    out << YAML::EndMap;

    if (_cache) {
        *_cache = out.c_str();
    } else {
        std::println("{}", out.c_str());
    }
}

template <typename T>
void impl_reflection_load(T& _object, const std::string& _cache)
{
    YAML::Node node = YAML::Load(_cache.c_str());
    constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info field : fields) {
        const std::string key(std::meta::identifier_of(field));
        _object.[:field:] = node[key.c_str()].as<decltype(_object.[:field:])>();
    }
}

template <typename T>
void impl_reflection_serialize(const T& _object, std::string* _cache)
{
    std::string schar;
    constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info field : fields) {
        using field_type = decltype(_object.[:field:]);
        if constexpr(std::is_same_v<field_type, std::string>) {
            int len = _object.[:field:].size();
            schar.append((const char*)&len, sizeof(len));
            schar.append(_object.[:field:].c_str(), _object.[:field:].size());
        } else {
            schar.append((const char*)&_object.[:field:], sizeof(field_type));
        }
    }

    if (_cache) {
        *_cache = schar;
    } else {
        std::println("{}", schar.c_str());
    }
}

template <typename T>
void impl_reflection_deserialize(T& _object, const std::string& _cache)
{
    std::string schar = _cache;
    constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    size_t offset = 0;
    template for (constexpr std::meta::info field : fields) {
        using field_type = decltype(_object.[:field:]);
        if constexpr(std::is_same_v<field_type, std::string>) {
            int len = 0;
            std::memcpy(&len, schar.data() + offset, sizeof(len));
            offset += sizeof(len);
            _object.[:field:] = std::string(schar.data() + offset, len);
            offset += len;
        } else {
            std::memcpy(&_object.[:field:], schar.data() + offset, sizeof(field_type));
            offset += sizeof(field_type);
        }
    }
}

void test_entry::implement_struct_reflection()
{
    std::println("[Implement Struct Reflection] ----------- START -----------");

    {
        // Extended useage
        std::println("impl_reflection_member_count<MyStruct>() -> {}", impl_reflection_member_count<MyStruct>());
        std::println("impl_reflection_member<MyStruct, 0>() -> {}", impl_reflection_member<MyStruct, 0>());
        std::println("impl_reflection_member<MyStruct, 1>() -> {}", impl_reflection_member<MyStruct, 1>());
        std::println("impl_reflection_member<MyStruct, 2>() -> {}", impl_reflection_member<MyStruct, 2>());
        // Test if consteval is working.
        // static_assert(impl_reflection_has_member<MyStruct>("xxx"), "xxx is not a member of MyStruct");
        std::println("impl_reflection_has_member<MyStruct>(\"m_pub_i\") -> {}", impl_reflection_has_member<MyStruct>("m_pub_i"));
    }

    {
        // Dump and load to yaml string
        MyStruct dump_struct;
        dump_struct.m_pub_i = 1;
        dump_struct.m_pub_d = 2.0;
        dump_struct.m_pub_s = "Hello";
        std::string cache;
        impl_reflection_dump(dump_struct, &cache);

        std::println("{}", cache);

        MyStruct load_struct;
        impl_reflection_load(load_struct, cache);

        constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^MyStruct, std::meta::access_context::unchecked()));
        template for (constexpr std::meta::info field : fields) {
            std::println("{}: dump: {}, load: {}", std::meta::display_string_of(field), dump_struct.[:field:], load_struct.[:field:]);
        }
    }

    {
        // Serialize and deserialize to character stream.
        MyStruct serialize_struct;
        serialize_struct.m_pub_i = 1;
        serialize_struct.m_pub_d = 2.0;
        serialize_struct.m_pub_s = "Hello";
        
        std::string cache;
        impl_reflection_serialize(serialize_struct, &cache);
        std::println("{}", cache);

        MyStruct deserialize_struct;
        impl_reflection_deserialize(deserialize_struct, cache);

        constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^MyStruct, std::meta::access_context::unchecked()));
        template for (constexpr std::meta::info field : fields) {
            std::println("{}: dump: {}, load: {}", std::meta::display_string_of(field), serialize_struct.[:field:], deserialize_struct.[:field:]);
        }
    }
    
    std::println("[Implement Struct Reflection] ----------- END -----------");
    
}