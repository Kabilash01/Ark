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

Install the model first with `ollama pull qwen2.5:7b` if needed. Open
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

This check covers the served dashboard assets, task submission, the native CSV
worker, result retrieval, events, and invalid-goal handling. The standard
`ctest` suite covers worker fixtures; the browser and Phase 2 integration tests
can also run with `ARK_TEST_CHROMIUM`, `ARK_TEST_DOCKER_SOCKET`, and
`ARK_TEST_REDIS_PORT` set. The Day 96 all-20-agent task and service-backed SAGE
run remain separate unfinished verification goals.
