#include "app_server.h"
#include "test_entry.h"

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

int test_entry::run_mcp()
{
    app::CppReflectServer server;
    return server.serve(std::cin, std::cout);
}

// --- tests ------------------------------------------------------------------
// The whole suite drives the real serve() loop through string streams, so what
// is asserted is exactly what an MCP client receives: one reply line per request.

namespace {

using json = nlohmann::json;

/// Runs newline-delimited requests through a fresh server and returns the reply
/// lines. A request without an id produces no line, which is how the notification
/// case is observed.
std::vector<json> drive(const std::vector<std::string>& _requests)
{
    std::string input;
    for (const std::string& request : _requests) {
        input += request;
        input += '\n';
    }
    std::istringstream in{input};
    std::ostringstream out;
    app::CppReflectServer server;
    server.serve(in, out);

    std::vector<json> replies;
    std::istringstream lines{out.str()};
    std::string line;
    while (std::getline(lines, line)) {
        replies.push_back(json::parse(line));
    }
    return replies;
}

json request(std::string_view _method, json _params = json::object())
{
    json r{{"jsonrpc", "2.0"}, {"id", 1}, {"method", _method}};
    if (!_params.empty()) {
        r["params"] = std::move(_params);
    }
    return r;
}

bool check(bool _ok, std::string_view _what)
{
    if (_ok) {
        test_entry::log()->info("  [ok]   {}", _what);
    } else {
        test_entry::log()->error("  [FAIL] {}", _what);
    }
    return _ok;
}

bool check_eq(const json& _got, const json& _want, std::string_view _what)
{
    if (_got == _want) {
        test_entry::log()->info("  [ok]   {}", _what);
        return true;
    }
    test_entry::log()->error("  [FAIL] {}: got {}, want {}", _what, _got.dump(), _want.dump());
    return false;
}

/// Reply to the single request `_request`, asserting it is a JSON-RPC result
/// (not an error) and returning the `result` object.
json result_of(std::string_view _request)
{
    return drive({std::string(_request)}).at(0).at("result");
}

} // namespace

