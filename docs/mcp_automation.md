# Reflection → MCP automation (design)

**Protocol revision: [2026-07-28][rev] (Current).** Every claim below is checked
against that revision's changelog, lifecycle, tools, prompts, resources, and
transport pages.

The target is an **abstract MCP server class**: a base class that owns the
connection and the protocol, and derived classes that register tools, prompt
templates, and resources. This is a design note — no code yet.

[rev]: https://modelcontextprotocol.io/specification/2026-07-28/changelog
[mcp]: https://modelcontextprotocol.io/

## Revision note

An earlier draft targeted the *legacy* handshake model (`initialize` →
`notifications/initialized`, `ping`) that `2025-06-18` and earlier used. Revision
`2026-07-28` **removed** that connection-scoped session: MCP is now **stateless**,
version and capabilities travel in per-request `_meta`, and `ping` is gone. This
document is written against `2026-07-28` only.

## Verdict

Yes for the part that drifts; no for the part that is boilerplate — and that split
is the whole story.

Reflection already automates the **tool contract and its dispatch**: schema
generation and typed JSON→C++ decoding. Those are the tedious, silent-drift-prone
halves, and they transfer to MCP unchanged. What reflection does **not** generate
is the JSON-RPC 2.0 protocol itself — `id` pairing, version handling, the
`server/discover` result, error codes, framing. That layer is ordinary runtime
C++, and that is exactly what the base class should own so no derived class ever
rewrites it.

---

## The artifact: `mcp::Server`

One base class owns transport + protocol + dispatch. A derived class only
*declares* what it exposes — as **member functions of the server class itself**.

> **Revision (class-scope / CRTP).** The earlier draft scanned a *namespace* via
> `register_namespace<^^ns>()`. That is superseded: the markers now sit on
> **non-static member functions of the derived server class**, and the base
> constructor scans `Derived` itself through CRTP
> (`class SomeServer : public mcp::Server<SomeServer>`). There is no
> `register_namespace`, no separate namespace, and no registration call. Note the
> fork facts this rests on: `members_of(^^Class)` also yields constructors, the
> destructor and operators, so the scan filters on `is_function &&
> has_identifier`; and `parameters_of` on a non-static member function **excludes
> the implicit object parameter**, so the schema/prompt projections are unchanged
> — only the call form binds `this`.

```
mcp::Server                                  (base: connection, JSON-RPC, routing)
 ├─ serve(in, out)                           stdio and socket share this loop
 ├─ server/discover                          capabilities assembled from what is registered
 ├─ tools/list · tools/call                  <- derived-registered tools
 ├─ prompts/list · prompts/get               <- derived-registered prompt templates
 └─ resources/list · resources/read          <- derived-registered resources
        ▲
        │ markers [[= mcp::tool{}]] / [[= mcp::prompt{}]] / [[= mcp::resource{...}]]
        │ Server<Derived> ctor scans ^^Derived   (CRTP: one scan, no call)
        │ server_name() / server_version() / instructions()
        ▼
   SomeServer : public mcp::Server<SomeServer>  (one small class per server)
```

### Shape

`serve()` is the only public member. Everything the derived class uses to declare
what it exposes is `protected`.

