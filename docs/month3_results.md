# ARK OS Month 3 results

The native runtime verified Chromium browsing, isolated Bash and static
analysis, local Git operations, document processing, and llama.cpp inference.
The Phase 2 integration test exercised 14 native components. GitHub pull
request creation was tested with an API fixture. The llama.cpp throughput
target remains open.

| Checklist item | Verification |
| --- | --- |
| Browser Agent | Chromium opened arXiv and extracted the first three titles in page order. |
| Terminal Agent | A fresh Docker container ran Bash with no network or host mounts. Tests verified stdout, stderr, timeout cleanup, and missing-module diagnoses. |
| GitHub Agent | Verified clone, branch, selective commit, and push using disposable repositories, plus a live issue read. A fixture verified the pull request request and response. |
| Document Agent | Read text from an arXiv paper. LibreOffice opened the generated DOCX and XLSX. |
| Security Agent | Semgrep and Bandit flagged the seeded SQL construction and placeholder credential in network-disabled containers. |
| llama.cpp | Live general, coding, vision, and embedding calls passed. Streaming and a one-model router verified switching between roles. |
| Phase 2 integration | All 14 components ran with local Chromium, Docker, Redis, and SQLite. Model, arXiv, Tavily, Qdrant, and GitHub responses came from fixtures. |

The Week 7 repair evaluation fixed **10 of 10** seeded defects in its
small, single-module benchmark. All locked tests failed before the repair
and passed after one attempt. [Repair evaluation](self_repair_evaluation.md)

## Inference benchmark

Both engines served the same Qwen2.5-Coder-0.5B-Instruct Q4_K_M GGUF on an
RTX 5060 Laptop GPU. The ensemble of ten prompts used the same task inputs
and JSON output contract. Both services were warmed before measurement.

| Measure | Ollama 0.40.7 | llama.cpp build 10635 |
| --- | ---: | ---: |
| Mean latency | 264.7 ms | 182.8 ms |
| Mean generation throughput | 307.3 tokens/s | 280.3 tokens/s |
| Structured responses | 8 of 10 | 10 of 10 |

llama.cpp lowered mean latency by 30.9% and produced 8.8% fewer tokens per
second. These are measurements for the recorded model and configuration.
The throughput metric and the latency metric use different denominators:
generation tokens per second covers decoding, while latency covers the
whole request. [Per-case measurements](../benchmarks/inference/results.json)

The [paper draft](papers/ark_os_architecture.md) describes the measured
inference configuration in its Methods section.
