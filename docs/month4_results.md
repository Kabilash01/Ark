# ARK OS Month 4 Closeout

Month 4 delivered native C++ research-worker balancing, conflict telemetry,
concurrency stress coverage, and a 50-case ARKBench v1.0 run. The benchmark
dataset and per-task evidence are in
[`benchmark_v1.0.json`](../benchmarks/arkbench/benchmark_v1.0.json) and
[`month4_final.json`](../benchmarks/arkbench/results/month4_final.json).

## Results

| Work | Verification result |
| --- | --- |
| Day 66 research routing | Two process-local worker slots; concurrent fixture assignments balanced, a low-rated slot was deprioritized, and exceptions released slots. Native tests passed. |
| Day 67 conflict collection | Runtime journal and 30-day report are implemented. The initial production capture contains zero events and cannot backfill earlier runs. Tests cover all four conflict classes' schemas, deduplication, sanitization, and resource-wait observation. |
| Day 68 concurrency | Twenty concurrent tasks reached measured peaks of 1 browser, 2 terminal, and 3 arXiv permits. All 20 concurrent SQLite writes were retained and matched the serial score results. |
| Day 69 ARKBench | All 50 cases scored; 44 have `DONE` status. The 30 original cases averaged 0.8127 and the 20 new coordination/mixed cases all passed their locked checks. Every row records its producer or participating agent versions. |

The unchanged set of five research and five coding task IDs scored 0.438 in
Month 4, compared with 0.4035 in the Month 2 baseline. That is an 8.55% relative
increase and falls within the 5–10% target. The combined 50-case mean is 0.8876,
but it should not be compared with the Month 2 ten-case mean because the final
dataset also contains deterministic tool checks.

The original 30-task run had 24 `DONE` rows. All 30 received numeric scores,
including six runs whose worker status was not `DONE`. The additional 20
workflow cases completed and scored 1.0 each. Research and coding remain the
main measured-model limitation: only three of five research cases and one of
five coding cases ended with `DONE` status in the original cohort.

## Architecture and research artifacts

The architecture draft now covers rated research slots, bounded resource
scheduling, the validated tool-workflow DAG path, and its measured scope.
The conflict-handling paper draft defines the event classes and privacy-aware
measurement method. It explicitly leaves production frequency and resolution
analysis open until the collector has accumulated a real observation window.

## Verification scope

The Day 66–69 targeted C++ tests passed. The stress test exercises the real
semaphore and SQLite implementations with deterministic local fixtures. The
benchmark uses local inference, Docker-backed terminal and coding tests,
Chromium-backed browser cases, local document generation, and pinned local
security-advisory fixtures. The concurrency fixture does not claim model
inference throughput or external-service load capacity.

The full CTest suite reported 22 passing tests and skipped two opt-in
integration tests when their environment variables were absent. Both skipped
tests were then enabled explicitly with Chromium, Docker, and Redis and passed.
The phase-two integration generated its artifact manifest after verifying 14
native components, including the planner, supervisor, research, coding,
evaluation, browser, memory, security, document, terminal, file-system, GitHub,
web-search, and reflection paths.