```cpp
namespace mcp {

// Markers: a member function of the server class carrying one of these is
// registered as that kind. Plain markers carry no data, so tool/prompt are empty.
// A resource does carry data — its URI (or URI template) and MIME type — which
// reflection cannot invent, so its marker holds structural strings (the same
// js::str mechanism js::doc uses).
struct tool {};
struct prompt {};
template <std::size_t U, std::size_t M = 1>
struct resource { js::str<U> uri; js::str<M> mime_type; };

struct Tool {                                    // internal, one emitted table row
    std::string_view name, description;
    std::string_view input_schema;               // bare JSON Schema
    void* self;                                  // the server object to call on
    std::string (*run)(void*, const nlohmann::json&);  // invoke_member_entry<Fn, Der>
};

struct PromptArgument { std::string_view name, description; bool required; };
struct Prompt {
    std::string_view name, description;
    std::vector<PromptArgument> arguments;
    void* self;
    std::string (*render)(void*, const nlohmann::json&);  // args -> messages JSON
};

struct Resource {
    std::string_view uri, name, description, mime_type;
    void* self;
    std::string (*read)(void*, const nlohmann::json&);    // -> text body
};

template <class Derived>
class Server {
public:
    virtual ~Server() = default;

    /// The one public entry point: transport-agnostic, newline-delimited JSON-RPC.
    /// stdio passes std::cin/std::cout; a socket server can pass fd-backed streams.
    int serve(std::istream& _in, std::ostream& _out);

protected:
    /// The whole registration: scan the derived class and register every marked
    /// member. CRTP — no call, no separate namespace.
    Server() { register_members<^^Derived>(); }

    /// Identity and guidance, emitted by server/discover.
    virtual std::string_view server_name() const = 0;
    virtual std::string_view server_version() const = 0;
    virtual std::string_view instructions() const { return {}; }

private:
    std::string dispatch(const nlohmann::json& _request);   // method -> handler
    template <std::meta::info M, typename Self> void register_member(Self* _self);
    template <std::meta::info Class>            void register_members();
    std::vector<Tool> m_tools;
    std::vector<Prompt> m_prompts;
    std::vector<Resource> m_resources;
};

} // namespace mcp
```

A derived server simply declares its functions as members; the base constructor
registers them — there is no per-entity registration of any kind:

```cpp
class CppReflectServer : public mcp::Server<CppReflectServer> {
public:
    [[= mcp::tool{}]]
    [[= js::doc{ .text = js::str("Searches the document index.") } ]]
    [[= js::param_docs(js::str("The raw query."), /* … */) ]]
    std::string search_documents(Query q, int limit, bool exact_match,
                                 std::optional<Unit> unit);

    [[= mcp::prompt{}]]
    [[= js::doc{ .text = js::str("Explain a reflection concept.") } ]]
    [[= js::param_docs(js::str("The concept to explain.")) ]]
    std::string explain(std::string topic);          // body renders the messages

    [[= mcp::resource{ .uri = js::str("file:///docs/reflection_bridge.md"),
                       .mime_type = js::str("text/markdown") }]]
    [[= js::doc{ .text = js::str("The C++/Python bridge notes.") } ]]
    std::string bridge_doc();                        // no params -> static resource

protected:
    std::string_view server_name()    const override { return "cppreflect"; }
    std::string_view server_version() const override { return "1.0.0"; }
};
```

Adding a tool, a prompt, or a resource is "write the function, mark it, done" — no
registration line, no macro, no lambda to hand-wire, no static initializer.

### The scan