void test_entry::implement_mcp()
{
    test_entry::section banner{"Implement MCP server"};
    bool valid = true;

    test_entry::log()->debug("--- server/discover ---");
    {
        const json result = result_of(request("server/discover").dump());
        valid &= check_eq(result.at("supportedVersions"), json::array({mcp::kProtocolVersion}),
                          "advertises the pinned revision");
        const json& caps = result.at("capabilities");
        valid &= check(caps.contains("tools") && caps.contains("prompts") && caps.contains("resources"),
                       "capabilities cover tools, prompts and resources");
        valid &= check_eq(
            result.at("_meta").at("io.modelcontextprotocol/serverInfo").at("name"), "cppreflect",
            "serverInfo.name");
    }

    test_entry::log()->debug("--- tool ---");
    {
        const json tools = result_of(request("tools/list").dump()).at("tools");
        valid &= check_eq(tools.size(), 1, "exactly the marked function is registered");
        const json& tool = tools.at(0);
        valid &= check_eq(tool.at("name"), "search_documents", "name from identifier_of");
        valid &= check_eq(tool.at("description"), "Searches the document index.",
                          "description from js::doc");
        const json& schema = tool.at("inputSchema");
        valid &= check_eq(schema.at("required"), json::array({"query", "limit", "exact_match"}),
                          "required = the non-optional parameters");
        valid &= check_eq(schema.at("properties").at("query").at("description"),
                          "The raw query text.", "parameter description from js::param_docs");
        valid &= check_eq(schema.at("properties").at("unit").at("anyOf").size(), 2,
                          "std::optional parameter -> anyOf[enum, null]");
    }
    {
        const json result = result_of(request("tools/call",
                                              {{"name", "search_documents"},
                                               {"arguments", {{"query", "mcp"},
                                                              {"limit", 2},
                                                              {"exact_match", true},
                                                              {"unit", "Fahrenheit"}}}})
                                         .dump());
        valid &= check_eq(result.at("isError"), false, "valid call is not an error");
        valid &= check_eq(result.at("content").at(0).at("type"), "text", "content is a text block");
        valid &= check(result.at("content").at(0).at("text").get<std::string>().find("mcp #1") !=
                           std::string::npos,
                       "the reflected call produced its payload");
    }
    {
        // A tool that throws is a *result* with isError, not a JSON-RPC error, so
        // the model can self-correct instead of the client seeing a transport fault.
        const json reply =
            drive({request("tools/call", {{"name", "search_documents"}, {"arguments", {{"query", "x"}}}})
                       .dump()})
                .at(0);
        valid &= check(!reply.contains("error"), "a failing tool is not a JSON-RPC error");
        valid &= check_eq(reply.at("result").at("isError"), true, "missing argument -> isError:true");
    }
    {
        const json reply =
            drive({request("tools/call", {{"name", "nosuch"}, {"arguments", json::object()}}).dump()})
                .at(0);
        valid &= check_eq(reply.at("error").at("code"), -32602, "unknown tool -> -32602");
    }

    test_entry::log()->debug("--- prompt ---");
    {
        const json prompts = result_of(request("prompts/list").dump()).at("prompts");
        valid &= check_eq(prompts.size(), 1, "exactly the marked function is registered");
        const json& prompt = prompts.at(0);
        valid &= check_eq(prompt.at("name"), "explain", "name from identifier_of");
        valid &= check_eq(prompt.at("description"), "Explain a reflection concept to a newcomer.",
                          "description from js::doc");
        const json& argument = prompt.at("arguments").at(0);
        valid &= check_eq(argument.at("name"), "topic",
                          "PromptArgument.name is the C++ parameter name");
        valid &= check_eq(argument.at("required"), true, "non-optional parameter -> required:true");
        valid &= check_eq(argument.at("description"), "The concept to explain, e.g. \"template for\".",
                          "PromptArgument.description from js::param_docs");
    }
    {
        const json result = result_of(
            request("prompts/get", {{"name", "explain"}, {"arguments", {{"topic", "P2996"}}}}).dump());
        valid &= check_eq(result.at("messages").at(0).at("role"), "user", "message role");
        valid &= check(result.at("messages")
                           .at(0)
                           .at("content")
                           .at("text")
                           .get<std::string>()
                           .find("P2996") != std::string::npos,
                       "the prompt body received its argument");
    }
    {
        const json reply =
            drive({request("prompts/get", {{"name", "explain"}, {"arguments", json::object()}}).dump()})
                .at(0);
        valid &= check_eq(reply.at("error").at("code"), -32602,
                          "missing required prompt argument -> -32602");
    }
    {
        const json reply = drive({request("prompts/get", {{"name", "nosuch"}}).dump()}).at(0);
        valid &= check_eq(reply.at("error").at("code"), -32602, "unknown prompt -> -32602");
    }

    test_entry::log()->debug("--- resource ---");
    {
        const json resources = result_of(request("resources/list").dump()).at("resources");
        valid &= check_eq(resources.size(), 1, "exactly the marked function is registered");
        const json& resource = resources.at(0);
        valid &= check_eq(resource.at("uri"), "file:///docs/mcp_automation.md",
                          "uri read from the marker");
        valid &= check_eq(resource.at("mimeType"), "text/markdown", "mimeType read from the marker");
        valid &= check_eq(resource.at("name"), "design_notes", "name from identifier_of");
        valid &= check_eq(resource.at("description"),
                          "Design notes for the reflection-driven MCP server.",
                          "description from js::doc");
    }
    {
        const json result = result_of(
            request("resources/read", {{"uri", "file:///docs/mcp_automation.md"}}).dump());
        const json& contents = result.at("contents");
        valid &= check_eq(contents.size(), 1, "one content entry");
        valid &= check_eq(contents.at(0).at("mimeType"), "text/markdown", "content mimeType");
        valid &= check(contents.at(0).at("text").get<std::string>().find("Reflection-driven MCP") !=
                           std::string::npos,
                       "the resource body was invoked");
    }
    {
        const json reply = drive({request("resources/read", {{"uri", "file:///nope"}}).dump()}).at(0);
        valid &= check_eq(reply.at("error").at("code"), -32602,
                          "missing resource -> -32602 (never empty contents)");
    }

    test_entry::log()->debug("--- protocol edges ---");
    {
        const std::vector<json> replies =
            drive({R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{}})"});
        valid &= check(replies.empty(), "a notification (no id) draws no reply");
    }
    {
        const json reply = drive({"not json"}).at(0);
        valid &= check_eq(reply.at("error").at("code"), -32700, "malformed JSON -> -32700");
    }
    {
        const json reply = drive({request("tools/unknown").dump()}).at(0);
        valid &= check_eq(reply.at("error").at("code"), -32601, "unknown method -> -32601");
    }
    {
        json r = request("tools/list");
        r["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2025-06-18";
        const json reply = drive({r.dump()}).at(0);
        valid &= check_eq(reply.at("error").at("code"), -32022,
                          "a mismatched protocol version -> -32022");
    }

    test_entry::log()->debug("--- one table, two emitters ---");
    {
        app::CppReflectServer server;
        const json openai = json::parse(server.tools_as_openai_json());
        const json listed = result_of(request("tools/list").dump()).at("tools");
        valid &= check_eq(openai.size(), listed.size(),
                          "--emit-tools and MCP tools/list read one table");
        valid &= check_eq(openai.at(0).at("function").at("name"), listed.at(0).at("name"),
                          "same name from both emitters");
        valid &= check_eq(openai.at(0).at("function").at("parameters"), listed.at(0).at("inputSchema"),
                          "same inputSchema behind the OpenAI wrapper");
        valid &= check(server.call_tool("search_documents",
                                        {{"query", "q"}, {"limit", 1}, {"exact_match", false}})
                               .find("q #1") != std::string::npos,
                       "call_tool mirrors the MCP call path");
        bool threw = false;
        try {
            (void)server.call_tool("nosuch", json::object());
        } catch (const std::exception& _e) {
            threw = std::string(_e.what()) == "unknown tool: nosuch";
        }
        valid &= check(threw, "call_tool keeps the CLI error wording");
    }

    test_entry::log()->trace("");
    if (valid) {
        test_entry::log()->info("  => all MCP checks passed");
    } else {
        test_entry::log()->error("  => MCP VALIDATION FAILED");
    }
}
