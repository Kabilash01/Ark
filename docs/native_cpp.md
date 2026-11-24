# ARK C++ runtime

The conversion keeps the frozen ARKMessage JSON contract so that Python and C++
can be checked against the same tasks during the transition.

## Build and run

Install a C++20 compiler, CMake 3.24 or newer, pkg-config, and the development
packages for libcurl (7.85+), libxml2, OpenSSL, SQLite, hiredis, libgit2,
libzip, Poppler C++, Cairo PDF, and PangoCairo. CMake uses
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

## Day 66 research worker balancing

Research dispatch uses two process-local worker slots. Each slot tracks its
load count and mean evaluator score. A free slot with a score below `0.65` is
deprioritized while a slot without evaluation history remains eligible. Ties
favor the slot with fewer lifetime assignments. Evaluation results update the
slot associated with the completed research result. This is a lightweight
in-process routing policy, not a claim that two separate model servers or
hardware workers are running. The native test dispatches two independent
research workers concurrently and checks assignment balance, score-based
selection, and slot release after exceptions.

## Day 67 conflict telemetry

`ark-cpp conflict-report [report.json]` summarizes sanitized conflict records
from the last 30 days. The SQLite journal stores only a pseudonymous case ID,
type, outcome, observation timestamp, and optional elapsed time. It omits task
prompts, outputs, source code, and conflict explanations. Resource records
measure time waiting on a saturated browser, terminal, or arXiv semaphore;
priority records measure queue time to selection; output records measure
reconciliation through resolution or escalation; procedural memory records
measure review time where an existing timestamp is available. Repeated updates
to one case count as one case with its latest outcome.

The committed `research/conflict_data/conflict_log.json` is an honest initial
capture with zero production observations. Earlier runs did not retain a
complete month of categorized conflict telemetry, so this report cannot
backfill or claim month-long empirical frequencies. Native tests exercise the
journal, deduplication, privacy filtering, and resource-wait observer with
controlled fixtures; those fixtures are not reported as production outcomes.

## Day 68 concurrency stress

`benchmarks/performance/day68_stress.json` records a 20-task concurrent wave
against the runtime browser, terminal, and arXiv semaphores, plus a 20-task
serial reference. The concurrent wave peaked at 1, 2, and 3 active users for
those resources, respectively, matching their configured capacities. All 20
parallel SQLite episode writes were retained, and their deterministic quality
scores matched the serial reference. This harness exercises semaphore and
SQLite synchronization; it does not measure concurrent model inference or
external browser and container throughput.

## Day 69 ARKBench v1.0

`benchmarks/arkbench/benchmark_v1.0.json` preserves the 30 v0.5 cases and adds
10 dependency-graph coordination cases and 10 mixed terminal, security, and
document workflows. The coordination and mixed cases run through the native
kernel DAG dispatcher. Their declared bindings can copy only the terminal
stdout or a matched local advisory ID into an existing document paragraph;
they cannot alter a command, URL, or model prompt. Each result records the
coordinator version and the versions of the participating tool agents.

The combined report at `benchmarks/arkbench/results/month4_final.json` contains
50 scored cases. It records the original 30-case run and the separately
executed 20 new workflow cases. The first ten matched research and coding IDs
scored 0.438 against 0.4035 in the Month 2 baseline, an 8.55% improvement. The
50-case mean spans deterministic tool checks and model-graded research and
coding, so use the matched original cohort for the Month 2 comparison.

## Day 70 Month 4 closeout

The implementation and verification evidence are summarized in
[`docs/month4_results.md`](month4_results.md). The architecture draft now
describes the rated research slots, bounded resource dispatch, and the new
tool-workflow DAG path. Conflict telemetry is ready for continued collection,
but the initial capture has no historical observations and does not support
claims about real-world conflict frequencies.

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

## Day 30 agent versioning

With Redis enabled, the C++ runtime saves the built-in prompts and configured
model the first time it initializes each agent. The API enables Redis by default.
For CLI execution, set `ARK_REDIS_PORT` and use the same `ARK_REDIS_DB` for
workflow and version-management commands. CLI execution without Redis uses the
built-in prompts and the configured models directly. With Redis, change a saved
model through a version bump. Each dispatched worker uses the same snapshot
throughout a run, including retries.

```bash
export ARK_REDIS_PORT=6379
build/ark-cpp version-bump evaluation_agent patch change.json
build/ark-cpp versions evaluation_agent
build/ark-cpp version-compare evaluation_agent 1.0.0 1.0.1
build/ark-cpp version-rollback evaluation_agent 1.0.0
```

`change.json` requires `change_summary`. Supply any of `prompts`, `model`,
`benchmark_score` (0 to 1), and `changed_by` (defaults to `manual`). The
`prompts` object replaces the named prompts you supply. Use `system` for
`coding_agent`, `evaluation_agent`, `planner_agent`, and `supervisor_agent`.
`research_agent` uses `query` and `synthesis`. Changes become active on a
successful `patch`, `minor`, or `major` bump.

`GET /agents/{name}/versions` and `ark-cpp versions` show revision metadata
including the prompt hash. `version-compare` returns the second version's
benchmark score minus the first when both have scores. `version-rollback`
restores the saved prompts and model for subsequent runs and writes an audit
event. Older metadata remains visible in version history. Rollback requires a
saved snapshot, and the first snapshot advances past any existing revision
number in that history. Routed worker messages and episodic SQLite records
carry the version used for that task.

## Day 31 task reflection