```cpp
// The trampoline binds the object: a pointer-to-member cannot be formed from a
// runtime reflection, but it can from a template instantiation, so the registry
// stores `&invoke_member_entry<Fn, Self>` plus `self`. One form serves all three
// kinds, because a tool, a prompt and a resource all return std::string.
template <std::meta::info Fn, typename Self>
std::string invoke_member_entry(void* _self, const nlohmann::json& _j)
{
    return js::invoke_member_with_json<Fn>(*static_cast<Self*>(_self), _j);
}

// A marker may be a plain type (tool/prompt) or an instantiation (resource), so
// the test covers both — unlike js::find_annotation, which assumes a template.
template <meta::info Tmpl>
consteval bool has_annotation(meta::info _e)
{
    for (meta::info a : std::meta::annotations_of(_e)) {
        const meta::info t = std::meta::remove_const(std::meta::type_of(a));
        if (t == Tmpl ||
            (std::meta::has_template_arguments(t) && std::meta::template_of(t) == Tmpl)) {
            return true;
        }
    }
    return false;
}

template <std::meta::info M, typename Self>
void Server::register_member(Self* _self)
{
    // members_of(^^Class) also yields constructors, the destructor and operators;
    // those have no identifier and are skipped here.
    if constexpr (!std::meta::is_function(M) || !std::meta::has_identifier(M)) {
        return;
    } else if constexpr (has_annotation<^^mcp::tool>(M)) {
        m_tools.push_back(Tool{
            std::meta::identifier_of(M),
            std::define_static_string(js::description_of<M>()),  // [[= js::doc{...}]]
            js::function_schema<M, js::Style::JsonSchema>(),
            _self, &invoke_member_entry<M, Self> });
    } else if constexpr (has_annotation<^^mcp::prompt>(M)) {
        // The same scalar parameter model as a tool, projected as PromptArgument
        // instead of JSON Schema; prompt_arguments<M>() is a projection over the
        // existing param_count / param_at / param_description.
        m_prompts.push_back(Prompt{
            std::meta::identifier_of(M),
            std::define_static_string(js::description_of<M>()),
            prompt_arguments<M>(),               // {name, description, required} from params
            _self, &invoke_member_entry<M, Self> });
    } else if constexpr (has_annotation<^^mcp::resource>(M)) {
        // Only the URI and MIME come from the marker; name and description come
        // from reflection, exactly as for tools.
        constexpr auto anno = js::find_annotation<^^mcp::resource>(M);
        constexpr auto cfg = std::meta::extract<typename [: std::meta::type_of(anno) :]>(anno);
        m_resources.push_back(Resource{
            std::string_view(cfg.uri.data),
            std::string_view(cfg.mime_type.data),
            std::meta::identifier_of(M),
            std::define_static_string(js::description_of<M>()),
            _self, &invoke_member_entry<M, Self> });
    }
}

// members_of returns a heap-allocated vector<info>, which cannot be bound to a
// constexpr variable; define_static_array inside a consteval call is the
// persistent form (the same shape as test_entry::member_static_array).
template <std::meta::info Class>
consteval auto class_member_infos()
{
    return std::define_static_array(
        std::meta::members_of(Class, std::meta::access_context::unchecked()));
}

template <std::meta::info Class>
consteval std::size_t class_member_count()
{
    return std::meta::members_of(Class, std::meta::access_context::unchecked()).size();
}

// Unroll over indices: `template for` over a reflection range is the known-broken
// path on this fork, index_sequence is the proven one.
template <std::meta::info Class, typename Self, std::size_t... Is>
void Server::scan(Self* _self, std::index_sequence<Is...>)
{
    constexpr auto infos = class_member_infos<Class>();
    (register_member<infos[Is]>(_self), ...);
}

template <std::meta::info Class>
void Server::register_members()
{
    using Self = typename [: Class :];
    scan<Class>(static_cast<Self*>(this),
                std::make_index_sequence<class_member_count<Class>()>{});
}
```

**Verified.** Standalone probes confirm the CRTP pattern end to end:
`members_of(^^Derived, unchecked())` enumerates the class's member functions
(constructors, the destructor and operators included), `annotations_of` reads the
marker off each, `has_identifier` filters the special members out, and the scan
registers exactly the marked ones, in **declaration order**. Invoking a
non-static member through `&[: Fn :]` with the bound `self` works, and
`parameters_of` on a member function returns only the explicit parameters, so the
schema/prompt projections are unaffected. Two fork requirements remain and are
baked into the code above: the vector must be turned into a persistent array by
`define_static_array` *inside a `consteval` call*, and iteration must unroll over
an `index_sequence`.

### Division of labour

| The base does | The derived does |
|---|---|
| stdio/socket loop, framing, UTF-8 line handling | declare identity (name/version) |
| JSON-RPC parse, `id` pairing, error codes | mark member functions `[[= mcp::tool{}]]` / `[[= mcp::prompt{}]]` / `[[= mcp::resource{...}]]` |
| version check against `_meta`, `resultType` | inherit `mcp::Server<Self>` (CRTP) |
| assembling `capabilities` from what is registered | optional `instructions()` |
| the class scan + per-marker dispatch | — |
| `server/discover`, `*/list`, `*/call\|get\|read` routing | — |
| wrapping results in the `resultType`/`content` envelope | — |

