# ARK OS Month 1 results

The C++20 runtime passed the Day 27 research-to-code acceptance test on Linux.
The live run scored **0.745** against the guide's minimum of **0.70**. The
evaluator returned `ACCEPT_WITH_WARNING` under its separate 0.80 threshold.

| Area | Evidence |
| --- | --- |
| Foundation | Native build, hardware tier selection, frozen ARKMessage, Redis registry and pub/sub, audit events, atomic state snapshots |
| Memory | Redis TTL, SQLite reopen tests, live Qdrant abstract storage and retrieval, persistent skills with success-weighted ranking |
| Kernel | Intent classification, plan validation, parallel dependency dispatch, offline worker handling, and local model calls |
| Research | Live arXiv metadata retrieval, batch BGE embeddings, stable Qdrant paper IDs, and grounded citations |
| Coding | Real Docker test runs, sandbox timeout and isolation, injected bug repair with fixed tests, and a bounded repair loop |
| Evaluation | Six rubric types with score and threshold tests. Routed code gets test and syntax credit from Docker evidence |
| API | Loopback REST and WebSocket lifecycle checks covering submission, queue cancellation, results, memory, and privacy settings |

The Fibonacci test retrieved [arXiv:1803.07199](https://arxiv.org/abs/1803.07199),
generated a solution, passed both the supplied contract suite and independent
Docker cases, and reopened the persisted research, code, and evaluation
episodes. The contract checks arithmetic results, negative inputs, and a
bounded number of executed source lines on a large input. The task supplies
the recurrence, so this run measures execution and handoffs with a known
specification. It does not establish unguided algorithm discovery.

The hardware detector selected T2. Verification used a cached Qwen3-4B model
for general tasks and evaluation, Qwen2.5-Coder-3B for code, and the local
BGE encoder. GAIA passed a separate C++ model client trial. ARK's fixed
workflows use direct clients, and no inference speed comparison was measured.

The first paper draft can frame ARK as a local agent runtime whose quality
depends on verifiable task handoffs, persistent context, and isolated execution.
The prototype produces a trace from a cited paper to tested code and a stored
evaluation. A comparative study can repeat the same workloads with shared
models, inputs, and hardware to measure the effects of orchestration choices.

For setup and the live test command, see [Native C++ runtime](native_cpp.md).
Day 29 adds system-level retry and escalation. The dashboard, ARKBench
expansion, and later phases remain on the roadmap.
