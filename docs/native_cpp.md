# ARK C++ runtime

The conversion keeps the frozen ARKMessage JSON contract so that Python and C++
can be checked against the same tasks during the transition.

## Build and run

Install a C++20 compiler, CMake 3.24 or newer, pkg-config, and the development
packages for libcurl (7.85+), libxml2, OpenSSL, SQLite, and hiredis. CMake uses
system nlohmann/json when available and otherwise downloads the pinned release
archive. The API uses a pinned cpp-httplib release.

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

The native router runs the Research, Coding, and Evaluation Agents. The Redis
bus, registry, all four memory tiers, and the REST and WebSocket API have
native implementations. The research worker uses abstracts and metadata.

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

## Day 25 coding

`ark-cpp code <instruction>` uses the local coding model from the active models
file. Override it with `ARK_CODE_MODEL`. The worker returns Python source and
unittest files in its ARKMessage. Run `ark-cpp code < message.json` to supply
trusted `payload.test_files`. Those files stay fixed throughout the repair
loop. The C++ worker sends actual Docker test output back to the model and
attempts at most three repairs. The Docker integration test injects a subtraction
bug, observes the failed test, and verifies that the repair passes the same test.

## Day 26 evaluation

The evaluator applies the guide's research, code, security report, hypothesis,
and data rubrics, plus a general fallback. It uses a local model through
`ARK_EVAL_MODEL`, which defaults to the general model. Tests and syntax scores
in a routed code task come from Docker execution. The model judges style and
security. A score below 0.65 requests a retry; scores from 0.65 to below 0.80
pass with a warning. `ark-cpp run` adds an evaluation task to the plan.

For a standalone evaluation, submit an ARKMessage with receiver
`evaluation_agent`, intent `EVALUATE`, and an `artifact` and `output_type` in
the payload via `ark-cpp evaluate < message.json`.

## Native REST and WebSocket API

Start the server with `build/ark-cpp serve 8000`. It binds to loopback and
uses the same workflow as `ark-cpp run`. Redis and the episodic SQLite store
are enabled by default for API jobs. Set `ARK_REDIS_PORT`, `ARK_REDIS_DB`,
or `ARK_EPISODIC_DB` to select local storage. Task responses stay in the
server's in-memory history. Persistent outcomes remain in the memory stores.

```bash
curl http://127.0.0.1:8000/task -H 'Content-Type: application/json' \\
  -d '{"goal":"Summarize arXiv:1706.03762","privacy":true}'
```

Poll `GET /task/{task_id}` or subscribe to `/ws` for task events. The API
also exposes agent records, version history, all four memory tiers, and
privacy settings. `DELETE /task/{task_id}` cancels a queued job. A running
job stays in the workflow until it reaches a terminal result. Set `test_files`
in the request body to give coding jobs a fixed set of tests.

## Day 27 live verification

Build with `-DARK_ENABLE_LIVE_MVP=ON`, then run the `live` label with local
Ollama, Qdrant, Docker, and a Redis instance dedicated to testing. The test
uses Redis database 15 and writes inspectable artifacts under `build/mvp-*`.
Set `ARK_MODEL` and `ARK_CODE_MODEL` to installed models and set
`ARK_REDIS_PORT` to the test instance's port.

```bash
cmake -S . -B build -DARK_ENABLE_LIVE_MVP=ON
cmake --build build --parallel
ARK_REDIS_PORT=16379 ctest --test-dir build -L live --output-on-failure
```

The guided Fibonacci scenario names arXiv paper 1803.07199 and supplies the
fast-doubling recurrence and a fixed contract suite. The runtime still
classifies, builds the DAG, reads the paper, writes code, evaluates it, and
persists the outcome. A second test suite runs independently in Docker.

## Day 29 retries and escalation

The supervisor gives a dispatched plan three rounds, including the initial
attempt. Goal preparation also gets three attempts for temporary service
failures. During dispatch, temporary HTTP failures retry the affected worker and unblock its
dependents when it succeeds. A low evaluation score sends the evaluator's
feedback to the artifact's producer and reruns downstream tasks with the
revised result. Completed independent work is reused. The coding worker
keeps its fixed tests across a revision. Invalid inputs and privacy denials
are terminal failures.

The final result contains `escalations` for unresolved worker failures,
including the plan task ID, attempt count, and last failure. The supervisor
records the report in episodic SQLite. Dispatch also adds a Redis audit event
when Redis is configured. The API emits `task_escalated` on `/ws` before
`task_finished`. Routed ARKMessages count supervisor retries in
`retry_count`. Coding's in-attempt repairs remain in `output.repairs`.
