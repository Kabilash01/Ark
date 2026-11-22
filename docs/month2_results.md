# ARK OS Month 2 results

The native runtime completed the Month 2 build checklist. The first live
[ARKBench v0.1 baseline](../benchmarks/arkbench/results/month2_baseline.json)
scored all 10 tasks. Three met the workflow's completion criteria. The mean
score was **0.4035** over **262.62 seconds** of case runtime.

| Suite | Scored | Completed | Mean score |
| --- | ---: | ---: | ---: |
| Research | 5 | 1 | 0.5180 |
| Coding | 5 | 2 | 0.2890 |
| Overall | 10 | 3 | 0.4035 |

The run used local Qwen3-4B Q4_K_M for research, evaluation, and reflection,
Qwen2.5-Coder-3B for code, and BGE Large for embeddings. Each case ran a fixed
producer/evaluator graph through the native dispatcher. The baseline records
the dataset checksum, exact agent revisions, model manifest digests, prompt
hashes, sandbox image, and evaluator scores.

## Verification checklist

| Area | Evidence |
| --- | --- |
| Agent versioning | Redis integration tests verified bumps and rollback. A routed task used the restored model and prompt and recorded the restored version in SQLite. |
| Privacy and airgap | The private coding workflow completed through loopback services and Docker. Airgap rejected an allowlisted literature request before provider access. [Week 5 verification](week5_review.md) |
| REST and WebSocket | The C++ API passed checks for task lifecycle, memory listings, privacy settings, agent versions, and live events. The integration check used local model fixtures with Redis, SQLite, Qdrant, and Docker. |
| Terminal CLI | The native CLI recorded task status and measured completion conditions. An end-to-end check stopped `--until` on its first passing iteration. |
| Dashboard | Chromium verified the live roster, task plan and output download, all four memory tiers, and Redis-backed rollback. |
| ARKBench | The runner stored a numeric result, elapsed time, producer version, and evidence for every case. The native test suite passed all 17 tests with Redis and Docker enabled. |

## Baseline findings

- All five research outputs cited every assigned paper. On three cases the
  evaluator returned its own rubric weights of `0.40`, `0.30`, and `0.30` as
  ratings while describing the work favorably. Each scored `0.34` after
  weighting. This calibration issue limits what the aggregate research score
  says about the artifacts.
- The separate Docker acceptance checks caught the interval solution mutating
  its input and the run-length decoder accepting invalid character strings.
  The CSV solution still contained an unterminated string after its repair
  attempts and failed its smoke tests.
- The score is a baseline for the recorded runtime and evaluator configuration.
  A future comparison can replay the dataset and report citation coverage,
  acceptance tests, and evaluator ratings alongside the aggregate.

The [architecture draft](papers/ark_os_architecture.md) describes the runtime
boundaries and how the benchmark measures them.
