#include "test_entry.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <meta>
#include <optional>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// --serve() is a Unix socket server, so this file is POSIX-only.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// JSON Schema generation from C++26 static reflection, for feeding a Python-side
// LLM (OpenAI-compatible tool/function calling). Structs and functions:
//   struct -> {"type":"object", ...}
//   function -> its parameter object schema, optionally wrapped as an OpenAI tool.
//
// The approach follows the mcpp reference (annotation-driven metadata, consteval
// schema assembly, define_static_string materialization), with one deliberate
// difference: `bool` is classified before the integral types.

namespace meta = std::meta;

namespace js {

// --- annotation value types -------------------------------------------------
// An annotation value must be structural, so a string cannot be a string_view.
// str<N> holds exactly N characters, including the NUL, e.g. str<6>{"hello"}.
template <std::size_t N>
struct str {
    char data[N]{};
    constexpr str() = default;
    constexpr str(const char (&s)[N])
    {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] = s[i];
        }
    }
};
template <std::size_t N>
str(const char (&)[N]) -> str<N>;

// Function-level documentation, applied as [[= js::doc{ .text = js::str("...") } ]].
template <std::size_t N = 1>
struct doc {
    str<N> text;
};

// Field-level description for a struct member, applied directly on the member as
// [[= js::desc{ .text = js::str("...") } ]]. The same type is reused for the
// positional parameter list below.
template <std::size_t N = 1>
struct desc {
    str<N> text;
};

// A structural, variadic list of descriptions. std::tuple is not a structural
// type in this implementation, so we roll our own aggregate.
template <typename... Ts>
struct doc_list;
template <>
struct doc_list<> {};
template <typename T, typename... Ts>
struct doc_list<T, Ts...> {
    T head;
    doc_list<Ts...> tail;
};

/// Function-parameter descriptions, matched to parameters by position:
///   [[= js::param_docs(js::str("first"), js::str("second")) ]]
/// (An annotation on the parameter itself is not readable: annotations_of() on a
/// parameter reflection fails, so the metadata lives on the function instead.)
consteval auto param_docs(auto... _xs)
{
    return doc_list<decltype(_xs)...>{_xs...};
}

// --- type traits ------------------------------------------------------------

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
struct is_sequence : std::false_type {};
template <typename T, typename A>
struct is_sequence<std::vector<T, A>> : std::true_type {
    using value_type = T;
};
template <typename T, std::size_t N>
struct is_sequence<std::array<T, N>> : std::true_type {
    using value_type = T;
};

template <typename T>
inline constexpr bool is_string_like_v =
    std::is_same_v<std::remove_cv_t<T>, std::string> ||
    std::is_same_v<std::remove_cv_t<T>, std::string_view>;

// --- JSON helpers -----------------------------------------------------------

