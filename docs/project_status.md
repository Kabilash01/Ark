# Project status — 2026-11-21

This report records the Python baseline through Day 23. The later C++
conversion and live MVP verification are in [the native runtime guide](native_cpp.md)
and [the Month 1 results](month1_results.md).

## Repository assessment

The latest pre-existing commit is `9a668d0` (Day 21 review). The implementation
sequence is defined by `Implementation Guide/ARK_OS_Daily_Build_Plan.md`.

| Area | Implemented | Remaining |
|---|---|---|
| Week 1 foundation | Hardware/model selection, Docker configuration, frozen ARKMessage schema, BaseAgent, Redis registry and messaging | Fresh service-level verification on this machine |
| Week 2 memory | Working (Redis), episodic (SQLite), semantic (Qdrant), procedural (FAISS/JSON), unified MemoryAgent | Fresh persistence/integration verification |
| Week 3 kernel | Supervisor classification, Planner DAG validation, context injection, STATE.md writer, topological dispatch, heartbeat monitor | LangGraph still contains only a stub node; real worker execution is not integrated |
| Week 4 workers | Day 22 research tools and Day 23 Research Agent | Docker code tools, Coding Agent/self-repair, Evaluation Agent, full MVP loop |
| Early Day 36 API | REST task lifecycle, registry endpoints, privacy config, WebSocket events | Task store is in-memory; submitted tasks remain pending; memory endpoints return placeholders; API is not wired to kernel |
| Later phases | Package/directory placeholders | Dashboard, ARKBench, additional workers, SAGE, citation graph, conflict resolution, deployment |

Research/Coding/Evaluation source files were empty at assessment time. Historical
Week 1–3 reports describe successful earlier tests; those are not fresh results
for the present checkout/environment. The API's comments claiming memory and
supervision do not exist are stale: those modules now exist but are not connected.

## Day 22 implementation

`tools/research_tools.py` provides the four functions requested by the guide:

- `search_arxiv(query, max_results=5, start=0)` returns metadata dictionaries.
- `fetch_arxiv_paper(arxiv_id)` returns metadata and abstract, not PDF full text.
- `search_semantic_scholar(query, limit=5, offset=0)` returns a provider page.
- `get_paper_citations(paper_id, limit=100, offset=0)` returns incoming citation
  records under `citingPaper`, preserving the `next` pagination offset.

All functions accept an optional `httpx.Client`. Semantic Scholar accepts an
optional API key or reads `SEMANTIC_SCHOLAR_API_KEY`. No key is required for mocked
tests. arXiv requests are serialized with a three-second interval within this
process. Requests have a 30-second timeout. HTTP errors, including rate limiting,
propagate rather than being disguised as empty results. No automatic retries or
unbounded pagination are performed. These tools access public internet services;
future airgap-mode orchestration must avoid calling them.

Provider contracts checked against:
- https://info.arxiv.org/help/api/user-manual.html
- https://api.semanticscholar.org/graph/v1/swagger.json

## Verification

Created a Python 3.11 virtual environment and installed the dependencies needed
for the selected checks; this is not a full project dependency installation.

```bash
.venv/bin/python -m pytest tests/tools tests/test_ark_message.py tests/test_base_agent.py tests/test_state_writer.py tests/kernel/test_ark_state.py tests/kernel/test_ark_graph.py -q
# 50 passed
.venv/bin/python -m pytest tests/kernel/test_planner_agent.py tests/kernel/test_router.py -m 'not live_llm' -k 'not offline' -q
# 18 passed, 6 deselected
```

A live `search_arxiv('ti:Attention AND ti:Need', max_results=3)` returned three
papers including `1706.03762v7`, “Attention Is All You Need”. Semantic Scholar
requests were verified with HTTP mock transports, not live credentials.

Full-suite collection was attempted but blocked by missing `qdrant_client`,
`faiss`, and `sentence_transformers`. Redis/Qdrant persistence, hosted/local LLMs,
and the complete integration suite have not been revalidated. Pytest also emits
an existing unset asyncio fixture-loop-scope warning.

## Day 23 Research Agent