Because the base assembles `capabilities` from the registered sets, a server that
registers no prompt simply never declares `prompts`. That is the automation.

### One parameter model, projected per surface

All three kinds are C++ functions, and reflection reads the same things off each —
the entity, its parameters, and its description:

| From one `^^fn` | Projection |
|---|---|
| `identifier_of` | tool / prompt / resource **name** |
| `js::doc` (function annotation) | **description**, for all three |
| parameter list (`param_count` / `param_at` / `param_description`) | tool `inputSchema`; prompt `arguments`; resource template variables |
| `invoke_member_with_json<Fn>` | the call itself (tool and prompt) |

**MCP does not use JSON Schema for prompts or resources.** JSON Schema appears only
for tools (`inputSchema` / `outputSchema`) and elicitation (`requestedSchema`);
prompts use `arguments: [{name, description?, required?}]` and resource templates
use RFC 6570 URI templates. But that is a difference in *wire shape*, not in
*authoring*: the parameter model is identical. A prompt argument is
`identifier_of`, `param_description`, and `!is_optional` over the very same
parameter reflections a tool uses — no new reflection, just a second projection of
one model.

Descriptions are the clearest case. `js::doc` and `js::param_docs` key on the
*entity*, not on its role, so `description_of<Fn>()` and `param_description<Fn,I>()`
already work for a prompt or a resource exactly as for a tool. Unification needs
nothing new there.

Two honest limits remain:

- **Prompt arguments are strings.** MCP's `PromptArgument` has no `type`; values are
  always strings. Prompt parameters should be constrained to string-like types (a
  `static_assert`), which is why a prompt can reuse `invoke_with_json` untouched.
- **Resource templates carry no argument descriptors** on the wire — only the
  `uriTemplate` string. A templated resource's parameters can *generate* the
  template's variables, but the protocol has nowhere to put their descriptions.
  Resources unify in authoring, not in wire descriptor.

The marker annotation *is* the registration, so there is no line to forget, and no
manual-schema overload to drift.

### A worked example

One namespace declares all three kinds. Each function carries a marker and,
optionally, `js::doc` / `js::param_docs` — the same description annotations a tool
already uses.

```cpp
class CppReflectServer : public mcp::Server<CppReflectServer> {
public:
    // A tool: parameters become a JSON Schema.
    [[= mcp::tool{}]]
    [[= js::doc{ .text = js::str("Searches the document index.") } ]]
    [[= js::param_docs(js::str("The raw query text."),
                       js::str("Maximum number of hits to return."),
                       js::str("Require an exact match."),
                       js::str("Optional unit filter.")) ]]
    std::string search_documents(std::string query, int limit, bool exact_match,
                                 std::optional<Unit> unit = std::nullopt);

    // A prompt: the same parameters become PromptArgument descriptors.
    [[= mcp::prompt{}]]
    [[= js::doc{ .text = js::str("Explain a reflection concept to a newcomer.") } ]]
    [[= js::param_docs(js::str("The concept to explain, e.g. \"template for\".")) ]]
    std::string explain(std::string topic);          // body returns the messages JSON

    // A resource: only the URI and MIME live in the marker; name and description
    // come from reflection, as for the others.
    [[= mcp::resource{ .uri = js::str("file:///docs/reflection_bridge.md"),
                       .mime_type = js::str("text/markdown") }]]
    [[= js::doc{ .text = js::str("The C++/Python bridge notes.") } ]]
    std::string bridge_doc();                        // body returns the text
};
```

The scan walks the class's marked members: `search_documents` → tool, `explain` →
prompt, `bridge_doc` → resource. The three wire forms it produces:

**Tool** (`inputSchema` from parameter types + `param_docs`):

