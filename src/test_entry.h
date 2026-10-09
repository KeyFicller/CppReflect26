#pragma once

#include <cstddef>
#include <istream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace test_entry {

// RAII section banner: prints the header on construction and a rule line of
// matching width (plus a blank line) on destruction:
//   =============== Hello Reflection ================
//   ...
//   ==================================================
class section {
public:
    explicit section(std::string_view _name)
        : m_rule_width(kPad * 2 + _name.size() + 2)
    {
        std::println("{} {} {}", std::string(kPad, '='), _name, std::string(kPad, '='));
    }

    ~section()
    {
        std::println("{}", std::string(m_rule_width, '='));
        std::println();
    }

    section(const section&) = delete;
    section& operator=(const section&) = delete;

private:
    static constexpr std::size_t kPad = 15;
    std::size_t m_rule_width;
};

void hello_reflection();

void grammar_and_concepts();

void list_of_meta_functions();

void implement_enumeration_reflection();

void implement_struct_reflection();

void implement_ui_reflection();

void implement_undo_redo_reflection();

void implement_ffn_reflection();

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
