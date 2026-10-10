#include "test_entry.h"
#include "helpers.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <meta>
#include <optional>
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

#include "reflection_json.h"
#include "app_server.h"

// The example domain now lives in app_model.h so the MCP server and this CLI
// share one set of declarations; only the model registration stays here, in the
// namespace the macro expects, so exactly one TU runs the static initializers.
namespace app {
CPP_REFLECT_MODEL(Range)
CPP_REFLECT_MODEL(Query)
CPP_REFLECT_MODEL(SearchRequest)
} // namespace app

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
                test_entry::log()->error("  [FAIL] {}: \"properties\" is missing or not an object", _path);
                return false;
            }
            if (!_doc.contains("required") || !_doc.at("required").is_array()) {
                test_entry::log()->error("  [FAIL] {}: \"required\" is missing or not an array", _path);
                return false;
            }
            for (const auto& name : _doc.at("required")) {
                const std::string key = name.get<std::string>();
                if (!_doc.at("properties").contains(key)) {
                    test_entry::log()->error("  [FAIL] {}: \"required\" lists \"{}\", absent from \"properties\"",
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
        test_entry::log()->error("  [FAIL] {}: not well-formed JSON", _label);
        return false;
    }
    if (!check_object_shapes(*doc, _label)) {
        return false;
    }
    test_entry::log()->info("  [ok]   {}: well-formed, nested object schemas consistent ({} bytes)",
                            _label, _text.size());
    return true;
}

/// The OpenAI tool wrapper: {"type":"function","function":{name,description,parameters}}.
bool check_tool_schema(const char* _label, std::string_view _text)
{
    const auto doc = parse_json(_text);
    if (!doc) {
        test_entry::log()->error("  [FAIL] {}: not well-formed JSON", _label);
        return false;
    }
    if (!doc->contains("type") || doc->at("type") != "function" ||
        !doc->contains("function") || !doc->at("function").is_object()) {
        test_entry::log()->error("  [FAIL] {}: missing the OpenAI tool wrapper", _label);
        return false;
    }
    const json& fn = doc->at("function");
    for (const char* key : {"name", "description", "parameters"}) {
        if (!fn.contains(key)) {
            test_entry::log()->error("  [FAIL] {}: \"function.{}\" is missing", _label, key);
            return false;
        }
    }
    if (!fn.at("name").is_string() || !fn.at("description").is_string()) {
        test_entry::log()->error("  [FAIL] {}: \"function.name\"/\"description\" are not strings", _label);
        return false;
    }
    if (!check_object_shapes(fn.at("parameters"), std::string(_label) + "/parameters")) {
        return false;
    }
    test_entry::log()->info("  [ok]   {}: valid tool, name=\"{}\", {} parameter(s)", _label,
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
        test_entry::log()->error("  [FAIL] property \"{}\" carries no description", _name);
        return false;
    }
    const std::string got = _properties.at(_name).at("description").get<std::string>();
    if (got != _expected) {
        test_entry::log()->error("  [FAIL] property \"{}\" description = \"{}\", expected \"{}\"", _name,
                                 got, _expected);
        return false;
    }
    test_entry::log()->info("  [ok]   property \"{}\" -> \"{}\"", _name, got);
    return true;
}

} // namespace

