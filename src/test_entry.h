#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <vector>

// The reflection suite's entry points, in the order main() runs them; each is
// implemented in its own translation unit. Shared helpers live in "helpers.h".
namespace test_entry {

void hello_reflection();

void grammar_and_concepts();

void list_of_meta_functions();

void implement_enumeration_reflection();

void implement_struct_reflection();

void implement_ui_reflection();

void implement_undo_redo_reflection();

void implement_ffn_reflection();

void implement_json_schema();

/// Exercises the MCP server end to end: drives the real serve() loop over string
/// streams and checks the tool / prompt / resource projections and the protocol
/// error codes.
void implement_mcp();

/// Runs one CLI-shaped request without touching std::cin/std::cout: `_args` is
/// argv[1..], and `_in` supplies stdin for the commands that take it (it is left
/// untouched otherwise). Returns the text that belongs on stdout, or throws
/// std::runtime_error. main() and serve() share this, so a session under a
/// debugger dispatches exactly like a plain CLI invocation.
std::string run_request(const std::vector<std::string>& _args, std::istream& _in);

/// Serves run_request over newline-delimited JSON on a Unix socket, so a
/// debugger can hold one process open across many requests. Does not return.
int serve(std::string_view _socket_path);

/// Runs the MCP server over stdin/stdout (newline-delimited JSON-RPC 2.0).
/// Returns when stdin reaches EOF.
int run_mcp();

} // namespace test_entry