constexpr std::string json_escape(std::string_view _s)
{
    std::string out;
    for (char c : _s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

/// JSON type keyword for a reflected type.
consteval const char* json_type_name(meta::info _t)
{
    _t = meta::dealias(_t);
    // bool must be tested before is_integral_type, which is also true for bool.
    if (meta::is_same_type(_t, ^^bool)) {
        return "boolean";
    }
    if (meta::is_integral_type(_t)) {
        return "integer";
    }
    if (meta::is_floating_point_type(_t)) {
        return "number";
    }
    if (meta::is_same_type(_t, ^^std::string) || meta::is_same_type(_t, ^^std::string_view)) {
        return "string";
    }
    return "string";
}

// --- description annotations ------------------------------------------------

/// First annotation on `_e` whose type is an instantiation of the template `Tmpl`,
/// or an empty reflection when there is none.
template <meta::info Tmpl>
consteval meta::info find_annotation(meta::info _e)
{
    for (meta::info a : meta::annotations_of(_e)) {
        const meta::info t = meta::remove_const(meta::type_of(a));
        if (meta::has_template_arguments(t) && meta::template_of(t) == Tmpl) {
            return a;
        }
    }
    return {};
}

/// Text held by the first `Tmpl` annotation on `E`, or "" when there is none.
template <meta::info Tmpl, meta::info E>
consteval std::string text_of()
{
    if constexpr (find_annotation<Tmpl>(E) == meta::info{}) {
        return {};
    } else {
        constexpr auto cfg =
            meta::extract<typename [: meta::type_of(find_annotation<Tmpl>(E)) :]>(find_annotation<Tmpl>(E));
        return std::string(std::string_view(cfg.text.data));
    }
}

template <meta::info Fn>
consteval std::size_t param_doc_count()
{
    if (find_annotation<^^doc_list>(Fn) == meta::info{}) {
        return 0;
    }
    return meta::template_arguments_of(meta::remove_const(meta::type_of(find_annotation<^^doc_list>(Fn))))
        .size();
}

template <std::size_t I, typename List>
consteval auto nth_doc(List _list)
{
    if constexpr (I == 0) {
        return _list.head;
    } else {
        return nth_doc<I - 1>(_list.tail);
    }
}

/// Positional description for parameter `I` of `Fn`, or "" if undocumented.
template <meta::info Fn, std::size_t I>
consteval std::string param_description()
{
    if constexpr (find_annotation<^^doc_list>(Fn) == meta::info{}) {
        return {};
    } else if constexpr (I < param_doc_count<Fn>()) {
        constexpr auto list =
            meta::extract<typename [: meta::type_of(find_annotation<^^doc_list>(Fn)) :]>(
                find_annotation<^^doc_list>(Fn));
        return std::string(std::string_view(nth_doc<I>(list).data));
    } else {
        return {};
    }
}

/// Splice a description into a generated schema, which is always one JSON object.
consteval std::string with_description(std::string _schema, std::string_view _desc)
{
    if (_desc.empty()) {
        return _schema;
    }
    return _schema.substr(0, _schema.size() - 1) + ",\"description\":\"" + json_escape(_desc) + "\"}";
}

// --- schema generation ------------------------------------------------------

template <typename T>
consteval std::string param_schema();

template <typename T>
consteval std::size_t member_count()
{
    return meta::nonstatic_data_members_of(^^T, meta::access_context::unchecked()).size();
}

template <typename T>
consteval std::array<meta::info, member_count<T>()> member_infos()
{
    const auto members = meta::nonstatic_data_members_of(^^T, meta::access_context::unchecked());
    std::array<meta::info, member_count<T>()> out{};
    for (std::size_t i = 0; i < members.size(); ++i) {
        out[i] = members[i];
    }
    return out;
}

template <typename T, std::size_t... Is>
consteval std::string object_schema_impl(std::index_sequence<Is...>)
{
    constexpr auto infos = member_infos<T>();
    std::string properties;
    std::string required;
    ((void)([&] {
        using MT = std::remove_cvref_t<typename [: meta::type_of(infos[Is]) :]>;
        const std::string name(meta::identifier_of(infos[Is]));
        if (!properties.empty()) {
            properties += ",";
        }
        properties += "\"" + name + "\":" +
                      with_description(param_schema<MT>(), text_of<^^js::desc, infos[Is]>());
        if constexpr (!is_optional<MT>::value) {
            if (!required.empty()) {
                required += ",";
            }
            required += "\"" + name + "\"";
        }
    }()), ...);
    return "{\"type\":\"object\",\"properties\":{" + properties + "},\"required\":[" + required + "]}";
}

/// JSON Schema for one C++ type. std::optional unwraps to its value type,
/// vectors/arrays become arrays, enums become string enums, and other classes
/// become nested objects.
template <typename T>
consteval std::string param_schema()
{
    if constexpr (is_optional<T>::value) {
        return param_schema<typename T::value_type>();
    } else if constexpr (is_sequence<T>::value) {
        return "{\"type\":\"array\",\"items\":" + param_schema<typename T::value_type>() + "}";
    } else if constexpr (std::is_enum_v<T>) {
        std::string values;
        for (meta::info e : meta::enumerators_of(^^T)) {
            if (!values.empty()) {
                values += ",";
            }
            values += "\"" + std::string(meta::identifier_of(e)) + "\"";
        }
        return "{\"type\":\"string\",\"enum\":[" + values + "]}";
    } else if constexpr (std::is_class_v<T> && !is_string_like_v<T>) {
        return object_schema_impl<T>(std::make_index_sequence<member_count<T>()>());
    } else {
        return std::string("{\"type\":\"") + json_type_name(^^T) + "\"}";
    }
}

// --- function introspection -------------------------------------------------
// Anything touching meta::info happens at consteval; the results handed to the
// runtime string assembly are plain values.

template <meta::info Fn>
consteval std::size_t param_count()
{
    return meta::parameters_of(Fn).size();
}

template <meta::info Fn, std::size_t I>
consteval meta::info param_at()
{
    return meta::parameters_of(Fn)[I];
}

template <meta::info Fn, std::size_t I>
using param_type_at = std::remove_cvref_t<typename [: meta::type_of(param_at<Fn, I>()) :]>;

template <meta::info Fn, std::size_t... Is>
consteval std::string param_object_schema_impl(std::index_sequence<Is...>)
{
    std::string properties;
    std::string required;
    ((void)([&] {
        using PT = param_type_at<Fn, Is>;
        const std::string name(meta::identifier_of(param_at<Fn, Is>()));
        if (!properties.empty()) {
            properties += ",";
        }
        properties += "\"" + name + "\":" +
                      with_description(param_schema<PT>(), param_description<Fn, Is>());
        if constexpr (!is_optional<PT>::value) {
            if (!required.empty()) {
                required += ",";
            }
            required += "\"" + name + "\"";
        }
    }()), ...);
    return "{\"type\":\"object\",\"properties\":{" + properties + "},\"required\":[" + required + "]}";
}

template <meta::info Fn>
consteval std::string param_object_schema()
{
    return param_object_schema_impl<Fn>(std::make_index_sequence<param_count<Fn>()>());
}

// --- documentation annotation ----------------------------------------------

/// Function-level description from [[= js::doc{ ... } ]].
template <meta::info Fn>
consteval std::string description_of()
{
    return text_of<^^js::doc, Fn>();
}

// --- public entry points ----------------------------------------------------

enum class Style {
    JsonSchema,  ///< the raw JSON Schema of the parameters/fields
    OpenAiTool,  ///< {"type":"function","function":{name, description, parameters}}
};

/// Wraps a parameter-object schema as an OpenAI tool. Shared by function_schema
/// and tool_schema so both emit the wrapper identically.
consteval std::string_view as_openai_tool(std::string_view _name, std::string_view _description,
                                         std::string _parameters)
{
    std::string out = "{\"type\":\"function\",\"function\":{\"name\":\"";
    out += _name;
    out += "\",\"description\":\"" + json_escape(_description) + "\",\"parameters\":";
    out += _parameters;
    out += "}}";
    return std::string_view(std::define_static_string(out));
}

/// Schema for a function's parameters. With Style::OpenAiTool this is a complete
/// tool the caller can hand straight to an API; with Style::JsonSchema it is the
/// bare parameter object. The wrapper is built here, so callers never assemble
/// one. The description comes from an optional [[= js::doc{...}]] on the function.
template <meta::info Fn, Style S = Style::JsonSchema>
consteval std::string_view function_schema()
{
    if constexpr (S == Style::OpenAiTool) {
        return as_openai_tool(meta::identifier_of(Fn), description_of<Fn>(),
                              param_object_schema<Fn>());
    } else {
        return std::string_view(std::define_static_string(param_object_schema<Fn>()));
    }
}

/// Schema for a struct's fields, mirroring function_schema. The tool name is the
/// type's identifier, and the description comes from an optional
/// [[= js::doc{...}]] on the type.
template <typename T, Style S = Style::JsonSchema>
consteval std::string_view tool_schema()
{
    if constexpr (S == Style::OpenAiTool) {
        return as_openai_tool(meta::identifier_of(^^T), description_of<^^T>(),
                              object_schema_impl<T>(std::make_index_sequence<member_count<T>()>()));
    } else {
        return std::string_view(std::define_static_string(
            object_schema_impl<T>(std::make_index_sequence<member_count<T>()>())));
    }
}

// --- reverse direction: JSON value -> C++ value ------------------------------
// The schema is a one-way projection for the LLM, so this does not parse the
// schema back. It re-reflects the same type/function instead: names and types
// are known at compile time, only the values come from the JSON document.
// Deserialization is driven by the reflection, so any reflected struct works.

template <typename T>
consteval std::size_t enumerator_count()
{
    return meta::enumerators_of(^^T).size();
}

template <typename T>
consteval std::array<meta::info, enumerator_count<T>()> enumerator_infos()
{
    const auto es = meta::enumerators_of(^^T);
    std::array<meta::info, enumerator_count<T>()> out{};
    for (std::size_t i = 0; i < es.size(); ++i) {
        out[i] = es[i];
    }
    return out;
}

/// {name, value} table for an enum, materialized as ordinary constants so it can
/// be indexed at runtime -- meta::extract() itself needs a constant expression.
template <typename T>
struct enumerator_table {
    static constexpr std::size_t size = enumerator_count<T>();
    std::array<std::string_view, size> names;
    std::array<T, size> values;
};

template <typename T, std::size_t... Is>
consteval enumerator_table<T> make_enumerator_table_impl(std::index_sequence<Is...>)
{
    constexpr auto infos = enumerator_infos<T>();
    return enumerator_table<T>{
        {std::string_view(std::define_static_string(meta::identifier_of(infos[Is])))...},
        {meta::extract<T>(infos[Is])...}};
}

template <typename T>
consteval enumerator_table<T> make_enumerator_table()
{
    return make_enumerator_table_impl<T>(std::make_index_sequence<enumerator_count<T>()>());
}

template <typename T>
T enum_from_string(const std::string& _name)
{
    constexpr auto table = make_enumerator_table<T>();
    for (std::size_t i = 0; i < table.size; ++i) {
        if (table.names[i] == _name) {
            return table.values[i];
        }
    }
    throw std::runtime_error("invalid " + std::string(meta::identifier_of(^^T)) + " value: " + _name);
}

/// Enum -> its reflected enumerator name, the inverse of enum_from_string().
template <typename T>
std::string enum_to_string(T _value)
{
    constexpr auto table = make_enumerator_table<T>();
    for (std::size_t i = 0; i < table.size; ++i) {
        if (table.values[i] == _value) {
            return std::string(table.names[i]);
        }
    }
    return "?";
}

template <typename T>
T from_json(const nlohmann::json& _j);

/// The member reflection for index `I`, as a constant expression. Reflections
/// never live in a runtime variable -- std::meta::info is a consteval-only type,
/// so this is used directly as a template argument.
template <typename T, std::size_t I>
consteval meta::info member_at()
{
    return member_infos<T>()[I];
}

template <meta::info M>
void assign_member(auto& _out, const nlohmann::json& _j)
{
    using MT = std::remove_cvref_t<typename [: meta::type_of(M) :]>;
    constexpr std::string_view name = std::define_static_string(meta::identifier_of(M));
    const std::string key(name);
    if (_j.contains(key)) {
        _out.[: M :] = from_json<MT>(_j.at(key));
    }
}

/// Members absent from the document keep the value from default construction,
/// so declared defaults and std::nullopt survive a partial document.
template <typename T, std::size_t... Is>
T object_from_json(const nlohmann::json& _j, std::index_sequence<Is...>)
{
    T out{};
    (assign_member<member_at<T, Is>()>(out, _j), ...);
    return out;
}

/// JSON value -> C++ value, mirroring param_schema()'s type dispatch.
template <typename T>
T from_json(const nlohmann::json& _j)
{
    if constexpr (is_optional<T>::value) {
        if (_j.is_null()) {
            return std::nullopt;
        }
        return from_json<typename T::value_type>(_j);
    } else if constexpr (is_sequence<T>::value) {
        using V = typename T::value_type;
        if constexpr (requires(T t) { t.push_back(V{}); }) {
            T out{};
            out.reserve(_j.size());
            for (const auto& element : _j) {
                out.push_back(from_json<V>(element));
            }
            return out;
        } else {
            T out{};
            const std::size_t n = _j.size() < out.size() ? _j.size() : out.size();
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = from_json<V>(_j.at(i));
            }
            return out;
        }
    } else if constexpr (std::is_enum_v<T>) {
        return enum_from_string<T>(_j.get<std::string>());
    } else if constexpr (std::is_same_v<T, bool>) {
        return _j.get<bool>();
    } else if constexpr (std::is_integral_v<T>) {
        return _j.get<T>();
    } else if constexpr (std::is_floating_point_v<T>) {
        return _j.get<T>();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return _j.get<std::string>();
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        // Borrows from the document, which must outlive the result.
        return std::string_view(_j.get_ref<const std::string&>());
    } else if constexpr (std::is_class_v<T>) {
        return object_from_json<T>(_j, std::make_index_sequence<member_count<T>()>());
    } else {
        static_assert(!sizeof(T), "js::from_json: unsupported type");
    }
}

/// Builds the argument tuple from the JSON object and actually calls the
/// function. This is the LLM tool-call path: the model's `arguments` string in,
/// a real C++ call out. Missing keys fall back to default-constructed arguments.
template <meta::info Fn, std::size_t I>
void assign_argument(auto& _args, const nlohmann::json& _j)
{
    using PT = param_type_at<Fn, I>;
    constexpr std::string_view name =
        std::define_static_string(meta::identifier_of(param_at<Fn, I>()));
    const std::string key(name);
    if (_j.contains(key)) {
        std::get<I>(_args) = from_json<PT>(_j.at(key));
    }
}

template <meta::info Fn, std::size_t... Is>
auto invoke_with_json_impl(const nlohmann::json& _j, std::index_sequence<Is...>)
{
    std::tuple<param_type_at<Fn, Is>...> args{};
    (assign_argument<Fn, Is>(args, _j), ...);
    return std::apply(&[: Fn :], args);
}

template <meta::info Fn>
auto invoke_with_json(const nlohmann::json& _j)
{
    return invoke_with_json_impl<Fn>(_j, std::make_index_sequence<param_count<Fn>()>());
}

// --- serialization: C++ value -> JSON ----------------------------------------
// The inverse of from_json(), used to dump a reconstructed struct generically so
// no per-struct print code is needed.

template <typename T>
std::string to_json(const T& _value);

template <meta::info M, typename T>
void append_member_json(std::string& _out, const T& _value)
{
    constexpr std::string_view name = std::define_static_string(meta::identifier_of(M));
    _out += "\"";
    _out += name;
    _out += "\":";
    _out += to_json(_value.[: M :]);
}

template <typename T, std::size_t... Is>
std::string object_to_json(const T& _value, std::index_sequence<Is...>)
{
    std::string out = "{";
    std::size_t n = 0;
    (([&] {
        if (n++) {
            out += ",";
        }
        append_member_json<member_at<T, Is>(), T>(out, _value);
    }()), ...);
    out += "}";
    return out;
}

template <typename T>
std::string to_json(const T& _value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return _value ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return "\"" + enum_to_string(_value) + "\"";
    } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
        return std::to_string(_value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return "\"" + json_escape(_value) + "\"";
    } else if constexpr (is_optional<T>::value) {
        return _value ? to_json(*_value) : "null";
    } else if constexpr (is_sequence<T>::value) {
        std::string out = "[";
        for (std::size_t i = 0; i < _value.size(); ++i) {
            if (i) {
                out += ",";
            }
            out += to_json(_value[i]);
        }
        return out + "]";
    } else if constexpr (std::is_class_v<T>) {
        return object_to_json(_value, std::make_index_sequence<member_count<T>()>());
    } else {
        return "null";
    }
}