`ResearchAgent` accepts a planner `instruction` or an explicit arXiv `query` in
an ARKMessage payload. It uses `models.general` from `config/active-models.json`
for local query generation and synthesis. On an empty arXiv search it can use
Semantic Scholar to discover papers with arXiv IDs. It fetches up to five
abstracts and stores them through `MemoryAgent` before synthesis. A stable ID per
arXiv version keeps repeated searches from duplicating those records.

The result contains a `summary`, `findings` with source IDs, and `citations` built
from fetched metadata. The worker rejects out-of-set citation IDs. Privacy mode
honors the configured literature-service allowlist, and airgap mode rejects
external research before sending requests. The worker handles abstracts and
metadata. Full-text reading and factual evaluation remain separate work.

Hardware detection selected T2 with `qwen2.5:7b`. The live research smoke test
used a temporary active-model config, a cached local Qwen3-4B model, and the
project's BGE encoder. It returned a structured summary citing `1706.03762v7`
and verified that the paper abstract was written to and retrieved from Qdrant.
Running the same search twice kept the collection at five unique abstracts.

After installing the vector dependencies and preparing temporary local Redis
and Qdrant instances, the regression run passed 171 tests. It deselected 18
hosted-model cases and 3 Supervisor cases that require `qwen2.5:7b`. The run
included the memory persistence tests and the stable record ID test.

Day 24 covers the Docker code execution sandbox. The guide then sequences the
Coding and Evaluation Agents before the Day 27 MVP integration test.

## Native implementation update — 2026-11-24

The subsequent C++ implementation and verification notes are in
[`native_cpp.md`](native_cpp.md), with the current review in
[`week15_review.md`](week15_review.md). Days 105–109 now have paper and MCP
artifacts. The full native suite currently reports 38 passed and 2
service-gated skips across 40 tests. This does not close the all-agent runtime
gap: the normal planner/dispatcher still lacks adapters for every specialist,
and local model/data services are not available for a complete live task.

## Native roadmap tail — 2026-11-24

- Day 109: native C++ MCP stdio server; local handshake and tool-list smoke
  verified, but no real client registration or backend tool call verified.
- Day 110: Week 15 evidence and blocker review recorded.
- Day 111: README, contributing guide, and code of conduct prepared. The GitHub
  repository is already public; no GitHub Release has been created. The MIT
  license remains unchanged.
- Days 112 and 114: local paper submission packages prepared. No conference or
  preprint submission was made.
- Day 113: evidence-based demo capture script prepared; no video recorded.
- Day 115: custom VLM training claim remains blocked; inference-only scope
  documented.
- Day 116: prospective conflict-data analysis protocol added; the current
  production telemetry capture has zero cases.
- Day 117: native Obsidian and Notion exporters implemented. Obsidian CLI and
  mocked Notion request tests pass; no Notion workspace was contacted.
- Day 118: native deployment guide added and Compose syntax validated. No
  clean-machine, boot-time, cloud, or Pi deployment was run.
- Day 119: architecture map added. Full live integration of all 20 agents
  remains open.
- Day 120: day-tagged Git history and release blockers indexed in
  [`CHANGELOG.md`](../CHANGELOG.md). The tag, GitHub Release, research
  submissions, and demo publication remain incomplete.

## Follow-on integration — 2026-11-25

The shared C++ planner now routes explicit `EXECUTE` goals through the
supervised terminal worker in the restricted Docker sandbox. Planner and
kernel tests validate that route and its opt-in boundary. Saved DAG dispatch
also supports typed `security_agent` inputs; local task checks reject host
report paths, and ARKBench fixture routing remains covered. Saved DAG dispatch
now also supports confined DOCX, XLSX, and PDF document writes with no
planner-controlled output path. The saved-DAG memory worker now provides typed
episodic writes and searches against SQLite. Deterministic CSV analysis is now
also routed through saved DAGs with bounded inline input. Scientific report
generation now accepts supplied evidence and returns LaTeX inline without
graph lookups. Live model-to-Docker remains unverified, and the all-20-agent
integration criterion stays open.