`run`, `dispatch`, and API jobs review the task outcome after execution. The
Reflection Agent receives bounded attempt summaries, scores, escalations,
and agent versions. It uses the local Ollama general model by default. Set
`ARK_REFLECTION_MODEL` for its first Redis revision or for runs without Redis.
An existing revision can be updated with `version-bump reflection_agent`.

The task result includes `reflection`. Each review is also saved to the
episodic database. When a successful task scores at least 0.80 and the review
identifies a reusable procedure, the runtime saves it to `ARK_PROCEDURAL_DB`.
`ark-cpp reflection-queue` lists pending version recommendations from
`ARK_EPISODIC_DB`. A reflection failure is reported alongside the task outcome.

## Day 32 task priority

`POST /task` accepts an integer `priority` (default `5`) and a `domain`
(default `general`). When jobs compete for the next API worker slot, the
highest priority runs first. Equal priorities follow the earlier timestamp.
Requests that share a timestamp rotate through domains in alphabetical order
and retain arrival order within each domain. Work already running continues
to completion. Each scheduling conflict emits `conflict_detected` on `/ws`
and is recorded in the Redis audit log.

## Day 33 privacy and airgap

```bash
build/ark-cpp --privacy "Write a local data parser"
build/ark-cpp run --airgap "Write a local data parser"
```

API clients can set `privacy` or `airgap_mode` on `POST /task`. Requests
can enable stricter settings than `config/privacy_config.json`. Airgap also
enables privacy. The effective mode reaches workers and is recorded in
`STATE.md`.

Supervisor, Planner, Evaluation, and Reflection use loopback model endpoints.
Privacy limits outside literature requests to the configured provider allowlist.
Airgap blocks all external literature requests. Local Docker, model, and memory
services are available in either mode.

## Day 34 web search and file operations

