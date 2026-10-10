#include "test_entry.h"
#include "helpers.h"
#include <cstring>
#include <exception>
#include <iostream>
#include <print>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {

    // Long-running mode for joint debugging: lldb launches this once and holds
    // it open while Python drives through the socket.
    if (argc > 1 && std::strcmp(argv[1], "--serve") == 0) {
        return test_entry::serve(argc > 2 ? argv[2] : "/tmp/cpp_reflect.sock");
    }

    // MCP server mode: newline-delimited JSON-RPC 2.0 on stdin/stdout. Kept
    // ahead of the "--" CLI branch below, whose stdout must stay a single JSON
    // document rather than a protocol stream.
    if (argc > 1 && std::strcmp(argv[1], "--mcp") == 0) {
        return test_entry::run_mcp();
    }

    // CLI modes used by the Python LLM loop.
    if (argc > 1 && std::strncmp(argv[1], "--", 2) == 0) {
        const std::vector<std::string> args(argv + 1, argv + argc);
        try {
            std::println("{}", test_entry::run_request(args, std::cin));
            return 0;
        } catch (const std::exception& e) {
            test_entry::log_err()->error("{}", e.what());
            return 1;
        }
    }

    test_entry::hello_reflection();

    test_entry::grammar_and_concepts();

    test_entry::list_of_meta_functions();

    test_entry::implement_enumeration_reflection();

    test_entry::implement_struct_reflection();

    test_entry::implement_undo_redo_reflection();

    //test_entry::implement_ui_reflection();

    test_entry::implement_ffn_reflection();

    test_entry::implement_json_schema();

    test_entry::implement_mcp();

    //std::cin.get();

}