```json
{ "name": "search_documents",
  "description": "Searches the document index.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "query":       { "type": "string",  "description": "The raw query text." },
      "limit":       { "type": "integer", "description": "Maximum number of hits to return." },
      "exact_match": { "type": "boolean", "description": "Require an exact match." },
      "unit":        { "anyOf": [ { "type": "string", "enum": ["Celsius", "Fahrenheit"] },
                                  { "type": "null" } ],
                       "description": "Optional unit filter." }
    },
    "required": ["query", "limit", "exact_match"]
  } }
```

**Prompt** (`arguments` — the *same* parameter reflections, projected without a type):

```json
{ "name": "explain",
  "description": "Explain a reflection concept to a newcomer.",
  "arguments": [
    { "name": "topic",
      "description": "The concept to explain, e.g. \"template for\".",
      "required": true }
  ] }
```

`name` is `identifier_of` on the parameter, `description` is `param_description`,
`required` is `!is_optional` — change `topic` to `std::optional<std::string>` and it
becomes `false` on its own.

**Resource** (URI/MIME from the marker, name/description from reflection):

```json
{ "uri": "file:///docs/reflection_bridge.md",
  "name": "bridge_doc",
  "description": "The C++/Python bridge notes.",
  "mimeType": "text/markdown" }
```

`resources/read { "uri": "file:///docs/reflection_bridge.md" }` puts
`bridge_doc()`'s return value in `contents[0].text`.

### The resource marker is a template

`mcp::resource` carries its URI and MIME as structural strings (`js::str<N>`, the
same mechanism `js::doc` uses), so — unlike the empty `mcp::tool` / `mcp::prompt`
markers — it is a **template** marker. Two consequences, both already handled:

- `has_annotation<Tmpl>` must match both a plain type (`tool`, `prompt`) and an
  instantiation (`resource<…>`), which the scan's `has_annotation` does explicitly.
- `register_resource<Fn>` reads the marker's fields with
  `annotation_of<^^mcp::resource, Fn>()` on the `consteval` side, where the
  structural strings are still available.

This is the decided design: the marker is the only place a resource can carry a
real URI and MIME. The alternative — deriving a URI from the function name by
convention — cannot express `file:///docs/…` or a MIME type, so it is rejected.

### Enabling refactor

The register templates and the scan must see the `js::` reflection helpers, which
live today in `src/implement_json_schema.cpp`, inside the `js` namespace, at
translation-unit scope. The shared header (`src/reflection_json.h`) must expose:

- `description_of<Fn>()` and `param_description<Fn,I>()` — the descriptions, reused
  by all three kinds;
- `param_count<Fn>()` / `param_at<Fn,I>()` / `is_optional` — the parameter model the
  projections read;
- `function_schema<Fn, Style>()` and `invoke_with_json<Fn>` — the tool projection
  and the call.

This is a **move**, not new code; the prompt/resource projections are thin wrappers
over what already exists.

---

## What MCP requires (2026-07-28)

### Transport

stdio: newline-delimited JSON-RPC 2.0, UTF-8, one message per line, no embedded
newlines. **stdout carries protocol messages only**; logs go to stderr. (The
Logging feature is deprecated in this revision, and the spec explicitly suggests
`stderr` for stdio servers.) Cancellation travels as
`notifications/cancelled`.

### Statelessness and request metadata

No handshake. Every request carries its metadata in `_meta`:

```json
"_meta": {
  "io.modelcontextprotocol/protocolVersion": "2026-07-28",
  "io.modelcontextprotocol/clientInfo": { "name": "…", "version": "…" },
  "io.modelcontextprotocol/clientCapabilities": { }
}
```

If the requested version is unsupported, the server replies with
`UnsupportedProtocolVersionError` (`-32022`) listing what it supports. On stdio the
body is the source of truth — no headers exist. Servers SHOULD identify themselves
in each result's `_meta["io.modelcontextprotocol/serverInfo"]`.

### `server/discover` (mandatory)

Servers **MUST** implement it. Result:

