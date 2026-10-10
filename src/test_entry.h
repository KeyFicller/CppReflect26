#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <vector>

// The reflection suite's table of contents: one entry per reflection example,
// listed in the order main() runs them. Each is implemented in its own
// translation unit. The CLI bridge (--emit-tools / --call / --emit-schemas /
// --parse) goes through run_request(); --serve() shares the same dispatch.
// Shared helpers (the section banner, member_static_array) live in "helpers.h".
namespace test_entry {

/// A POD's fields, printed by splicing each reflected member.
void hello_reflection();

/// The ^^ and [: :] operators, reflection comparison, and `template for`.
void grammar_and_concepts();

/// A tour of std::meta queries: names, types, members, classification, bases.
void list_of_meta_functions();

/// enum <-> string, enumerator value/name tables, and bitmask rendering.
void implement_enumeration_reflection();

/// Member count/name/has-member queries; YAML dump+load and raw byte
/// (de)serialization, all driven by the member list.
void implement_struct_reflection();

/// Auto-generated Dear ImGui widgets from reflected members and base classes.
void implement_ui_reflection();

/// Reflection-driven undo/redo: locate a member by index, snapshot it, roll a
/// command frame back.
void implement_undo_redo_reflection();

/// A forward-chain network composed at compile time from reflected members.
void implement_ffn_reflection();

/// The LLM bridge: JSON Schema from annotations, from_json/to_json, the tool
/// and model registries.
void implement_json_schema();

/// Runs one CLI-shaped request without touching std::cin/std::cout: `_args` is
/// argv[1..], and `_in` supplies stdin for the commands that take it (it is left
/// untouched otherwise). Returns the text that belongs on stdout, or throws
/// std::runtime_error. main() and serve() share this, so a session under a
/// debugger dispatches exactly like a plain CLI invocation.
std::string run_request(const std::vector<std::string>& _args, std::istream& _in);

/// Serves run_request over newline-delimited JSON on a Unix socket, so a
/// debugger can hold one process open across many requests. Does not return.
int serve(std::string_view _socket_path);

} // namespace test_entry