// --- registry: self-registration, filled by the macros below -----------------
// A tool's schema is the OpenAI tool JSON; its handler returns std::string (JSON
// text), which the macro enforces because the handler is declared to do so.

struct tool_entry {
    std::string_view name;
    std::string_view schema;
    std::string (*run)(const nlohmann::json&);
};

inline std::vector<tool_entry>& tool_registry()
{
    static std::vector<tool_entry> entries;
    return entries;
}

struct model_entry {
    std::string_view name;
    std::string_view schema;
    std::string (*dump)(const nlohmann::json&);
};

inline std::vector<model_entry>& model_registry()
{
    static std::vector<model_entry> entries;
    return entries;
}

/// Names must be unique: an OpenAI tools array with a duplicate name is rejected
/// outright ("Tool names must be unique."), and duplicate model names would
/// silently collapse to one key in the emitted object. Both registries are
/// populated by static initializers, so this fires before main() -- a duplicate
/// is a hard startup failure, never a silent misroute.
inline void register_tool(tool_entry _entry)
{
    for (const auto& existing : tool_registry()) {
        if (existing.name == _entry.name) {
            std::println(std::cerr, "js: duplicate tool name '{}' (names must be unique)",
                         _entry.name);
            std::abort();
        }
    }
    tool_registry().push_back(std::move(_entry));
}