void test_entry::implement_json_schema()
{
    test_entry::section banner{"Implement JSON Schema"};

    constexpr std::string_view fn_schema =
        js::function_schema<^^app::CppReflectServer::search_documents>();
    constexpr std::string_view tool_schema =
        js::function_schema<^^app::CppReflectServer::search_documents, js::Style::OpenAiTool>();
    constexpr std::string_view query_schema = js::tool_schema<app::Query>();
    constexpr std::string_view request_schema = js::tool_schema<app::SearchRequest>();

    test_entry::log()->debug("--- Function -> JSON Schema ---");
    test_entry::log()->trace("{}", fn_schema);

    test_entry::log()->trace("");
    test_entry::log()->debug("--- Function -> OpenAI tool ---");
    test_entry::log()->trace("{}", tool_schema);

    test_entry::log()->trace("");
    test_entry::log()->debug("--- Struct -> JSON Schema (Query) ---");
    test_entry::log()->trace("{}", query_schema);

    test_entry::log()->trace("");
    test_entry::log()->debug("--- Struct -> JSON Schema (SearchRequest) ---");
    test_entry::log()->trace("{}", request_schema);

    test_entry::log()->trace("");
    test_entry::log()->debug("--- Validation with nlohmann/json {}.{}.{} ---",
                 NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
                 NLOHMANN_JSON_VERSION_PATCH);

    bool valid = true;
    valid &= check_schema("function schema", fn_schema);
    valid &= check_tool_schema("openai tool", tool_schema);
    valid &= check_schema("struct Query", query_schema);
    valid &= check_schema("struct SearchRequest", request_schema);
    test_entry::log()->trace("");
    test_entry::log()->debug("--- Parameter / field descriptions ---");
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
    test_entry::log()->trace("  description keys: fn={}, tool={}, query={}, request={}", count_of(fn_schema),
                 count_of(tool_schema), count_of(query_schema), count_of(request_schema));
    if (valid) {
        test_entry::log()->info("  => all schemas valid");
    } else {
        test_entry::log()->error("  => VALIDATION FAILED");
    }

    test_entry::log()->trace("");
    test_entry::log()->debug("--- JSON value -> C++ struct ---");
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
        const app::SearchRequest req = js::from_json<app::SearchRequest>(*doc);
        test_entry::log()->trace("  query.text        = {}", req.query.text);
        test_entry::log()->trace("  query.unit        = {}",
                     req.query.unit == app::Unit::Celsius ? "Celsius" : "Fahrenheit");
        test_entry::log()->trace("  query.ranges      = {} entries", req.query.ranges.size());
        test_entry::log()->trace("  ranges[0].min     = {}", req.query.ranges[0].min);
        test_entry::log()->trace("  ranges[0].max     = {}",
                     req.query.ranges[0].max ? std::to_string(*req.query.ranges[0].max) : "null");
        test_entry::log()->trace("  ranges[1].max     = {} (absent in JSON)",
                     req.query.ranges[1].max ? std::to_string(*req.query.ranges[1].max) : "null");
        test_entry::log()->trace("  query.weight      = {}",
                     req.query.weight ? std::to_string(*req.query.weight) : "null");
        test_entry::log()->trace("  top_k             = [{}, {}, {}]", req.top_k[0], req.top_k[1], req.top_k[2]);
        test_entry::log()->trace("  debug             = {} (absent -> default)", req.debug);
    }

    test_entry::log()->trace("");
    test_entry::log()->debug("--- JSON args -> actual member call ---");
    app::CppReflectServer call_server;
    if (const auto doc = parse_json(R"({"query":"reflection in C++26","limit":5,
                                         "exact_match":true,"unit":"Fahrenheit"})")) {
        test_entry::log()->trace(
            "  full args    -> {}",
            js::invoke_member_with_json<^^app::CppReflectServer::search_documents>(call_server, *doc));
    }
    if (const auto doc = parse_json(R"({"query":"hello","limit":1,"exact_match":false})")) {
        test_entry::log()->trace(
            "  unit omitted -> {}",
            js::invoke_member_with_json<^^app::CppReflectServer::search_documents>(call_server, *doc));
    }
    if (const auto doc = parse_json(
            R"({"query":"x","limit":1,"exact_match":true,"unit":"Kelvin"})")) {
        try {
            (void)js::invoke_member_with_json<^^app::CppReflectServer::search_documents>(
                call_server, *doc);
            test_entry::log()->error("  invalid enum -> NOT rejected (bug)");
            valid = false;
        } catch (const std::exception& e) {
            test_entry::log()->info("  invalid enum -> rejected: {}", e.what());
        }
    }

    test_entry::log()->trace("");
    test_entry::log()->debug("--- Re-serialized by nlohmann/json (function schema) ---");
    if (const auto doc = parse_json(tool_schema)) {
        test_entry::log()->trace("{}", doc->dump(2));
    }
}

// --- entry points for the Python LLM loop ------------------------------------
// The tool table is class-owned (an mcp::Server instance) and the models still
// self-register. These emit their schemas and route by name, with no per-entity
// case: a tool is added by marking its function, a model by its CPP_REFLECT_MODEL
// line.
//
// Nothing below writes to stdout. Every function returns the text that belongs
// there, so main()'s argv mode and --serve() dispatch through the same code and
// a debugged session exercises exactly what the CLI exercises.

namespace {

/// The tool table projected as OpenAI tools. The same instance that serves MCP
/// supplies this, so --emit-tools and MCP tools/list can never disagree.
std::string tool_schemas_json()
{
    app::CppReflectServer server;
    return server.tools_as_openai_json();
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
    app::CppReflectServer server;
    return server.call_tool(_name, args);
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
        test_entry::log_err()->error("serve: socket: {}", std::strerror(errno));
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (_socket_path.size() >= sizeof(address.sun_path)) {
        test_entry::log_err()->error("serve: socket path too long");
        return 1;
    }
    std::memcpy(address.sun_path, _socket_path.data(), _socket_path.size());

    const std::string path(_socket_path);
    ::unlink(path.c_str());
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener, 1) < 0) {
        test_entry::log_err()->error("serve: bind/listen {}: {}", path, std::strerror(errno));
        return 1;
    }

    test_entry::log_err()->info("serve: listening on {}", path);
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