```json
{
  "resultType": "complete",
  "supportedVersions": ["2026-07-28"],
  "capabilities": { "tools": {}, "prompts": {}, "resources": {} },
  "_meta": { "io.modelcontextprotocol/serverInfo": { "name": "…", "version": "…" } },
  "instructions": "…",
  "ttlMs": 3600000,
  "cacheScope": "public"
}
```

### Tools

- `tools/list` → `{ resultType, tools: [{ name, title?, description?, inputSchema, annotations? }], nextCursor?, ttlMs, cacheScope }`.
- `tools/call` `{ name, arguments? }` → `{ resultType, content: [{ type:"text", text }], isError? }`.
- Capability: `"tools": { "listChanged"? }`.
- `inputSchema` defaults to JSON Schema 2020-12 and was **loosened** (SEP-2106) to
  allow any 2020-12 keywords.

### Prompts

- `prompts/list` → `{ resultType, prompts: [{ name, title?, description?, arguments?: [{ name, description?, required? }], icons? }], nextCursor?, ttlMs, cacheScope }`.
- `prompts/get` `{ name, arguments? }` → `{ resultType, description?, messages: [{ role: "user"|"assistant", content }] }`.
- Capability: `"prompts": { "listChanged"? }`.
- Errors: invalid prompt name or missing required argument → `-32602`; internal →
  `-32603`.

### Resources

- `resources/list` → `{ resultType, resources: [{ uri, name, title?, description?, mimeType?, size?, icons?, annotations? }], nextCursor?, ttlMs, cacheScope }`.
- `resources/templates/list` → `{ resultType, resourceTemplates: [{ uriTemplate, name, title?, description?, mimeType?, icons? }], … }` (optional).
- `resources/read` `{ uri }` → `{ resultType, contents: [{ uri, mimeType?, text? | blob? }], ttlMs, cacheScope }`.
- Capability: `"resources": { "listChanged"?, "subscribe"? }` (may be `{}`).
- Not found → `-32602` (`"Resource not found"`); internal → `-32603`. Servers
  **MUST NOT** return an empty `contents` array for a missing resource.

### Errors

| Condition | Mechanism |
|---|---|
| Unknown tool / prompt / resource, malformed request | **JSON-RPC error** (`-32602` for unknown names; `-32603` internal) |
| API failure, input validation, business logic | **Tool result** with `isError: true` (the model self-corrects) |

Error-code allocation: `-32000`–`-32019` implementation-defined, `-32020`–`-32099`
specification-reserved (`-32022` = unsupported version), and the standard JSON-RPC
`-326xx` range.

### Caching

`tools/list`, `prompts/list`, `resources/list`, `resources/templates/list`, and
`resources/read` are `CacheableResult`s and carry `ttlMs` + `cacheScope`
(`"public"` or `"private"`). `tools/list` SHOULD also be deterministically ordered
— the registry already is, which helps client and prompt caching.

## Reuse map

Everything below already exists in `src/implement_json_schema.cpp`, and is what the
register templates and the per-surface projections consume.

| MCP needs | Existing machinery | Gap |
|---|---|---|
| tool `inputSchema` | `js::function_schema<^^fn, Style::JsonSchema>()` | none — it *is* the bare parameter object |
| prompt `arguments` | `param_count` / `param_at` / `param_description` + `is_optional` | none — already the same reflections |
| resource template variables | parameter identifiers | none |
| `name` | `meta::identifier_of(Fn)` | none |
| `description` (all three) | `js::description_of<Fn>()` from `[[= js::doc{...}]]` | none — entity-keyed, not role-keyed |
| `arguments` → typed call | `js::invoke_with_json<^^fn>(json)` | none |
| deterministic order | registration order is stable | none |
| the entry type | `js::tool_entry` (OpenAI-wrapped today) | store neutral fields; see below |

## The neutral entry decision

The registry stores the OpenAI projection as canonical today:

```cpp
struct tool_entry { std::string_view name; std::string_view schema; std::string (*run)(const json&); };
```

