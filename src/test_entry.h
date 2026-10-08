#pragma once

#include <print>
#include <string_view>

namespace test_entry {

// Uniform section banner, e.g. "=== Hello Reflection ===".
inline void section(std::string_view _name)
{
    std::println("=============== {} ================", _name);
}

void hello_reflection();

void grammar_and_concepts();

void list_of_meta_functions();

void implement_enumeration_reflection();

void implement_struct_reflection();

void implement_ui_reflection();

void implement_undo_redo_reflection();

void implement_ffn_reflection();

} // namespace test_entry
