# Test the local product

The native C++ server includes the dashboard and task API. Run it from the
repository root:

```bash
cmake --build build -j 6
OLLAMA_NO_CLOUD=1 ollama serve
```

In another terminal, start Redis if it is not already running, then start ARK:

```bash
redis-server --bind 127.0.0.1 --port 6379 --save '' --appendonly no --daemonize yes
ARK_MODEL=qwen2.5:7b ARK_CODE_MODEL=qwen2.5:7b \
  ARK_EVAL_MODEL=qwen2.5:7b ARK_REFLECTION_MODEL=qwen2.5:7b \
  ARK_REDIS_PORT=6379 build/ark-cpp serve 8000
```

For a lightweight check before the 7B model is installed, use
`qwen2.5:0.5b` for all four model variables in the server command. The exact
CSV goal below executes deterministically. The 0.5B model has misclassified
ordinary free-text goals in a live check, so use the configured 7B model when
assessing natural-language planning quality.

Install the models first with `ollama pull qwen2.5:7b` and
`ollama pull qwen2.5-coder:7b` if needed. Open
<http://127.0.0.1:8000/>. Select **New task** and submit this exact goal:

```text
Analyze this CSV: sample,value
A,1
B,2
```

The task should finish with two rows and mean `1.5` for `value`. Open the task
in the recent queue to inspect its result and events. The same flow can be
checked from a terminal:

```bash
python3 scripts/product_smoke.py
python3 scripts/product_smoke.py --browser  # also clicks New task in Chromium
```

The agent cards reflect on-demand C++ workers. **Idle** means the worker's
required local services are available; **Needs setup** shows the missing
prerequisite on the card. ARK registers its 20 built-in agents in Redis when
the server starts and refreshes their readiness when the dashboard requests
`/agents`. A missing Redis service appears as **Start Redis** on every card.
To enable the research, vision, and file workers on a fresh checkout, run:

```bash
docker compose up -d qdrant
ollama pull bge-large:latest
ollama pull qwen2.5vl:7b
```

The file worker creates its default workspace at `data/agent_files` and only
accepts paths within that directory. Web search requires a Tavily key in the
ARK server environment (`TAVILY_API_KEY`); restart the server after setting it.
Versioned agents retain their selected model in Redis. Changing `ARK_MODEL`
after registration does not change an existing profile; use `ark-cpp
version-bump <agent> patch <change.json>` with a `model` and `change_summary`
to record the switch in version history. The readiness check uses the active
profile model.

This check covers the served dashboard assets, task submission, the native CSV
worker, result retrieval, events, and invalid-goal handling. The standard
`ctest` suite covers worker fixtures; the browser and Phase 2 integration tests
can also run with `ARK_TEST_CHROMIUM`, `ARK_TEST_DOCKER_SOCKET`, and
`ARK_TEST_REDIS_PORT` set. The Day 96 all-20-agent task and service-backed SAGE
run remain separate unfinished verification goals.

On 2026-11-25, the local product check passed through the browser's **New task**
form and the API. Report, bounded ML, and Docker tasks submitted through the
API also returned `DONE`; an invalid goal returned HTTP 422 and an incomplete
report returned `FAILED` with an error. With Chromium, Docker, and Redis
enabled, the full native CTest suite passed all 40 tests.

On 2026-11-26, Qdrant and the BGE embedding model were brought up locally.
The 7B vision model completed a figure description and a cited arXiv research
request; Qdrant stored the paper vector. The dedicated file workspace passed
write, read, and list checks. A dashboard coding task reached the native
Coding Agent, passed Docker tests, and completed evaluation after a generated
NaN equality assertion was corrected. The supervisor, planner, research, and
coding profiles were versioned onto the installed `qwen2.5vl:7b` model. The
dashboard then showed 19 ready agents and one needing setup: web search still
requires `TAVILY_API_KEY`. Downloads of the separate general and coding 7B
models were interrupted by registry DNS failures, so this verification used
the installed vision model for those four active profiles.