MCP wants the *flat* row. `mcp::Tool` above **is** that neutral shape, so the two
collapse: store `name` + `description` + bare `input_schema` + `run`, and build
either wrapper on emit. `as_openai_tool(name, description, input_schema)` already
exists, so the OpenAI emitter stays a one-liner and `--emit-tools` output is
byte-for-byte unchanged.

### The table becomes class-owned

Today tools self-register into a process-global: `CPP_REFLECT_TOOL` runs a static
initializer that pushes into `js::tool_registry()`, and `--emit-tools` reads that
global. If the tools are scanned into a `Server` **instance** — the whole point of
the abstraction — then there is no ambient global to read, and the class owns its
own table. That is the consequence, made explicit:

- **`CPP_REFLECT_TOOL` and the global `js::tool_registry()` are retired.** No
  registration line, no static initializer, nothing to forget.
- **`--emit-tools` obtains the table from an instance**, not a global: it
  constructs the server (cheap — no transport starts) and projects `m_tools` as
  OpenAI tools. MCP `tools/list`, MCP `tools/call`, and `--emit-tools` then all read
  **one** table with two emitters, which is exactly the single source of truth.
- This means the concrete server class (or a factory for it) must be visible to the
  JSON-schema TU, so the class belongs in a header both can include.
- **The models registry stays.** `js::model_registry()` drives `--emit-schemas` /
  `--parse`, which serve the Python structured-output loop; MCP has no "models"
  concept, so it is orthogonal and untouched. Only the **tools** table moves into
  the class.

## Change points

Approximate; verify against the live file.

| Location | Change |
|---|---|
| `src/implement_json_schema.cpp` | extract `js::` schema gen + `from_json`/`to_json`/`invoke_with_json` into `src/reflection_json.h` (move only); keep only the `CPP_REFLECT_MODEL` calls for the example models; `--emit-tools`/`--call` read the server instance |
| `src/reflection_json.h` (new) | the extracted reflection schema + decoder; `CPP_REFLECT_TOOL` and `js::tool_registry()` removed, `CPP_REFLECT_MODEL` / `js::model_registry()` kept |
| `src/mcp.h` | `template <class Derived> class Server` (CRTP, header-only), the `tool` / `prompt` / `resource` markers, `Tool`/`Prompt`/`Resource`, the member scan, and the runtime `tools_as_openai_json()` / `call_tool()` emitters over the one table |
| `src/app_model.h` | the example domain *types* (`Range`/`Query`/`SearchRequest`), shared by the CLI TU and the server |
| `src/app_server.h` | the concrete `app::CppReflectServer : mcp::Server<CppReflectServer>`, with `search_documents` / `explain` / `design_notes` as marked member functions |
| `src/implement_mcp.cpp` | `test_entry::run_mcp()` and the MCP test entry point |
| `src/test_entry.h` | declare `run_mcp()` and `implement_mcp()` |
| `main.cpp` | route `--mcp` **before** the `--*` catch-all, exactly like `--serve` |

The scan rests on `std::meta::members_of(^^Class, …)`, which is **probed and
confirmed**: it enumerates class member functions (including the special members,
which the scan filters out by `has_identifier`), the marker annotation is readable,
and the registered order is declaration order. The one caveat to carry is the
fork's iteration style — the `index_sequence` unroll shown in *The scan*, not
`template for`. Because the base is now a CRTP template, its definitions are
header-only (`src/mcp.cpp` is gone).

`--mcp` must be special-cased in `main()` ahead of the generic `--` dispatch, or it
would fall through to `run_request` and throw `unknown command: --mcp`.

This is a **different transport** from the existing `--serve`: that one is a Unix
socket carrying `{"args":[...],"stdin":...}` purely for joint debugging, and it
stays as is. MCP stdio is its own loop.

## Staged slice

The abstraction is built once; the capabilities can land in stages, each
independently testable against an off-the-shelf MCP client (MCP Inspector, a
current Claude client):