`build/ark-cpp web-search "query"` searches via Tavily using
`TAVILY_API_KEY`. The local general model summarizes the returned excerpts.
Citation IDs resolve to the provider's actual result URLs. The supervisor
uses this worker for `BROWSE` goals with no specified site. Airgap blocks the request before it
reaches Tavily. Private requests require `api.tavily.com` in the configured
allowlist. [Tavily Search API](https://docs.tavily.com/documentation/api-reference/endpoint/search)

Set `allowed_directories` in `config/file_system.json` or the equivalent
file under `ARK_CONFIG_DIR`. Map each workspace name to an absolute directory.
The default configuration grants access to no directories.

```json
{"allowed_directories":{"notes":"/absolute/path/to/notes"}}
```

```bash
printf '%s\n' '{"root":"notes","operation":"list"}' | build/ark-cpp files
printf '%s\n' '{"root":"notes","operation":"read","path":"index.md"}' | build/ark-cpp files
printf '%s\n' '{"root":"notes","operation":"write","path":"new.md","content":"# Notes"}' | build/ark-cpp files
```

File paths are relative to the named workspace. Reads and writes are limited
to 1 MiB of UTF-8 text. Set `overwrite` explicitly to replace an existing
regular file. The worker writes file-operation events to episodic SQLite
with the workspace, path, and byte count.

## Day 36 API verification

The C++ server implements the guide's task submission, lookup, listing,
and cancellation routes, along with agent revisions, memory, privacy
configuration, and WebSocket events. `GET /memory/{tier}` takes `limit`.
The working tier takes `task_id`. The semantic and procedural tiers list
stored records by default and rank matches when `query` is supplied.
`POST /memory/search` accepts a nonblank `query` and an integer `top_k`
from 1 to 50.

`POST /config/privacy` persists `privacy_mode` and accepts an optional
`airgap_mode`. In a fresh configuration directory, the first update creates
the settings file. A native API run verified the toggle with Redis, Qdrant,
SQLite, Docker, and loopback model fixtures.

## Day 37 completion conditions and live events

```bash
build/ark-cpp --until 'all_tests_pass AND quality_score > 0.85' --max-iter 4 \
  'Implement a parser with unit tests'
```

API clients can send `until` and `max_iter` with `POST /task`. The default
limit is 10 iterations. A successful workflow stops at the first iteration
that meets the condition. The runtime records the measured values and condition
status in `STATE.md` and the task result. An exhausted run returns `FAILED`
with the final iteration's results.

Conditions use comparisons, Boolean operators, and parentheses. The current
workers measure quality, test outcomes, citation count, and repair success
when the relevant evidence exists. A comparison against an unavailable
metric stays unresolved. Coverage requires a coverage measurement from the
sandbox. Novelty, feasibility, and security findings require their respective
workers or evaluation evidence.

`status`, `list`, and `stop` use the loopback API at `ARK_API_PORT` (default
`8000`). `stop` cancels a queued task. `agents`, `logs`, `versions`, and
`memory show` read the configured storage directly.

`/ws` emits worker activity, tool calls and results, memory writes, task
completion, and priority conflicts. Agent activity shows the current step.
Version bumps and rollbacks also appear as `version_updated` events via Redis.

## Day 38 web dashboard

Start `build/ark-cpp serve 8000` and open `http://127.0.0.1:8000/`.
The C++ server embeds the dashboard's HTML, CSS, and browser JavaScript at
build time. It serves both the page and the APIs on the same loopback origin.

The dashboard shows the 20 agent roles, registered version numbers, running
assignments, active and recent tasks, and the WebSocket event log. It supports
task submission, roster filters, and pausing the visible log. The registry
provides versions and idle or offline status. The API overlays active worker
assignments so new dashboard connections can see work already in progress.

## Day 39 dashboard inspection

Open a task in the queue to see its dependency layers, run log, output, and
JSON download. The API retains up to 160 events per recent task for the
lifetime of the server.

The Memory browser can browse and search all four tiers. Working memory
requires a task ID. Episodic search matches the summary. Semantic and
procedural search use the existing local embeddings.

Open an agent card to see its revision history, benchmark score changes,
and the active version. Rollback is available for saved prompt snapshots
and goes through the same versioner as the CLI, with a Redis audit event.

## Day 40 ARKBench v0.1

The [v0.1 suite](../benchmarks/arkbench/benchmark_v0.1.json) defines five
abstract-based research tasks and five coding tasks. Each case runs a fixed
two-step graph through the native dispatcher. It measures the selected
research or coding worker and its evaluator without varying the plan.

```bash
build/ark-cpp benchmark benchmarks/arkbench/benchmark_v0.1.json build/arkbench-report.json
```

The default suite path is relative to the repository root. The runner prints
the JSON report and, when a report path is supplied, saves progress after each
case. Each row includes the dataset task ID, producer version, all participating
agent versions, score, elapsed seconds, notes, and evidence. Research evidence
includes the actual arXiv revisions cited. Coding cases give the worker smoke
tests and run separate acceptance tests on the solution in Docker.

Missing required citations and failed coding tests score zero. When the runner
cannot obtain the evidence or evaluation needed to score a case, its score is
`null` and the report retains the reason. The command exits nonzero if any case
is unscored. Running the full suite requires local models, Docker, Qdrant, and
access to arXiv. Use an isolated Redis database and separate paths for state
and SQLite stores when recording a baseline.

The [Month 2 review](month2_results.md) records the live baseline and its
verification evidence.

## Day 43 browser tool

Install Chromium and run `build/ark-cpp browser < actions.json` from the
repository root. `ARK_BROWSER_BINARY` selects a compatible Chromium
executable. The C++ tool uses Chromium's DevTools pipe and a temporary browser
profile. The profile closes when the batch exits. [Chrome DevTools Protocol](https://chromedevtools.github.io/devtools-protocol/)

```json
{
  "timeout_ms": 10000,
  "actions": [
    {"action": "navigate", "url": "https://arxiv.org/abs/1706.03762"},
    {"action": "wait_for_element", "selector": "h1"},
    {"action": "get_page_text"},
    {"action": "extract_links"},
    {"action": "screenshot"}
  ]
}
```

Each action returns a result. `get_page_html` returns the current DOM as HTML.
`click`, `type_text`, `wait_for_element`, and `extract_table` take CSS
selectors. `type_text` inserts text at the cursor. `fill_form` takes a
`fields` object that maps selectors to string values or Boolean checkbox and
radio values. Screenshots return a base64-encoded PNG.

```bash
build/ark-cpp browser < actions.json | jq -r '.results[] | select(.action == "screenshot") | .result.data' | base64 -d > page.png
```

Set `privacy` or `airgap_mode` on the batch to apply the corresponding policy.
Private browsing permits loopback and exact hosts from `allowed_external`.
Airgap permits loopback only. The Chromium proxy policy applies to redirects
and page resources as well. [Chromium proxy rules](https://chromium.googlesource.com/chromium/src/+/HEAD/net/docs/proxy.md)

Set `ARK_TEST_CHROMIUM` to the executable path when running CTest to include
the headless Chromium integration checks.

## Day 44 browser agent

```bash
build/ark-cpp browse 'Browse https://arxiv.org/list/cs.CL/recent and extract the first three paper titles.'
```

The agent uses the general model from `config/active-models.json` or the
`ARK_MODEL` override. A saved Redis version pins the model and prompt for the
`browser_agent` role. Explicit browse goals also route through the normal
task dispatcher and API.

The local model chooses each action after seeing the previous result and
the visible page structure. The agent allows up to eight actions and limits
Chromium to the requested host. It returns the extracted text, links, or
table cells with their page URL, an action log, and up to two screenshots as
base64 PNG data. A single explicit count such as “the first three paper titles”
sets the extraction limit and requires that many results before completion.

Use `--privacy` or `--airgap` before the instruction to apply the same
privacy policy as the Browser Tool. The requested host must also be allowed
by that policy. [arXiv recent submissions](https://arxiv.org/list/cs.CL/recent)

## Day 45 terminal agent

```bash
build/ark-cpp terminal 'printf "%s\n" "hello from the sandbox"'
printf '%s\n' '{"command":"sleep 5","timeout_seconds":1}' | build/ark-cpp terminal
```

The agent runs the supplied Bash command in a fresh container using the
[Day 24 sandbox](#day-24-sandbox). It uses the same Docker socket and image
settings. A custom image must provide `/bin/bash`. The timeout defaults to 30
seconds and accepts whole seconds from 1 through 180 in the JSON request.

The result contains `stdout`, `stderr`, `exit_code`, and `timed_out`. If a
command fails with `ModuleNotFoundError` on stderr, the result adds the missing
import and a suggested pip command for review. Command execution and error
parsing require no model. The CLI exits with 0 on success, 124 on timeout,
and 1 on other failures. Set `ARK_TEST_DOCKER_SOCKET` to test the Terminal Agent
against a local Docker Engine.

## Day 46 tool resource limits

Each ARK runtime process shares one Chromium session slot, two Docker job
slots, and three arXiv API slots. Requests for the same tool wait in arrival
order when its slots are full. Browser closure, container cleanup, and request
completion release their slots, including on errors. arXiv requests still
follow the runtime's paced, serial request policy.

## Day 47 GitHub agent

The native build uses libgit2 for local Git operations. Run
`build/ark-cpp github < request.json` with one of these JSON actions.

| `action` | Fields |
| --- | --- |
| `clone_repo` | `repository` (`owner/name`, or `local_source` for a local repository), new `path` |
| `list_changed_files` | `path` |
| `create_branch` | `path`, `branch`, optional `checkout` (defaults to true) |
| `commit_files` | `path`, `files` array of worktree paths, `message` |
| `push_branch` | `path`, `branch` |
| `get_open_issues` | `repository`, optional `page` |
| `create_pull_request` | `repository`, `title`, `head`, `base`, optional `body` and `draft` |

Commits use the local Git author identity and require a clean staging area.
Each push updates the named branch on `origin` (GitHub HTTPS or a local bare
repository). Set `GH_TOKEN` or
`GITHUB_TOKEN` with the repository permissions required for private access
and pull request creation. [GitHub REST pulls](https://docs.github.com/en/rest/pulls/pulls)

## Day 50 document agent

Use `build/ark-cpp document < request.json`. All actions take a local `path`.
`read_pdf` returns page text, `read_docx` returns paragraphs, and `read_xlsx`
returns worksheet names and cell rows. PDF extraction uses the embedded text
layer. XLSX reads return cell values and cached formula results.

`write_docx` and `write_pdf` take a `paragraphs` array of strings and an
optional `title`. `write_xlsx` takes a `rows` array of cell arrays and creates
one worksheet. Strings, numbers, Booleans, and null are accepted. The caller
can replace an existing output file by naming its path explicitly.

```bash
printf '%s\n' '{"action":"write_docx","path":"build/report.docx","title":"Report","paragraphs":["Findings from the collected evidence."]}' | build/ark-cpp document
```

## Day 51 security agent

```bash
docker build -t ark-security:local docker/security
build/ark-cpp security < audit-request.json
```

Supply a `files` object of relative Python file names and source strings.
The default `audit` action runs Semgrep's bundled SQL rule and Bandit's
Python checks inside network-disabled Docker containers. Set `report_path`
to write HTML alongside the structured result. `run_semgrep` and `run_bandit`
select one scanner. `ARK_SECURITY_IMAGE` selects a prepared scanner image.

The dependency check compares the supplied `dependencies` object of pinned
versions with local `advisories` entries containing `id`, `package`, and
`affected_versions`. Use `check_dependencies` for that check alone. The report
maps the supported checks to the OWASP 2021 categories for injection,
vulnerable components, and authentication failures.

## Day 52 citation graph

Point `ARK_NEO4J_URL` at your local Neo4j HTTP endpoint (defaults to
`http://127.0.0.1:7474`). Use `ARK_NEO4J_USER`, `ARK_NEO4J_PASSWORD`, and
`ARK_NEO4J_DATABASE` for connection settings. Run `build/ark-cpp citation-graph`
with a JSON request on stdin. `initialize` creates the uniqueness constraints.

| `action` | Fields |
| --- | --- |
| `add_paper` | `id`, `title`, optional `concepts` array |
| `add_citation` | `from` and `to` paper IDs |
| `add_related` | `from` and `to` concept names |
| `list_citations` | source paper `id`, optional `limit` |
| `find_concept_gaps` | optional `limit`, ranks co-mentioned pairs without a direct relation |
| `find_bridge_concepts` | optional `limit`, ranks concepts connecting otherwise unlinked pairs |

The graph uses `Paper` and `Concept` nodes with `CITES`, `MENTIONS`, and
`RELATED` relationships. [Neo4j Query API](https://neo4j.com/docs/query-api/current/query/)

## Day 53 procedural memory conflicts

Skills with the same embedding model, intent, and description (ignoring ASCII
case and surrounding spaces) are treated as revisions of one procedure.
Storage preserves each skill ID, source, steps, and observed success rate.
Retrieval chooses the highest success rate within that group before ranking
it against other procedures. Ties favor the revision with more observations,
then the earliest entry.

Conflicting steps create a durable review item for the Reflection Agent.
Use `build/ark-cpp skill-conflicts` to list pending items and
`build/ark-cpp skill-conflicts <conflict-id>` to acknowledge an item. Review
status is stored separately from the skill revisions.

## Day 54 llama.cpp inference

Set `ARK_LLAMA_URL` to a loopback llama-server endpoint to route the native
agents' chat and embedding requests through llama.cpp. Use `ARK_LLAMA_API_KEY`
when the local server requires a bearer token. The legacy Ollama endpoint is
used when `ARK_LLAMA_URL` is unset.

The preset command resolves configured model names through Ollama manifests
to their GGUF weights and vision projector. Set `ARK_EMBEDDING_MODEL` to
include an embedding model. Model aliases must match the active model file
or the `ARK_MODEL`, `ARK_CODE_MODEL`, and `ARK_VISION_MODEL` overrides.

```bash
build/ark-cpp llama-presets "$OLLAMA_MODELS" > build/ark-models.ini
llama-server --models-preset build/ark-models.ini --models-max 1 --host 127.0.0.1 --port 8080
```

With one loaded model, the router loads the requested GGUF and releases the
previous model when roles change. `build/ark-cpp infer < request.json` takes
`role`, OpenAI-style `messages`, and optional `max_tokens`, `temperature`,
`schema`, and `stream`. A `model` field selects an explicit router alias.

```bash
ARK_LLAMA_URL=http://127.0.0.1:8080 build/ark-cpp inference-benchmark \
  benchmarks/coding_self_repair/cases.json build/inference-results.json
```

The comparison warms both services and sends each the same ten coding
prompts. Use `ARK_BENCH_OLLAMA_MODEL` when the same GGUF weights have a
different name in Ollama. It records generation tokens per second, latency,
and structured output for each engine. [llama-server reference](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)

On an RTX 5060 Laptop GPU, the tuned llama.cpp run averaged 183 ms per
prompt versus 265 ms for Ollama, about 31% lower latency. Generation throughput
averaged 280 versus 307 tokens per second, so the guide's throughput improvement
target remains unmet on this setup. All ten llama.cpp responses matched the
structured output contract. This measures generation, with the locked Docker
repair results recorded separately. [Benchmark results](../benchmarks/inference/results.json)

## Days 71–72 native Vision Agent

`build/ark-cpp vision` reads one JSON payload from standard input and analyzes
one PNG, JPEG, or WebP image (up to 10 MiB). The request has an `instruction`
and exactly one of `image_path` or embedded `image` data with `mime_type` and
base64 `data`. The active vision model comes from the configured model profile;
`ARK_VISION_MODEL` overrides it, and `ARK_LLAMA_URL` selects the llama.cpp
OpenAI-compatible endpoint.

```json
{"instruction":"Read the plotted years and approximate values.","image_path":"/tmp/figure.png"}
```

The response contains `summary`, `caption`, `extracted_text`, normalized
`chart_type`, up to 10 `{label,value,unit}` observations, and uncertainty notes.
Scientific-figure guidance covers line and bar plots, scatter plots, heatmaps,
and confusion matrices. Confusion-matrix observations preserve visible
row/column labels and cell values; the model is instructed not to derive
metrics from unclear cells. Embedded image bytes and local paths are removed
from the returned message.

The automated image tests use deterministic fixtures to check the Ollama and
llama.cpp image request formats, validation, chart output, caption structure,
and confusion-matrix normalization. They do not establish visual accuracy for
every model. A live check with the available small SmolVLM fallback identified
a bar chart and returned three observations but misread the values. The
configured Qwen2.5-VL model was not installed locally, so numeric extraction
accuracy with that model remains unverified. Do not treat estimated chart
values as ground truth without checking them against the source data.

## Day 86 native VLM runtime setup

The guide's Python/PyTorch bridge-training step is not added because the
project direction is C++ only. ARK's native `VisionAgent` already sends image
content to Ollama or llama.cpp; llama.cpp's multimodal runtime uses `libmtmd`
and supports Qwen2.5-VL GGUF models with image input. See the upstream
[multimodal guide](https://github.com/ggml-org/llama.cpp/blob/master/docs/multimodal.md).

`build/ark-cpp vision-check` reads the local llama-server `/props` endpoint and
reports the loaded model's declared image capability before a figure-analysis
request. It only accepts loopback HTTP and passes `ARK_LLAMA_API_KEY` as a
Bearer token when configured. To start a small Qwen2.5-VL runtime, use:

```bash
llama-server -hf ggml-org/Qwen2.5-VL-3B-Instruct-GGUF \
  --alias qwen2.5vl:3b --host 127.0.0.1 --port 8080
ARK_LLAMA_URL=http://127.0.0.1:8080 ARK_VISION_MODEL=qwen2.5vl:3b \
  build/ark-cpp vision-check
```

This runtime uses pretrained weights and does not train the custom SigLIP/Qwen
projection described in the original guide. No running local inference server or VLM
weights were installed during this setup pass, so an actual model load and
image inference remain unverified here; the new capability check is fixture
tested.

## Day 87 SAGE report citations and layout

The report generator accepts evidence references from the Hypothesis Engine,
filters them to the selected hypothesis's exact `paper_ids`, and writes those
titles and validated HTTPS source URLs into the bibliography. Unsafe URLs are
rejected before LaTeX generation. Figures use `[!htbp]` placement. Tests cover
graph citations, direct arXiv references, escaped text, URL rejection, and
section structure. `pdflatex` and `tectonic` are not installed here, and Day 84
has no live experiment artifact yet, so PDF compilation against a real SAGE
run remains unverified.

## Day 73 native ML Engineer

`build/ark-cpp ml-optimize` accepts a JSON object containing bounded numeric
`ranges`, a `target`, optional `trial_count` (1–1000), and an optional seed.
The command is a deterministic smoke objective that minimizes squared distance
from the target. The C++ `optimize_hyperparameters` API accepts an objective
callback, so native training code can score real model trials without a Python
runtime. The adaptive sampler is an ARK TPE-style implementation, not the
Optuna library; the name and output make this implementation explicit.

Set `MLFLOW_TRACKING_URI` to log the study through MLflow's REST API. It creates
or reuses a named experiment, records the trial objective and each numeric
hyperparameter as stepped metrics, records stable optimizer metadata as run
parameters, and marks the run finished. Tests use a fake tracking server to
verify request sequencing and payloads; a live MLflow service was not available
for this verification.

## Day 74 native experiment pipeline

Set `model` to `linear_regression`, provide `dataset.features` as numeric row
arrays and `dataset.targets`, and include a bounded `learning_rate` range.
Optional `l2` defaults to 0–0.1. The pipeline uses a seeded 80/20
train/validation split, trains only on training rows, selects trials by
validation RMSE, and returns coefficients, metrics, all trials, and plot-ready
validation history. It accepts up to 5,000 rows, 32 features, 100 trials, and
300 epochs. This first native pipeline supports linear regression; other model
families and CSV ingestion remain future extensions.

## Day 75 native DevOps Agent

`build/ark-cpp devops` accepts a JSON object with a `files` map (including a
Dockerfile) and a `command` string array. It sends the bounded tar build
context directly to the local Docker Engine API, disables networking for the
build and container, starts the image with no shell interpolation, drops Linux
capabilities, uses a read-only root filesystem and resource limits, then
removes the temporary container and image. Set `ARK_DOCKER_SOCKET` to select a
non-default local socket. This path is intended for generated project output;
it requires a preloaded base image because image builds have no network.

The Docker API fixture checks tar encoding, source path traversal rejection,
the restricted runtime command, logs, and cleanup. A live Docker run built from
the local `python:3.11-slim` image printed `ARK DevOps integration passed` and
returned exit code 0; the agent removed the temporary image and container.

## Day 76 native Data Analysis Agent

`build/ark-cpp analyze-csv` accepts exactly one of a UTF-8 `csv` string or
`csv_path`. It reads up to 10 MiB, 100,000 rows, and 50 columns. Numeric columns
include count, mean, sample standard deviation, and pairwise-complete Pearson
correlations. The output also includes a concise summary and three standalone
SVG plots: a histogram, a scatter plot, and a correlation heatmap. Raw CSV text
and its path are removed from the response message. Labels are XML-escaped, and
numeric magnitudes above 1e100 are rejected to keep the calculations finite.

## Day 78 native Hypothesis Engine

`build/ark-cpp hypothesize` accepts `{"domain":"..."}`. It retrieves up to 20
related records from the existing Qdrant semantic collection and up to 20
unlinked concept pairs from Neo4j, then asks the configured local model for five
testable candidates. The embedding model compares each candidate with the paper
abstracts. Novelty is the cosine distance from the nearest reference paper;
the ranking score is `novelty × feasibility`, where feasibility is an
uncalibrated model estimate in [0, 1]. Results include the score components and
rank, not a claim that the hypothesis is original or experimentally validated.
This requires the existing local Qdrant, Neo4j, embedding, and chat services.

## Day 79 native Experiment Designer

`build/ark-cpp design-experiment` accepts a hypothesis string and asks the
configured local model for a reproducible dataset plan, method, baseline,
metric definitions, and a bounded implementation spec. The schema requires the
code spec to target C++20, list source files and interfaces, restrict
dependencies where practical, and include acceptance tests. The result is a
design artifact; generated C++ compilation is not wired into this command yet.
The existing Coding Agent currently targets Python 3.11 and runs tests in its
Python sandbox, so routing this C++20 specification through that agent would
contradict the project's C++ direction. A native C++ code generation and
compiler sandbox remains a follow-up requirement.

## Day 80 native Scientific Report Generator

`build/ark-cpp scientific-report` accepts a hypothesis, experiment `spec`,
measured `results`, optional figure path/caption pairs, and optional Neo4j
`paper_ids`. It writes a bounded `.tex` draft with Abstract, Introduction,
Related Work, Method, Results, Figures, and bibliography sections. Linked works
are queried from Neo4j and cited by generated keys; titles and graph IDs are
included in the bibliography. User-supplied prose is escaped for LaTeX, figure
paths must be relative and traversal-free, and the output is marked unverified.
This creates a `.tex` artifact, not a PDF, and citations may need richer
bibliographic metadata before publication.

## Day 81 native SAGE checkpoints

`CheckpointManager` stores one atomic JSON file per completed step under
`data/sage/checkpoints/<loop-id>/`, plus `status.json`. Loop IDs and step names
are restricted to safe path components, and each checkpoint is limited to
16 MiB. `run_workflow` loads saved steps, executes only missing steps, and
writes each result before continuing, so a thrown step leaves earlier work
available for restart.

The CLI supports `sage-checkpoint list`, `resume <loop>`, `load <loop> <step>`,
`save <loop> <step>`, and `clear <loop>`. Set `ARK_SAGE_CHECKPOINT_DIR` to use a
different local directory. `resume` reports the last saved step and first
missing step in the standard SAGE workflow. Unit tests simulate an interruption
at the experiment step, restart, and verify the first two steps are skipped.

## Day 82 native task scheduler

Schedules persist in SQLite at `data/schedules.db` (`ARK_SCHEDULE_DB` can
override the path). Create schedules with `ark-cpp schedule cron '0 2 * * *'
'goal'`, `interval <seconds> <goal>`, `once <YYYY-MM-DDTHH:MM:SSZ> <goal>`, or
`watch <absolute-path> <goal>`. Cron expressions and one-time timestamps use
UTC. File watches poll path modification times once per second. The scheduler
executes goals while `ark-cpp serve` is running and records last-run/error
status. `ark-cpp schedules` lists jobs; `schedule pause|resume|cancel <id>`
controls them. This is a native C++ scheduler without APScheduler.

## Day 83 Materials Project client and Week 12 review

`MaterialsProjectClient` queries the official `/materials/summary/` endpoint
from native C++. It sends `MP_API_KEY` through the `X-API-KEY` header, requests
selected material IDs and fields, and caps a request at 1000 records. The
client accepts HTTPS endpoints and HTTP loopback endpoints for tests. Run
`printf '%s\\n' '{"material_ids":["mp-149"],"fields":["formula_pretty","band_gap"]}'
| build/ark-cpp materials-query` after setting `MP_API_KEY`.

The Day 71–82 native Vision, ML, DevOps, CSV analysis, Hypothesis Engine,
Experiment Designer, report generator, checkpoint, and scheduler components
are present. Day 83 adds the native materials data source. A full end-to-end
SAGE materials run still requires live local model, embedding, Qdrant, and
Neo4j services plus seeded literature; the client test uses a local transport
fixture and does not claim a live Materials Project query.

Day 84 adds `ark-cpp sage-materials <loop-id>`. Provide a JSON object on stdin
with `topic` and at least two `material_ids`. Its first five checkpointed
stages run arXiv literature, hypotheses, C++ experiment design, Materials
Project summary screening plus CSV analysis, and a LaTeX report. The request
and workflow version are bound to the loop ID so resuming with different inputs
or incompatible checkpoint schemas fails clearly. Each stage must return `DONE`
before its output is checkpointed. The
Materials Project analysis is database screening, not an executed physical
experiment; generated results are therefore not represented as measurements.
The command checks for `MP_API_KEY` before starting research, so a missing key
does not leave a partial loop checkpoint.

## Day 88 training monitor status

There is no Stage 1 SigLIP/Qwen training run to monitor: the Python training
recipe was replaced with native C++ inference on pretrained GGUF VLMs to follow
the all-C++ project direction. `vision-check` verifies the loaded llama.cpp
model advertises image input. MLflow optimization runs cover the separate
native hyperparameter smoke workflow; they are not VLM training loss curves.

## Day 89 two-hypothesis SAGE run

The native SAGE command automatically processes the top two ranked hypotheses.
It designs a separate C++ experiment for each, reuses the identical Materials
Project screening artifact, and writes a separate cited LaTeX report per
hypothesis. All nine workflow stages checkpoint independently, so a failure
in the second report resumes there without repeating literature retrieval or
the first report. The second screening intentionally reuses the same material
records rather than issuing an identical API request again.

## Day 90 Vision Agent captions in SAGE reports

Pass optional relative `figure_paths` in the `sage-materials` input. SAGE runs
each image through the native Vision Agent, stores its caption, chart type,
observations, and uncertainty notes in the `vision_figures` checkpoint, and
places the generated caption and relative image path in both LaTeX reports.
The workflow rejects absolute and traversal paths before reading image files.
When no paths are supplied, report generation continues with no figures. The
existing Vision Agent fixture test verifies confusion-matrix labels, cell
values, and a misclassification caption; a live VLM remains unavailable here.

## Day 92 native self-improvement

Run `ark-cpp self-improve research_agent <suite.json> <report.json>` or use
`coding_agent`. The suite must contain exactly 10 ARKBench research or coding
tasks for that target. The agent reads up to 50 pending reflection records for
the target with quality below 0.65, asks the configured local model for a
replacement system prompt, then evaluates both prompts on the same 10 cases.
It calls `AgentVersioner::bump` only when the candidate mean exceeds the
baseline mean by strictly more than 0.05. Otherwise it writes a rejection
report and leaves the active version unchanged. The report includes per-case
scores, means, rationale, and the resulting version when promoted.

This uses the native local inference and versioning stack rather than the
guide's Claude-based prompt generator. A live benchmark and promotion still
require Redis, the configured inference service, and (for coding suites) the
Docker test image. The self-improvement unit test uses deterministic reflection,
inference, and evaluator fixtures; it verifies target filtering, all 10 paired
scores, the strict threshold, and the version-commit callback.

The Week 13 status and live verification requirements are recorded in
[the review](week13_review.md).

## Day 85 hypothesis evidence and specificity

Hypothesis proposals now cite one to three exact paper IDs from the retrieved
Qdrant evidence. The engine rejects unsupported or duplicate citation IDs and
returns a compact reference list with titles and source URLs. The generation
prompt requires a material or intervention, measurable outcome, expected
direction, and comparator or control, and asks for bounded composition or
conditions when supported by evidence. Novelty remains an embedding distance
from retrieved abstracts, not proof that a hypothesis is scientifically new.

```bash
printf '%s\n' '{"experiment":"smoke","ranges":[{"name":"learning_rate","minimum":0,"maximum":1}],"target":{"learning_rate":0.5},"trial_count":10}' \
  | build/ark-cpp ml-optimize
```

## Day 55 Phase 2 integration

The integration test classifies and plans a defensive code-review task,
passes fixture research evidence to the Coding Agent, and checks a patched
SQL lookup with locked tests and offline static analysis. It also verifies
search and browser evidence, issue lookup, terminal output, working memory,
document generation, reflection, and filesystem writes. The model, arXiv,
Tavily, Qdrant, and GitHub responses are fixtures. Chromium, Docker, Redis,
and SQLite run locally.

```bash
ARK_TEST_CHROMIUM=/usr/bin/chromium \
ARK_TEST_DOCKER_SOCKET=/var/run/docker.sock \
ARK_TEST_REDIS_PORT=16379 \
ARK_TEST_PHASE2_ARTIFACTS="$PWD/build/phase2-report" \
ctest --test-dir build -R ark_phase2_integration_tests --output-on-failure
```

Build the [Day 51 scanner image](#day-51-security-agent) first. Use an unused
artifact directory to keep the research summary, patched Python module,
security audit, DOCX report, reflection log, and fourteen-component manifest.

## Day 57 output conflicts

For a coding task with a direct research dependency, the supervisor compares
the research findings with passing test evidence. If the local reviewer finds
a contradiction, both workers receive the other's evidence and may revise
their output in up to two rounds. The Coding Agent keeps the same tests.
An unresolved pair retains its artifacts, flags the coding task for human
review, and marks dependent results skipped. The task result records these
decisions under `output_conflicts`.

`ark-cpp conflict-check < evidence.json` reviews one pair with top-level
`research` and `code` outputs. Five controlled comparisons used real Docker
observations. All five were detected after calibrating the review prompt on
those cases. [Controlled comparisons](../benchmarks/conflicts/output_conflicts.json)

## Day 58 WebGoat source audit

`ark-cpp security` accepts `language: "java"` with `run_semgrep` or `audit`.
The bundled rules flag dynamically built SQL, Java object deserialization,
weak message digests, and upload paths derived from client filenames.
They run offline and report source locations for defensive review.

On a pinned WebGoat v2025.3 checkout, the scanner matched **6 of 8** expected
file/rule pairs and reported zero findings for two prepared-statement controls.
Both misses were advanced SQL examples. The 75% recall applies to these
selected source files. The workflow inspects source without launching the
application. [WebGoat release](https://github.com/WebGoat/WebGoat/releases/tag/v2025.3)
[Per-file results](../benchmarks/security/webgoat_source_results.json)

## Day 59 memory retention

Completed tasks retain working state in Redis for up to one hour. SQLite
moves older episodes into an archive after the latest 10,000 active entries.
`ark-cpp episodes-archive` shows the most recent archived records.

The SQLite semantic index (`ARK_SEMANTIC_DB`) records when a vector was first
ingested and whether it appeared in a semantic search. `ark-cpp memory-prune`
removes indexed vectors at least 30 days old with zero retrievals and
procedural skills with observed success below 0.30. Skills linked to a
conflict retain their review history. Semantic records created before the
index was added enter retention tracking when re-ingested.

## Day 60 native performance

Set `ARK_LLAMA_EMBED_URL` to a separate loopback llama-server to keep the
embedding model resident while the generation router switches models.
One repeated single-text embedding is shared across copied encoders in
the same operation. SQLite indexes support ordered recent episode reads.

The end-to-end profile used five live arXiv abstracts, Qwen3-4B, a separate
embedding server for the tuned run, local Qdrant, and Docker. Warm memory
retrieval fell from 37.6 ms to 31.8 ms median. The first retrieval after a
generation request fell from 607 ms to 36 ms. The other tuned timings were
24.3 seconds for five-paper research, 6.2 seconds for the measured coding
task, and 2.8 seconds for evaluation. The evaluator returned a valid retry
decision in both runs. SQLite changed from a sort to a covering-index scan.
[Raw performance profile](../benchmarks/performance/day60.json)

## Day 61 service degradation

- Local planning is the default. Set both `ARK_GROQ_MODEL` and
  `ARK_GROQ_API_KEY` to opt into Groq classification and planning for public
  tasks. Failed requests fall back to the configured local models. Private
  and airgap tasks use local planning. Retrieved memory stays local.
  [Groq API reference](https://console.groq.com/docs/api-reference)
- When Redis is unavailable, active tasks continue with configured agent
  profiles and bounded process-local working state. Registry records and
  version changes require Redis. SQLite episode logs continue independently.
- Qdrant writes are mirrored in SQLite. During an outage the runtime ranks
  mirrored vectors by exact cosine similarity, and pending writes replay
  when Qdrant returns. The fallback covers records already mirrored under
  the active embedding model.
- A failed local model request names the unavailable Ollama or llama.cpp
  endpoint. The supervisor reports a failed task after bounded retries.

## Day 62 dashboard graphs

The task inspector draws arrows from each prerequisite to its dependent
task. Running nodes follow worker events and honor reduced-motion settings.
Each agent's version panel charts measured ARKBench scores by run date and
producer version. It uses completed reports from `ARK_BENCHMARK_RESULTS`
(defaults to `benchmarks/arkbench/results`). Tooltips and a text list give
the dataset version, agent version, score, and number of scored cases.

## Day 64 ARKBench v0.5

```bash
build/ark-cpp benchmark benchmarks/arkbench/benchmark_v0.5.json build/arkbench-v0.5-report.json
```

The suite keeps the original ten research and coding tasks and adds ten
dependency checks based on public CVE advisories and ten browser, document,
and terminal tasks. The dependency checks use offline pinned versions. The
browser cases use local Chromium pages. The new cases score against fixed
tool expectations. The original cases retain their acceptance tests and
model evaluator.

The intermediate run scored all 30 tasks, completed 23, and averaged 0.7853.
The original ten averaged 0.3560 in this replay, and every new tool contract
passed. The larger suite uses a different mix of scoring methods than the
Month 2 baseline. [Intermediate results](../benchmarks/arkbench/results/month4_intermediate.json)

### Capability discovery (Day 93)

`ark-cpp capability-discovery "Can ARK OS research a scientific topic?"` refreshes
`config/capabilities.json` from the live Redis agent registry, recent episodic
reflection records, and local ARKBench result history, then answers the question
with confidence and evidence. Set `ARK_CONFIG_DIR`, `ARK_EPISODIC_DB`,
`ARK_BENCHMARK_RESULTS`, `ARK_REDIS_PORT`, and `ARK_REDIS_DB` to select the data
sources. Confidence is evidence-weighted and capped at 0.99; a capability absent
from the registry is reported unsupported with confidence 0.0. The ten-question
fixture verifies query matching and confidence handling without claiming the
current live registry has passed those questions.

## Day 95 native voice input

`ark-cpp voice` records five seconds of mono 16 kHz WAV through `arecord`, runs
the local C++ `whisper-cli`, and submits the transcript as an ARK task. Use
`--seconds 1..60` to change capture time or `--file recording.wav` to transcribe
an existing recording. Configure `ARK_AUDIO_RECORDER`, `ARK_WHISPER_CLI`, and
`ARK_WHISPER_MODEL`. Audio is temporary and deleted after transcription. The
machine has `arecord`, but no installed `whisper-cli` or Whisper model, so live
speech submission has not been verified here; offline tests cover argument
construction, transcript handling, capture cleanup, and invalid input.
