# Reflection quality study

Does guidance derived from a failed test help the Coding Agent produce a
correct first solution to a related task?

## Method

The study used 56 held-out task variants from eight Python programming
families. Each family had one separate training example. Docker verified
that the supplied training implementation failed its test before the
Reflection Agent summarized a reusable pattern from that evidence.

Each held-out variant ran twice through the same Coding Agent and llama.cpp
model. Both attempts received the same task contract and smoke tests. One
attempt also received the relevant Reflection pattern through the existing
review-feedback field. The runner alternated which attempt went first and
used one candidate per attempt. A candidate had to pass its smoke tests
and a second set of tests in a fresh Docker container. Only the smoke
tests were shown to the model.

The study used `coding_agent@1.0.0` with Qwen2.5-Coder-0.5B-Instruct Q4_K_M
and `reflection_agent@1.0.0` with Qwen3-4B Q4_K_M. The task variants within
a family share the same implementation pattern. The comparison is
descriptive and applies to these models, prompts, and contracts.

## Results

Across **112 coding runs**, the baseline passed **12 of 56 (21.4%)** and
the guided arm passed **14 of 56 (25.0%)**, an increase of **3.6 percentage
points**. Five variants improved, three regressed, and 48 were unchanged.

| Task family | Baseline passed | With Reflection passed |
| --- | ---: | ---: |
| Inclusive bounds | 2/7 | 7/7 |
| Stable sorting | 0/7 | 0/7 |
| Rotation | 2/7 | 0/7 |
| Stable deduplication | 0/7 | 0/7 |
| Median | 0/7 | 0/7 |
| Grouping | 7/7 | 7/7 |
| Text normalization | 1/7 | 0/7 |
| Chunking | 0/7 | 0/7 |

The net gain was small and varied by family. Guidance helped the inclusive
bounds examples and several other variants still failed their contracts.
These measurements support testing Reflection guidance before applying it
broadly. [Study data and paired outcomes](../benchmarks/reflection/results_v0.1.json)

## Repeat the study

```bash
build/ark-cpp reflection-study benchmarks/reflection/study_v0.1.json build/reflection-results.json
```

Set `ARK_CODE_MODEL` and `ARK_REFLECTION_MODEL` to the corresponding
local model aliases, then select the llama.cpp endpoint with `ARK_LLAMA_URL`.
