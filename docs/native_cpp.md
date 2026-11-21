# ARK C++ runtime

The conversion keeps the frozen ARKMessage JSON contract so that Python and C++
can be checked against the same tasks during the transition.

## Build and run

Install a C++20 compiler, CMake 3.24 or newer, pkg-config, and the development
packages for libcurl (7.85+), libxml2, OpenSSL, SQLite, and hiredis. CMake uses
system nlohmann/json when available and otherwise downloads the pinned release
archive.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run `build/ark-cpp models` to select a hardware tier and write
`config/active-models.json`. An optional GiB argument overrides VRAM detection.
For research, run Ollama locally with cloud features disabled, pull the model
from the active model file and `bge-large:latest`, and start Qdrant on the
loopback interface. The C++ process does not load a Python interpreter.

```bash
OLLAMA_NO_CLOUD=1 ollama serve
# In a separate terminal with Qdrant running:
ollama pull bge-large:latest
ARK_MODEL=qwen2.5:7b build/ark-cpp research "Explain the Transformer architecture"
# Or submit an existing pending message:
build/ark-cpp research < message.json
```

Use `ark-cpp classify <goal>` to inspect the local Supervisor's intent and
`ark-cpp plan <goal>` to print a validated task DAG. `ark-cpp run <goal>`
classifies, plans, and dispatches research goals. `ark-cpp dispatch < dag.json`
also accepts a saved plan. The router derives parallel batches from the
dependency edges and skips tasks whose prerequisites failed.
The dispatcher refreshes `STATE.md` after each worker finishes. Use
`ARK_STATE_PATH` to select a different snapshot file.

Set `ARK_REDIS_PORT` and `ARK_EPISODIC_DB` to persist native run metadata to
Redis and SQLite. Run `ark-cpp audit` and `ark-cpp episodes` with the same
settings to inspect it. Native working memory, pub/sub, registry hashes, and
episode rows use the existing Python key and column formats. The storage tests
always exercise SQLite restart persistence. Set `ARK_TEST_REDIS_PORT` to the
port of a Redis instance dedicated to tests to include live Redis checks.

`ark-cpp memory <query>` retrieves relevant abstracts and procedures. Use
`ark-cpp skill-add < skill.json` with an `intent`, a `description`, and a
`steps` string array. `ARK_PROCEDURAL_DB` selects the native skill database.
The native store uses an exact cosine scan with a bounded candidate heap and
weights the candidates by their success rate. SQLite keeps each skill and its
vector in one transaction. The previous Python FAISS index needs a re-embedding
step to migrate because the native runtime uses the Ollama encoder.

`ARK_MODEL` overrides the general model from the active model file.
`ARK_CONFIG_DIR`, `ARK_OLLAMA_URL`, `ARK_QDRANT_URL`, and
`ARK_EMBEDDING_MODEL` override the config directory, loopback service addresses,
and embedding model. `SEMANTIC_SCHOLAR_API_KEY` optionally authenticates the
fallback search. The worker uses a separate Qdrant collection per embedding
model to avoid mixing vectors from different engines. Paper UUIDs keep the
same mapping as the Python worker.

## SDK trial

Built AMD GAIA at `2a0e47ea6a420eeaa18f6870c32572eed0a9c928` with its TUI and
examples disabled. Its C++ client successfully requested a JSON completion
from the local Qwen3-4B model and returned two 1,024-dimensional embeddings
from the local BGE model. The source revision also exposes a bearer auth
setter.

ARK uses a small HTTP client and a fixed workflow for this worker. The workflow
batches the arXiv ID lookup, embeddings, and Qdrant upsert. It controls exactly
which external endpoints receive the query and verifies every citation against
retrieved evidence. GAIA would be useful to evaluate again for MCP and
open-ended tool calling. No inference speed advantage was measured for either
client.

## Port status

The Research Agent, its provider requests, the paper storage path, the local
Supervisor and Planner, and the DAG router are native. CTest exercises the
frozen message shape, Scholar fallback, citation checks, batched calls,
stable IDs, network restrictions, plan validation, and parallel dispatch. A live
run with arXiv, Qwen3-4B, BGE, and Qdrant returned a structured summary citing
`1706.03762v7`. Qdrant returned the stored abstract by its stable point ID.
The same topic also passed through `ark-cpp run` from classification and
planning to the cited research result.

The native router currently runs the Research Agent. The Redis bus, registry,
and all four memory tiers have native implementations. The API and milestones
beyond Day 24 still need native implementations before the Python reference can
be retired. The research worker uses abstracts and metadata.

## Day 24 sandbox

`ark-cpp sandbox` uses the local Docker Engine Unix socket. Pull
`python:3.11-slim` once before running it. Each job gets a fresh container,
non-root UID, read-only root, bounded writable temporary space, no host mounts,
and no network. The runner kills timed-out jobs and removes their containers.

```bash
docker pull python:3.11-slim
printf '%s\\n' '{"code":"print(2 + 2)","timeout_seconds":10}' | build/ark-cpp sandbox
```

The request can include a `files` object of relative paths and a `tests` flag
for unittest discovery. File staging stays in memory until the container starts.
Use `ARK_SANDBOX_IMAGE` to select a prepared image and `ARK_DOCKER_SOCKET`
for a different local socket. Package requirements use only wheels bundled
under `/wheels` in the image. Set `ARK_TEST_DOCKER_SOCKET` when running CTest to
verify the real timeout, host filesystem isolation, and unittest execution.