1. **Base + `server/discover` + tools** (`[[= mcp::tool{}]]` on members of `mcp::Server<Self>`). ~150–200 lines.
   Acceptance: client discovers, lists `search_documents`, calls it, gets a text
   block back.
2. **Prompts**: `[[= mcp::prompt{}]]` + `prompts/list`/`prompts/get` (argument
   descriptors projected from the parameter model).
3. **Resources**: `[[= mcp::resource{...}]]` + `resources/list`/`resources/read`
   (static; `resources/templates/list` optional later).

## Constraints and failure modes

- **stdout purity.** The `--mcp` path may call `log_err()` (stderr) only, never
  `log()` (stdout). MCP and the deprecation of the Logging feature make this a hard
  rule rather than a convenience.
- **`std::meta::info` is consteval-only** (see `reflection_bridge.md`), so
  `mcp::Tool` stores `string_view` + a function pointer. Two fork mechanics fall
  out and are mandatory: `members_of`'s `vector<info>` must be `define_static_array`'d
  inside a `consteval` call, and reflection ranges must be unrolled over an
  `index_sequence` (`template for` does not work here).
- **Non-static members, so CRTP.** The scan reflects the *derived* type, so the
  base must know it: `mcp::Server<Derived>` (CRTP). The base constructor runs the
  scan, so registration is zero-boilerplate. Invocation stores the object (`void*`)
  plus a thunk `invoke_member_entry<Fn, Self>`; a bare `std::function` per entry is
  deliberately avoided. Member bodies may read/write server state, but they are
  therefore bound to a `Server` instance and not standalone-reusable like free
  functions would be — the accepted trade-off.
- **Class-scoped, declared-order.** P2996 has no whole-program function list, so
  the class is the unit of declaration; "automatic" means one scan of `Derived`,
  not zero configuration. `members_of(^^Class)` also returns constructors, the
  destructor and operators (and non-functions), filtered out by
  `is_function && has_identifier`.
- **Error provenance.** Unknown name → JSON-RPC `-32602`; a handler that throws →
  `isError: true`. Two mechanisms, not one.
- **Content block type.** Tool and resource handlers return `std::string`; reflection
  cannot know whether that is `text` or base64 `image`, so it defaults to `text`.
  Prompts are different: their function returns the messages payload, a distinct
  return convention.
- **Single page.** No `nextCursor`; the registries are small.

## Out of scope

`annotations`, `title`, `icons`, `structuredContent`/`outputSchema`,
`subscriptions/listen` (and therefore `listChanged`), resource subscriptions,
`resources/templates/list` (URI-template matching — static resources are in scope),
the Tasks extension, Streamable HTTP, auth, and Multi Round-Trip Requests
(`input_required`).

## Decisions

Settled while designing this; recorded so the rationale survives.

- **No non-function escape hatch.** Authoring stays uniform: a marked *member
  function of the server class* is the only way to declare a tool, prompt, or
  resource. Two registration paths would reintroduce the drift this design
  removes. A real case that needs it is the trigger to revisit, not speculation.
- **Class-scope (CRTP) over a namespace scan.** The markers sit on the server
  class's own member functions, so there is no separate namespace and the base
  constructor scans `Derived` itself. Chosen over the earlier
  `register_namespace<^^ns>()`: the declaration site and the server are one, and
  registration needs no line at all.
- **Modern-only protocol.** Advertise `2026-07-28` and nothing else. Honest about
  what is tested; a legacy client gets `UnsupportedProtocolVersionError` rather than
  a half-working handshake path that would double the surface.
- **The tool table is class-owned (single source of truth).** Because tools are
  scanned into a `Server` instance, the class manages its own table; the global
  `js::tool_registry()` + `CPP_REFLECT_TOOL` is retired, and `--emit-tools` reads the
  instance's table instead of a global. See *The table becomes class-owned*.
- **Resource URI lives in the marker** (`mcp::resource` is a template marker); a
  name-derived URI cannot express `file:///…` or a MIME type.
