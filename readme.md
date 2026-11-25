# ARK OS

ARK OS is a local-first agent runtime with a native C++20 application. It
includes research, coding, evaluation, memory, browser, sandbox, science, and
workflow components. The C++ runtime is the active implementation; the Python
tree remains in the repository as a migration reference and is not required to
build `ark-cpp`.

See the [day-tagged development history](CHANGELOG.md) for completed slices
and roadmap days without a standalone commit.

## Build

On Linux, install CMake 3.24+, a C++20 compiler, pkg-config, and development
packages for libcurl, libxml2, OpenSSL, SQLite, hiredis, libgit2, libzip,
Poppler C++, Cairo PDF, and PangoCairo. CMake uses system nlohmann/json when
available and otherwise fetches a pinned release. The API uses pinned
cpp-httplib source.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/ark-cpp --help
```

Full setup details and configuration are in the [native runtime guide](docs/native_cpp.md).
For a hands-on dashboard and API check, use the [local product test](docs/product_test.md).
The [architecture map](ARCHITECTURE.md) summarizes runtime components and
integration limits.
The project [Implementation Guide](Implementation%20Guide/ARK_OS_Daily_Build_Plan.md)
describes the longer roadmap.

## Run

For local model-backed research, run Ollama with cloud features disabled,
install a model listed by `build/ark-cpp models`, and start Qdrant on loopback.
Some workflows also use Redis, Neo4j, Docker, provider keys, or local audio and
Whisper tools. These services are optional for building and running the unit
tests, but required by the workflows that use them.
Explicit execution goals are routed through the Docker-isolated terminal
worker; model-backed planning and a working local Docker service are required.

```bash
OLLAMA_NO_CLOUD=1 ollama serve
# In another terminal, after starting Qdrant and installing the selected model:
build/ark-cpp research "Explain the Transformer architecture"
```

The stdio MCP server can be launched from clients that support MCP:

```json
{
  "mcpServers": {
    "ark-os": {
      "command": "/absolute/path/to/Ark/build/ark-cpp",
      "args": ["mcp"]
    }
  }
}
```

It currently exposes `ark_task`, `ark_research`, `ark_code`, `ark_security`,
and `ark_sage`. The local protocol handshake and tool listing have been smoke
tested. Client-specific registration and service-backed live tool calls have
not been verified; see the [Week 15 review](docs/week15_review.md).

Completed task results can be exported to a local Obsidian vault with
`build/ark-cpp export-obsidian <vault-directory> < result.json`. An explicit
`build/ark-cpp export-notion < result.json` call can create a child page when
`ARK_NOTION_TOKEN` and `ARK_NOTION_PARENT_PAGE_ID` are set. Notion export sends
the supplied result fields to Notion; no live workspace has been connected or
tested here.

## Current evidence and limits

The latest complete native CTest run reported 38 passed and 2 service-gated
skips out of 40 tests. This is not evidence that every external service or all
20 planned agents work together in one live task. See the [all-agent integration
audit](docs/day96_agent_integration.md), [benchmark methods and results](docs/papers/arkbench.md),
and [current project status](docs/project_status.md) for measured outcomes and
open work.

For unattended operation, see the [native deployment guide](docs/deployment_guide.md).

ARK OS is provided under the [MIT License](LICENSE). Contributions are covered
by [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).
