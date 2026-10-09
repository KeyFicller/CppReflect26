"""LLM clients over the C++ reflection demo. Two modes, one entry point.

The C++ binary owns the contract in both directions; Python never redeclares a
signature:

  tools       --emit-tools        -> the OpenAI tool schemas the model is given
              --call <name>       <- tool-call arguments, real C++ call out
  structured  --emit-schemas      -> OpenAI tool wrappers for registered structs
              --parse <name>      <- the model's JSON, reconstructed struct out

Usage:
  python llm_client.py tools      [question]
  python llm_client.py structured [request] [format]

Positional order is strict: the first argument is always the request text, the
second always the format (registered struct name, default "SearchRequest").
Pass both or neither, so a lone argument is rejected rather than silently taken
as either slot. To target a format, pass the request too, e.g.
`structured "find reflection docs" Query`.

Endpoint quirks this works around:
  * response_format "json_schema" is rejected ("unavailable now"), so a schema
    is sent as a tool/function schema instead, which does work here.
  * that path forces tool_choice, and the model runs in thinking mode by
    default, which refuses a forced tool_choice ("Thinking mode does not
    support this tool_choice"). reasoning_effort="none" clears it.
  * the server is not bound by "strict" even then, so conformance is proven on
    the C++ side, where parsing fails loudly on a bad enum or type.

Joint debugging: set CPP_REFLECT_SOCKET to a running `CppReflection --serve`
socket and run_cpp() talks to it instead of spawning a process, so a debugger
holding that process keeps its breakpoints across a whole LLM loop.
"""

import json
import os
import socket
import subprocess
import sys

from langchain_core.messages import HumanMessage, ToolMessage
from langchain_deepseek import ChatDeepSeek

ROOT = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(ROOT, "build", "CppReflection")
MODEL = "deepseek-flash"

DEFAULT_QUESTION = (
    "Find documents about C++26 static reflection using Fahrenheit, "
    "top 3 results, and require an exact match."
)
DEFAULT_REQUEST = (
    "I want exact matches for 'static reflection', in Fahrenheit, "
    "with results between 1 and 10 and above 20, matching loosely, "
    "weighted 0.75, and I need 3, 5 and 8 hits. No debug output."
)


def load_env(path: str) -> None:
    """Minimal .env loader, so python-dotenv is not a dependency."""
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def run_cpp(*args: str, stdin: dict | None = None) -> str:
    """Run one request and return its stdout text, raising on a non-zero exit.

    With CPP_REFLECT_SOCKET set, the request goes to an already-running `--serve`
    process over a Unix socket instead of a freshly spawned one. That process is
    launched by the debugger, which keeps its breakpoints live across the whole
    session; the channel has to be out of band, because stdin/stdout belong to
    the debugger.
    """
    address = os.environ.get("CPP_REFLECT_SOCKET")
    if address:
        return _run_over_socket(address, args, stdin)

    result = subprocess.run(
        [BINARY, *args],
        input=json.dumps(stdin) if stdin is not None else None,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "{} failed".format(" ".join(args)))
    return result.stdout


def _run_over_socket(address: str, args: tuple[str, ...], stdin: dict | None) -> str:
    """One request/response line against a `--serve` process."""
    request = json.dumps({"args": list(args), "stdin": stdin}) + "\n"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(address)
        sock.sendall(request.encode())
        with sock.makefile("r", encoding="utf-8") as stream:
            reply = json.loads(stream.readline())
    if "error" in reply:
        raise RuntimeError(reply["error"])
    return reply["stdout"]


def make_model(*, force_tool: bool = False) -> ChatDeepSeek:
    """A chat model wired for this endpoint.

    force_tool disables thinking mode. A forced tool_choice is otherwise rejected
    ("Thinking mode does not support this tool_choice"), and forcing one is what
    with_structured_output does. Plain tool calling forces nothing, so thinking
    can stay on and the model gets more room to decide.
    """
    return ChatDeepSeek(
        model=MODEL,
        temperature=0,
        **({"reasoning_effort": "none"} if force_tool else {}),
    )


# --- tools mode --------------------------------------------------------------


def emit_tools() -> list[dict]:
    """Ask the C++ binary for the tool schemas it exposes."""
    return json.loads(run_cpp("--emit-tools"))


def call_tool(name: str, arguments: dict) -> str:
    """Run one tool call in C++ with the model-supplied arguments."""
    return run_cpp("--call", name, stdin=arguments).strip()


def run_tools(question: str) -> int:
    tools = emit_tools()
    print("tools from C++: {}\n".format([t["function"]["name"] for t in tools]))

    model = make_model()
    model_with_tools = model.bind_tools(tools)

    messages = [HumanMessage(question)]
    reply = model_with_tools.invoke(messages)

    tool_calls = getattr(reply, "tool_calls", None) or []
    if not tool_calls:
        print("model answered without a tool call:\n", reply.content)
        return 0

    messages.append(reply)
    for call in tool_calls:
        print("model called {} with {}".format(call["name"], json.dumps(call["args"])))
        output = call_tool(call["name"], call["args"])
        print("C++ returned: {}\n".format(output))
        messages.append(ToolMessage(output, tool_call_id=call["id"]))

    final = model.invoke(messages)
    print("final answer:\n", final.content)
    return 0


# --- structured mode ---------------------------------------------------------


def emit_response_schemas() -> dict:
    """Every registered model's OpenAI tool wrapper, keyed by name."""
    return json.loads(run_cpp("--emit-schemas"))


def parse_model(name: str, payload: dict) -> str:
    """Hand the model's JSON to C++ and get the reconstructed struct back."""
    return run_cpp("--parse", name, stdin=payload).rstrip()


def run_structured(request: str, format: str) -> int:
    schemas = emit_response_schemas()
    if format not in schemas:
        print("unknown format {!r}; registered: {}".format(format, list(schemas)))
        return 1

    # The C++ side already emits a full OpenAI tool, so it is passed through
    # untouched; no wrapper is assembled here.
    tool = schemas[format]
    print("registered formats: {}".format(list(schemas)))
    print("tool schema for {} ({} bytes)\n".format(format, len(json.dumps(tool))))

    structured = make_model(force_tool=True).with_structured_output(
        tool, method="function_calling"
    )

    extracted = structured.invoke([HumanMessage(request)])
    print("model returned:")
    print(json.dumps(extracted, indent=2), "\n")

    print("parsed back into demo::{} by C++:".format(format))
    print(parse_model(format, extracted))
    return 0


# --- entry point -------------------------------------------------------------


def main() -> int:
    load_env(os.path.join(ROOT, ".env"))

    mode = sys.argv[1] if len(sys.argv) > 1 else "tools"
    rest = sys.argv[2:]

    if mode == "tools":
        return run_tools(rest[0] if rest else DEFAULT_QUESTION)
    if mode == "structured":
        # Strict positional order: request first, format second. Either pass
        # both or neither, so a lone argument can never be silently mistaken
        # for the other slot.
        if len(rest) not in (0, 2):
            print(
                "structured expects 0 or 2 arguments (got {}); "
                "usage: structured [request] [format]".format(len(rest))
            )
            return 2
        return run_structured(
            rest[0] if rest else DEFAULT_REQUEST,
            rest[1] if rest else "SearchRequest",
        )

    print("unknown mode {!r}; use 'tools' or 'structured'".format(mode))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
