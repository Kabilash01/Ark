# Week 7 self-repair evaluation

The native Coding Agent repaired 10 of 10 seeded defects, passing the guide's
7-of-10 target. Every seed failed its locked unittest in a fresh Docker
container. All ten corrected versions passed those same tests on the first
model-assisted repair.

| Error type | Fixed | Rate |
| --- | ---: | ---: |
| Syntax | 3 of 3 | 100% |
| Import | 2 of 2 | 100% |
| Logic | 3 of 3 | 100% |
| Type | 2 of 2 | 100% |
| **Overall** | **10 of 10** | **100%** |

The run used `coding_agent@1.0.0`, local Qwen3-4B Q4_K_M through Ollama
(the `ark-day44-qwen3:latest` alias), and the `python:3.11-slim` Docker image.
The runner allowed up to three candidate repairs per case. The test set covers
small, single-module, standard-library tasks with explicit contracts. The
results measure repair on these fixtures and their test cases.

The [suite](../benchmarks/coding_self_repair/cases.json) and
[run record](../benchmarks/coding_self_repair/results.json) include all ten
case IDs and per-case results. Set `ARK_CODE_MODEL` to your local model name
and `ARK_OLLAMA_URL` to your local service before rerunning.

```bash
build/ark-cpp repair-benchmark benchmarks/coding_self_repair/cases.json build/repair-results.json
```
