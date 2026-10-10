# Reflection ⇄ LLM bridge

How the Python LLM side talks to this C++ reflection code: the CLI contract, the
Python client, the editor debug setup, and the joint-debugging workflow.

Build prerequisites, `LLVM_ROOT`, dependency fetching and the plain `cmake`
commands live in [`../ReadMe.md`](../ReadMe.md); everything below starts at the CLI.

---

## CLI: the C++ ⇄ Python contract

Everything the Python side does goes through *one* dispatch —
`test_entry::run_request` in `src/implement_json_schema.cpp` — shared by the argv
path and the socket server, so a debugged session executes exactly what a plain
CLI invocation executes. Adding a tool or struct touches only its
`CPP_REFLECT_TOOL` / `CPP_REFLECT_MODEL` line; there is no per-entity `switch`.

| Command | stdin | stdout |
|---|---|---|
| `--emit-tools` | — | JSON array of OpenAI tool schemas |
| `--call <tool>` | tool-call arguments (JSON) | the tool's JSON result |
| `--emit-schemas` | — | JSON object of model schemas, keyed by name |
| `--parse <Model>` | the model's JSON | reconstructed struct, re-serialized |
| `--serve [sock]` | — | long-running socket server (see *Debugging*) |

```sh
./build/CppReflection --emit-tools

./build/CppReflection --emit-schemas | python3 -m json.tool

echo '{"query":"hi","limit":2,"exact_match":true,"unit":"Celsius"}' \
  | ./build/CppReflection --call search_documents

echo '{"query":{"text":"x","unit":"Celsius","ranges":[],"fuzzy":false},
            "top_k":[1,2,3],"debug":false}' \
  | ./build/CppReflection --parse SearchRequest
```

Errors go to **stderr** with exit code 1 (`invalid JSON arguments on stdin`,
`unknown tool: …`, `unknown model: …`, `unknown command: …`).

Anything starting with `--` is treated as a command; any other argv runs the
reflection suite in `main()`. `--serve` can take no argument (defaults to
`/tmp/cpp_reflect.sock`) and never returns.

---

## Python client

```sh
python -m llm_client tools      [question]
python -m llm_client mcp        [--prompt NAME [key=value ...]] [--resource URI] [question]
python -m llm_client structured [request] [format]
```

- `tools` — asks `--emit-tools`, binds the schemas, runs the model, feeds each
  tool call's arguments to `--call <name>`, then asks for a final answer.
- `mcp` — the same loop, but the whole MCP surface goes through LangChain's own
  `MCPAdapter` (FastMCP underneath), which owns the transport, the session and
  protocol negotiation. It is async, so the mode runs under `asyncio.run`. The
  three primitives are used as MCP defines them — by their own actor:
  - **tools** are model-controlled: discovered via the adapter, handed to the model.
  - **prompts** are user-controlled: `--prompt NAME key=value ...` is *you* picking
    a template, whose `prompts/get` messages open the conversation.
  - **resources** are application-controlled: `--resource URI` is *this app*
    choosing context, attached as a system message. The model never asks for one.
- `structured` — asks `--emit-schemas`, forces a single tool via
  `with_structured_output`, and hands the model's JSON to `--parse <format>`.

`structured` is **strict positional**: pass *both* `request` and `format`, or
neither (defaults apply). A lone argument is rejected rather than guessed, since
`structured SearchRequest` would otherwise silently extract from the literal
request text `"SearchRequest"`.

`requirements.txt` (`langchain`, `langgraph`, `langchain-deepseek`,
`langchain-ollama`) is installed into `.venv`; `.env` must define
`DEEPSEEK_API_KEY`. `llm_client.py` never redeclares a C++ signature — schemas and
results always cross a boundary the C++ side defines, whether that is the `--*`
CLI or the MCP wire.

Endpoint quirks handled in `make_model()`:

- `response_format: {"type":"json_schema"}` is rejected by the API ("unavailable
  now"), so a schema is sent as a tool/function schema instead.
- That path forces `tool_choice`, which thinking mode refuses
  ("Thinking mode does not support this tool_choice"); `reasoning_effort="none"`
  clears it. Plain tool calling forces nothing, so thinking can stay on.
- Duplicate tool names in one request are rejected outright, hence the
  `std::abort()` uniqueness check on registration.

---

## Editor setup (Cursor / VS Code)

`.gitignore` lists `.vscode/*`, so these files are **local convenience config** —
they are not in the repo and must be recreated by hand on a new machine. They are
reproduced here so they survive.

Needs the **CodeLLDB** (`vadimcn.vscode-lldb`) extension for `"type": "lldb"` and
the Microsoft Python extension for `"type": "debugpy"`.

### `.vscode/settings.json`

```jsonc
{
  // CMake Tools: use the P2996 clang++ and Ninja, configured in CMakeLists.txt.
  "cmake.generator": "Ninja",
  "cmake.buildDirectory": "${workspaceFolder}/build",
  "cmake.configureOnOpen": true,

  // clangd. --query-driver lets the local clang-p2996 fork report its own
  // libc++ (include/c++/v1) and the macOS sysroot.
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build",
    "--query-driver=/Users/keyficller/src/llvm-p2996/bin/clang++"
  ],
  // Cursor bundles cpptools; keep its IntelliSense from fighting clangd.
  "C_Cpp.intelliSenseEngine": "disabled",

  "files.associations": { "meta": "cpp" }
}
```

### `.vscode/tasks.json`

```jsonc
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "cmake configure",
      "type": "shell",
      "command": "cmake -S . -B build -G Ninja",
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": []
    },
    {
      "label": "cmake build",
      "type": "shell",
      "command": "cmake --build build",
      "options": { "cwd": "${workspaceFolder}" },
      "dependsOn": "cmake configure",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": ["$gcc"]
    }
  ]
}
```

### `.vscode/launch.json`

```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug CppReflection (LLDB)",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/build/CppReflection",
      "args": [],
      "cwd": "${workspaceFolder}",
      "preLaunchTask": "cmake build"
    },
    {
      // Joint debugging, step 1. CodeLLDB holds this process open as a server,
      // so breakpoints stay armed for a whole LLM loop instead of dying after
      // one call. Python connects through the socket (config below), which is
      // why the request channel is out of band rather than stdin/stdout.
      "name": "C++: --serve (joint debug)",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/build/CppReflection",
      "args": ["--serve", "/tmp/cpp_reflect.sock"],
      "cwd": "${workspaceFolder}",
      "terminal": "integrated",
      "preLaunchTask": "cmake build"
    },
    {
      // Joint debugging, step 2. Debugs the Python side; every C++ call it
      // makes goes to the running --serve process above.
      "name": "Python: llm_client tools",
      "type": "debugpy",
      "request": "launch",
      "module": "llm_client",
      "args": ["tools"],
      "cwd": "${workspaceFolder}",
      "python": "${workspaceFolder}/.venv/bin/python",
      "console": "integratedTerminal",
      "env": { "CPP_REFLECT_SOCKET": "/tmp/cpp_reflect.sock" }
    },
    {
      "name": "Python: llm_client structured",
      "type": "debugpy",
      "request": "launch",
      "module": "llm_client",
      "args": ["structured", "<request text>", "SearchRequest"],
      "cwd": "${workspaceFolder}",
      "python": "${workspaceFolder}/.venv/bin/python",
      "console": "integratedTerminal",
      "env": { "CPP_REFLECT_SOCKET": "/tmp/cpp_reflect.sock" }
    }
  ],
  "compounds": [
    {
      // One-click: starts the server, then the Python loop that drives it.
      // Breakpoints in both languages are live at the same time.
      "name": "Joint: C++ serve + Python tools",
      "configurations": ["C++: --serve (joint debug)", "Python: llm_client tools"],
      "stopAll": true
    }
  ]
}
```

---

## Debugging

### Why the socket exists

A debugger must **own** the process to bind breakpoints, but Python must own the
request channel. Both cannot be stdin/stdout. And the two usual escapes are
closed on this machine: `process attach --waitfor` is denied
(`DevToolsSecurity -status` → *Developer mode is currently disabled*), and
`follow-fork-mode child` does not survive CPython's `posix_spawn`.

So `--serve` keeps one process alive and moves the channel **out of band** to a
Unix socket: lldb launches the process once and holds it open, while the Python
side connects instead of spawning. `run_cpp()` spawns a fresh process by default
and talks to the socket only when `CPP_REFLECT_SOCKET` is set.

To allow attach instead: `sudo DevToolsSecurity -enable`.

### Cursor / VS Code (GUI)

1. Click a breakpoint in the gutter (e.g. on `call_tool_json`).
2. `F5` → **`C++: --serve (joint debug)`**; wait for `serve: listening on …`.
   Leave this session running — it is the server.
3. `F5` → **`Python: llm_client tools`**; the C++ breakpoint lights up and the
   editor shows locals (`_name`, the raw `_j`), the call stack and watches.

`F5` continue · `F10` step over · `F11` step in · `Shift+F11` step out ·
`Shift+F5` stop. Stopping this way **does** kill the `--serve` process cleanly.

One-click alternative: **`Joint: C++ serve + Python tools`**.

Tip: right-click a gutter breakpoint → *Edit Breakpoint* → change the kind to
**Log Message** and enter `>>> {_name}` to log every call without stopping.

### Terminal lldb

```sh
lldb build/CppReflection
(lldb) breakpoint set --file implement_json_schema.cpp --line 1106
(lldb) process launch -- --serve /tmp/cpp_reflect.sock     # stays alive
```

Then, in another terminal:

```sh
CPP_REFLECT_SOCKET=/tmp/cpp_reflect.sock python -m llm_client tools
```

To debug a single CLI call instead, feed stdin through lldb (see *Gotchas*):

```sh
printf '%s' '{"query":"C++26 static reflection","limit":3,"exact_match":true,"unit":"Fahrenheit"}' > /tmp/args.json

lldb build/CppReflection
(lldb) breakpoint set --file implement_json_schema.cpp --line 576
(lldb) process launch --stdin /tmp/args.json -- --call search_documents
```

`Ctrl-C` in lldb takes back control from the server's `accept()` at any time; set
more breakpoints and `continue`.

### Common breakpoints

Prefer **file + line** over `--name` for templates: a single `from_json`
instantiation expands to many symbols.

| Location | What you see |
|---|---|
| `call_tool_json` (≈1106) | tool name + raw args from the model |
| `parse_response_json` (≈1122) | the structured-extraction JSON |
| `invoke_with_json_impl` (≈576) | just before the real C++ call |
| `from_json` (≈516) | per-type deserialization (array length, enum errors) |
| `serve` (≈1227) | server startup |

Line numbers drift; treat them as hints.

### Gotchas

- **stdin must be fed with `--stdin <file>`**, not a shell `< file` redirect. The
  latter makes the program wait on input and hangs lldb.
- **A `--serve` server can outlive its lldb parent as an orphan.** Reclaim it with
  `pkill -f "CppReflection --serve"`. (`serve()` unlinks the socket before
  binding, so a stale file never blocks a fresh start.)
- While a C++ breakpoint is hit, Python blocks reading the socket — expected;
  `continue` releases it. A request that hangs past `CPP_REFLECT_TIMEOUT` seconds
  (default 30) fails instead of blocking forever; set `CPP_REFLECT_TIMEOUT=0` to
  wait indefinitely while stepping in the debugger.
- `python -m llm_client`, not `python -m llm_client.py`.

---

## Layout

```
main.cpp                         argv routing + --serve; else runs the reflection suite
src/test_entry.h                 entry points + run_request/serve
src/helpers.h                    section banner + member_static_array
src/implement_json_schema.cpp    schema gen, from_json/to_json, registries,
                                 run_request + --serve socket server
llm_client.py                    tools + structured modes; the only C++ caller
.vscode/                         local editor/debug config (gitignored)
```

---

## Notes / quirks encountered

Compiler (clang-p2996 fork):

- `std::meta::info` is consteval-only — it cannot live in a runtime variable. Keep
  it inside `consteval` helpers that hand plain values to runtime code.
- `template for` over reflection ranges fails here (`could not compute size of
  expansion`); iterate with `std::index_sequence<Is...>` instead.
- A reflection range's `.size()` needs a `consteval` helper before it can be a
  template argument.
- Function *parameter* annotations fail inside the intrinsic, so parameter docs
  are attached at function level via `[[= js::param_docs(...) ]]`.
- Overloads: `^^f` binds to the last declared overload; enumerate an overload set
  through `meta::members_of`. We require globally unique tool/model names instead
  (enforced with `std::abort()` at startup).

`std::array<T, N>` is pinned in both directions: the schema carries
`"minItems": N` and `"maxItems": N`, and `from_json` rejects any document whose
array length is not exactly `N` (`array length mismatch: expected N, got M`).
Omit the field entirely to keep the default; a partial array is an error, not a
silently padded result.