inline void register_model(model_entry _entry)
{
    for (const auto& existing : model_registry()) {
        if (existing.name == _entry.name) {
            std::println(std::cerr, "js: duplicate model name '{}' (names must be unique)",
                         _entry.name);
            std::abort();
        }
    }
    model_registry().push_back(std::move(_entry));
}

} // namespace js

// Colocated registration. Use inside the namespace of the entity, after its
// definition:
//   CPP_REFLECT_TOOL(search_documents)   // fn -> OpenAI tool, callable via --call
//   CPP_REFLECT_MODEL(SearchRequest)     // struct -> JSON Schema, parseable
// A function-local static backs the registry, so registration order across
// translation units is safe.

#define CPP_REFLECT_TOOL(fn)                                                                       \
    namespace {                                                                                    \
    std::string js_run_##fn(const nlohmann::json& _j)                                              \
    {                                                                                              \
        return ::js::invoke_with_json<^^fn>(_j);                                                   \
    }                                                                                              \
    [[maybe_unused]] const int js_reg_tool_##fn = [] {                                             \
        ::js::register_tool(::js::tool_entry{                                                      \
            #fn, ::js::function_schema<^^fn, ::js::Style::OpenAiTool>(), &js_run_##fn});           \
        return 0;                                                                                  \
    }();                                                                                           \
    }

#define CPP_REFLECT_MODEL(Type)                                                                    \
    namespace {                                                                                    \
    std::string js_dump_##Type(const nlohmann::json& _j)                                           \
    {                                                                                              \
        return ::js::to_json(::js::from_json<Type>(_j));                                           \
    }                                                                                              \
    [[maybe_unused]] const int js_reg_model_##Type = [] {                                          \
        ::js::register_model(                                                                      \
            ::js::model_entry{#Type, ::js::tool_schema<Type, ::js::Style::OpenAiTool>(), &js_dump_##Type});   \
        return 0;                                                                                  \
    }();                                                                                           \
    }

namespace demo {

enum class Unit {
    Celsius,
    Fahrenheit
};

struct [[= js::doc{ .text = js::str("A numeric range to match.") } ]] Range {
    [[= js::desc{ .text = js::str("Inclusive lower bound.") } ]] int min = 0;
    [[= js::desc{ .text = js::str("Optional inclusive upper bound.") } ]] std::optional<int> max;
};
CPP_REFLECT_MODEL(Range)

struct [[= js::doc{ .text = js::str("One search clause.") } ]] Query {
    [[= js::desc{ .text = js::str("The raw query text.") } ]] std::string text;
    [[= js::desc{ .text = js::str("Temperature unit for numeric filters.") } ]] Unit unit =
        Unit::Celsius;
    [[= js::desc{ .text = js::str("Numeric ranges to match.") } ]] std::vector<Range> ranges;
    [[= js::desc{ .text = js::str("Allow approximate matching.") } ]] bool fuzzy = false;
    [[= js::desc{ .text = js::str("Relative weight of this clause.") } ]] std::optional<double> weight;
};
CPP_REFLECT_MODEL(Query)

struct [[= js::doc{ .text = js::str("A structured search request.") } ]] SearchRequest {
    Query query;
    [[= js::desc{ .text = js::str("Number of hits requested per shard.") } ]] std::array<int, 3> top_k{};
    bool debug = false;
};
CPP_REFLECT_MODEL(SearchRequest)

[[= js::doc{ .text = js::str("Searches the document index.") } ]]
[[= js::param_docs(js::str("The raw query text."),
                   js::str("Maximum number of hits to return."),
                   js::str("Require an exact match."),
                   js::str("Optional unit filter.")) ]]
std::string search_documents(std::string query, int limit, bool exact_match,
                             std::optional<Unit> unit = std::nullopt)
{
    std::string hits;
    for (int i = 0; i < limit && i < 3; ++i) {
        if (!hits.empty()) {
            hits += ",";
        }
        hits += "{\"rank\":" + std::to_string(i + 1) + ",\"title\":\"" + query + " #" +
                std::to_string(i + 1) + "\"}";
    }
    return "{\"query\":\"" + query + "\",\"limit\":" + std::to_string(limit) +
           ",\"exact_match\":" + (exact_match ? "true" : "false") + ",\"unit\":\"" +
           (unit ? (*unit == Unit::Celsius ? "Celsius" : "Fahrenheit") : "any") + "\",\"hits\":[" +
           hits + "]}";
}
CPP_REFLECT_TOOL(search_documents)

} // namespace demo

// --- validation with a real JSON parser -------------------------------------

namespace {

using json = nlohmann::json;

std::optional<json> parse_json(std::string_view _text)
{
    try {
        return json::parse(_text);
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

/// Recursively checks every object schema in `_doc`: nodes with
/// "type":"object" must carry "properties" and a "required" array, and every
/// required name must be declared in "properties".
bool check_object_shapes(const json& _doc, const std::string& _path)
{
    if (_doc.is_object()) {
        const bool is_object_schema = _doc.contains("type") && _doc.at("type").is_string() &&
                                      _doc.at("type").get<std::string>() == "object";
        if (is_object_schema) {
            if (!_doc.contains("properties") || !_doc.at("properties").is_object()) {
                std::println("  [FAIL] {}: \"properties\" is missing or not an object", _path);
                return false;
            }
            if (!_doc.contains("required") || !_doc.at("required").is_array()) {
                std::println("  [FAIL] {}: \"required\" is missing or not an array", _path);
                return false;
            }
            for (const auto& name : _doc.at("required")) {
                const std::string key = name.get<std::string>();
                if (!_doc.at("properties").contains(key)) {
                    std::println("  [FAIL] {}: \"required\" lists \"{}\", absent from \"properties\"",
                                 _path, key);
                    return false;
                }
            }
        }
        for (const auto& [key, value] : _doc.items()) {
            if (!check_object_shapes(value, _path + "/" + key)) {
                return false;
            }
        }
    } else if (_doc.is_array()) {
        for (std::size_t i = 0; i < _doc.size(); ++i) {
            if (!check_object_shapes(_doc.at(i), _path + "/" + std::to_string(i))) {
                return false;
            }
        }
    }
    return true;
}

bool check_schema(const char* _label, std::string_view _text)
{
    const auto doc = parse_json(_text);
    if (!doc) {
        std::println("  [FAIL] {}: not well-formed JSON", _label);
        return false;
    }
    if (!check_object_shapes(*doc, _label)) {
        return false;
    }
    std::println("  [ok]   {}: well-formed, nested object schemas consistent ({} bytes)",
                 _label, _text.size());
    return true;
}

/// The OpenAI tool wrapper: {"type":"function","function":{name,description,parameters}}.
bool check_tool_schema(const char* _label, std::string_view _text)
{
    const auto doc = parse_json(_text);
    if (!doc) {
        std::println("  [FAIL] {}: not well-formed JSON", _label);
        return false;
    }
    if (!doc->contains("type") || doc->at("type") != "function" ||
        !doc->contains("function") || !doc->at("function").is_object()) {
        std::println("  [FAIL] {}: missing the OpenAI tool wrapper", _label);
        return false;
    }
    const json& fn = doc->at("function");
    for (const char* key : {"name", "description", "parameters"}) {
        if (!fn.contains(key)) {
            std::println("  [FAIL] {}: \"function.{}\" is missing", _label, key);
            return false;
        }
    }
    if (!fn.at("name").is_string() || !fn.at("description").is_string()) {
        std::println("  [FAIL] {}: \"function.name\"/\"description\" are not strings", _label);
        return false;
    }
    if (!check_object_shapes(fn.at("parameters"), std::string(_label) + "/parameters")) {
        return false;
    }
    std::println("  [ok]   {}: valid tool, name=\"{}\", {} parameter(s)", _label,
                 fn.at("name").get<std::string>(), fn.at("parameters").at("properties").size());
    return true;
}

/// Counts every "description" key anywhere in the document.
std::size_t count_descriptions(const json& _doc)
{
    std::size_t n = 0;
    if (_doc.is_object()) {
        if (_doc.contains("description")) {
            ++n;
        }
        for (const auto& [key, value] : _doc.items()) {
            n += count_descriptions(value);
        }
    } else if (_doc.is_array()) {
        for (const auto& value : _doc) {
            n += count_descriptions(value);
        }
    }
    return n;
}

bool check_description(const json& _properties, const char* _name, const char* _expected)
{
    if (!_properties.contains(_name) || !_properties.at(_name).contains("description")) {
        std::println("  [FAIL] property \"{}\" carries no description", _name);
        return false;
    }
    const std::string got = _properties.at(_name).at("description").get<std::string>();
    if (got != _expected) {
        std::println("  [FAIL] property \"{}\" description = \"{}\", expected \"{}\"", _name, got,
                     _expected);
        return false;
    }
    std::println("  [ok]   property \"{}\" -> \"{}\"", _name, got);
    return true;
}

} // namespace

void test_entry::implement_json_schema()
{
    test_entry::section banner{"Implement JSON Schema"};

    constexpr std::string_view fn_schema = js::function_schema<^^demo::search_documents>();
    constexpr std::string_view tool_schema =
        js::function_schema<^^demo::search_documents, js::Style::OpenAiTool>();
    constexpr std::string_view query_schema = js::tool_schema<demo::Query>();
    constexpr std::string_view request_schema = js::tool_schema<demo::SearchRequest>();

    std::println("--- Function -> JSON Schema ---");
    std::println("{}", fn_schema);

    std::println();
    std::println("--- Function -> OpenAI tool ---");
    std::println("{}", tool_schema);

    std::println();
    std::println("--- Struct -> JSON Schema (Query) ---");
    std::println("{}", query_schema);

    std::println();
    std::println("--- Struct -> JSON Schema (SearchRequest) ---");
    std::println("{}", request_schema);

    std::println();
    std::println("--- Validation with nlohmann/json {}.{}.{} ---",
                 NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
                 NLOHMANN_JSON_VERSION_PATCH);

    bool valid = true;
    valid &= check_schema("function schema", fn_schema);
    valid &= check_tool_schema("openai tool", tool_schema);
    valid &= check_schema("struct Query", query_schema);
    valid &= check_schema("struct SearchRequest", request_schema);
    std::println();
    std::println("--- Parameter / field descriptions ---");
    if (const auto tool = parse_json(tool_schema)) {
        const auto& props = tool->at("function").at("parameters").at("properties");
        valid &= check_description(props, "query", "The raw query text.");
        valid &= check_description(props, "limit", "Maximum number of hits to return.");
        valid &= check_description(props, "exact_match", "Require an exact match.");
        valid &= check_description(props, "unit", "Optional unit filter.");
    }
    if (const auto query = parse_json(query_schema)) {
        valid &= check_description(query->at("properties"), "text", "The raw query text.");
        valid &= check_description(query->at("properties").at("ranges").at("items").at("properties"),
                                   "max", "Optional inclusive upper bound.");
    }
    if (const auto request = parse_json(request_schema)) {
        valid &= check_description(request->at("properties").at("query").at("properties"), "unit",
                                   "Temperature unit for numeric filters.");
    }
    const auto count_of = [](std::string_view _text) {
        const auto doc = parse_json(_text);
        return doc ? count_descriptions(*doc) : 0;
    };
    std::println("  description keys: fn={}, tool={}, query={}, request={}", count_of(fn_schema),
                 count_of(tool_schema), count_of(query_schema), count_of(request_schema));
    std::println("  => {}", valid ? "all schemas valid" : "VALIDATION FAILED");

    std::println();
    std::println("--- JSON value -> C++ struct ---");
    const char* request_json = R"({
        "query": {
            "text": "static reflection",
            "unit": "Fahrenheit",
            "ranges": [{"min": 1, "max": 10}, {"min": 20}],
            "fuzzy": true,
            "weight": 0.75
        },
        "top_k": [3, 5, 8]
    })";
    if (const auto doc = parse_json(request_json)) {
        const demo::SearchRequest req = js::from_json<demo::SearchRequest>(*doc);
        std::println("  query.text        = {}", req.query.text);
        std::println("  query.unit        = {}",
                     req.query.unit == demo::Unit::Celsius ? "Celsius" : "Fahrenheit");
        std::println("  query.ranges      = {} entries", req.query.ranges.size());
        std::println("  ranges[0].min     = {}", req.query.ranges[0].min);
        std::println("  ranges[0].max     = {}",
                     req.query.ranges[0].max ? std::to_string(*req.query.ranges[0].max) : "null");
        std::println("  ranges[1].max     = {} (absent in JSON)",
                     req.query.ranges[1].max ? std::to_string(*req.query.ranges[1].max) : "null");
        std::println("  query.weight      = {}",
                     req.query.weight ? std::to_string(*req.query.weight) : "null");
        std::println("  top_k             = [{}, {}, {}]", req.top_k[0], req.top_k[1], req.top_k[2]);
        std::println("  debug             = {} (absent -> default)", req.debug);
    }

    std::println();
    std::println("--- JSON args -> actual function call ---");
    if (const auto doc = parse_json(R"({"query":"reflection in C++26","limit":5,
                                         "exact_match":true,"unit":"Fahrenheit"})")) {
        std::println("  full args    -> {}", js::invoke_with_json<^^demo::search_documents>(*doc));
    }
    if (const auto doc = parse_json(R"({"query":"hello","limit":1,"exact_match":false})")) {
        std::println("  unit omitted -> {}", js::invoke_with_json<^^demo::search_documents>(*doc));
    }
    if (const auto doc = parse_json(
            R"({"query":"x","limit":1,"exact_match":true,"unit":"Kelvin"})")) {
        try {
            (void)js::invoke_with_json<^^demo::search_documents>(*doc);
            std::println("  invalid enum -> NOT rejected (bug)");
            valid = false;
        } catch (const std::exception& e) {
            std::println("  invalid enum -> rejected: {}", e.what());
        }
    }

    std::println();
    std::println("--- Re-serialized by nlohmann/json (function schema) ---");
    if (const auto doc = parse_json(tool_schema)) {
        std::println("{}", doc->dump(2));
    }
}

// --- entry points for the Python LLM loop ------------------------------------
// The registered tools and models are the single source of truth. These emit
// their schemas and route by name, with no per-entity case: adding a tool or
// model touches only its CPP_REFLECT_* line.
//
// Nothing below writes to stdout. Every function returns the text that belongs
// there, so main()'s argv mode and --serve() dispatch through the same code and
// a debugged session exercises exactly what the CLI exercises.

namespace {

std::string tool_schemas_json()
{
    std::string out = "[";
    for (std::size_t i = 0; i < js::tool_registry().size(); ++i) {
        if (i) {
            out += ",";
        }
        out += std::string(js::tool_registry()[i].schema);
    }
    out += "]";
    return out;
}

/// Every registered model's JSON Schema, keyed by name. The schema is what the
/// model's structured output must conform to.
std::string response_schemas_json()
{
    std::string out = "{";
    std::size_t n = 0;
    for (const auto& entry : js::model_registry()) {
        if (n++) {
            out += ",";
        }
        out += "\"" + std::string(entry.name) + "\":" + std::string(entry.schema);
    }
    out += "}";
    return out;
}

/// The whole of `_in` parsed as JSON, where `_what` names the payload for the
/// error message.
json parse_stdin(std::istream& _in, const char* _what)
{
    const std::string input{std::istreambuf_iterator<char>(_in), std::istreambuf_iterator<char>()};
    const auto doc = parse_json(input);
    if (!doc) {
        throw std::runtime_error(std::string("invalid JSON ") + _what + " on stdin");
    }
    return *doc;
}

std::string call_tool_json(std::string_view _name, std::istream& _in)
{
    const json args = parse_stdin(_in, "arguments");
    for (const auto& entry : js::tool_registry()) {
        if (entry.name != _name) {
            continue;
        }
        try {
            return entry.run(args);
        } catch (const std::exception& e) {
            throw std::runtime_error("tool " + std::string(_name) + " failed: " + e.what());
        }
    }
    throw std::runtime_error("unknown tool: " + std::string(_name));
}

std::string parse_response_json(std::string_view _name, std::istream& _in)
{
    const json doc = parse_stdin(_in, "response");
    for (const auto& entry : js::model_registry()) {
        if (entry.name != _name) {
            continue;
        }
        try {
            // dump() re-serializes the reconstructed struct from reflection, so
            // no per-struct printing lives here.
            return entry.dump(doc);
        } catch (const std::exception& e) {
            throw std::runtime_error("parse into " + std::string(_name) + " failed: " + e.what());
        }
    }
    throw std::runtime_error("unknown model: " + std::string(_name));
}

} // namespace

std::string test_entry::run_request(const std::vector<std::string>& _args, std::istream& _in)
{
    if (_args.empty()) {
        throw std::runtime_error("no command");
    }
    const std::string& command = _args[0];
    if (command == "--emit-tools") {
        return tool_schemas_json();
    }
    if (command == "--emit-schemas") {
        return response_schemas_json();
    }
    if (command == "--call") {
        return call_tool_json(_args.size() > 1 ? _args[1] : "", _in);
    }
    if (command == "--parse") {
        return parse_response_json(_args.size() > 1 ? _args[1] : "", _in);
    }
    throw std::runtime_error("unknown command: " + command);
}

// --- --serve: the joint-debug transport --------------------------------------
// A debugger must own the process to set breakpoints, but Python must own the
// request channel. Both cannot be stdin/stdout, so the channel is out of band:
// lldb launches the process once and holds it open, and the Python side connects
// to the socket instead of spawning. Every request of a whole LLM loop then hits
// the same breakpoints in the same session.
//
// One JSON object per line each way:
//   request   {"args":["--call","search_documents"], "stdin":{...}}
//   response  {"stdout":"..."} or {"error":"..."}

namespace {

void write_all(int _fd, std::string_view _text)
{
    for (std::size_t sent = 0; sent < _text.size();) {
        const ssize_t written = ::write(_fd, _text.data() + sent, _text.size() - sent);
        if (written <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(written);
    }
}

/// Runs one request line and writes exactly one response line.
void respond(int _fd, const std::string& _line)
{
    json reply;
    try {
        const json request = json::parse(_line);
        const std::vector<std::string> args = request.value("args", std::vector<std::string>{});
        const bool has_stdin = request.contains("stdin") && !request.at("stdin").is_null();
        std::istringstream in(has_stdin ? request.at("stdin").dump() : std::string{});
        reply["stdout"] = test_entry::run_request(args, in);
    } catch (const std::exception& e) {
        reply["error"] = e.what();
    }
    write_all(_fd, reply.dump() + "\n");
}

/// Reads newline-delimited requests from one client until it disconnects.
void serve_client(int _fd)
{
    std::string pending;
    char chunk[4096];
    for (;;) {
        const ssize_t received = ::read(_fd, chunk, sizeof chunk);
        if (received <= 0) {
            return;
        }
        pending.append(chunk, static_cast<std::size_t>(received));
        std::size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty()) {
                respond(_fd, line);
            }
        }
    }
}

} // namespace

int test_entry::serve(std::string_view _socket_path)
{
    // A client that disconnects mid-session must not take the debuggee down.
    ::signal(SIGPIPE, SIG_IGN);

    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) {
        std::println(std::cerr, "serve: socket: {}", std::strerror(errno));
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (_socket_path.size() >= sizeof(address.sun_path)) {
        std::println(std::cerr, "serve: socket path too long");
        return 1;
    }
    std::memcpy(address.sun_path, _socket_path.data(), _socket_path.size());

    const std::string path(_socket_path);
    ::unlink(path.c_str());
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener, 1) < 0) {
        std::println(std::cerr, "serve: bind/listen {}: {}", path, std::strerror(errno));
        return 1;
    }

    std::println(std::cerr, "serve: listening on {}", path);
    // Never returns: the caller is a debugger holding the process open.
    for (;;) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            continue;
        }
        serve_client(client);
        ::close(client);
    }
}
