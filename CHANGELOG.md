# Changelog

This is a development-history index reconstructed from commits tagged `[Day NN]` in Git. It is not a release announcement or a claim that every roadmap item passed verification. Combined-day commits appear once with their covered day range. Entries are listed in repository order.

## Day-tagged work

- **Days 1–2** — Env detection + repo scaffold: retire backend/, add ark-os structure ([`da6bb7b`](https://github.com/Kabilash01/Ark/commit/da6bb7b))
- **Day 3** — Docker Compose: qdrant, redis, neo4j, mlflow, ark-sandbox up ([`97b08b6`](https://github.com/Kabilash01/Ark/commit/97b08b6))
- **Day 4** — ARKMessage schema: AgentSpec v1.0 FROZEN ([`1c3e08c`](https://github.com/Kabilash01/Ark/commit/1c3e08c))
- **Day 5** — BaseAgent abstract class ([`b00d740`](https://github.com/Kabilash01/Ark/commit/b00d740))
- **Day 6** — Agent Registry with version history ([`80b3b29`](https://github.com/Kabilash01/Ark/commit/80b3b29))
- **Day 7** — Week 1 review ([`8b9e2c2`](https://github.com/Kabilash01/Ark/commit/8b9e2c2))
- **Day 8** — Redis Working Memory ([`b878026`](https://github.com/Kabilash01/Ark/commit/b878026))
- **Day 9** — SQLite Episodic Memory ([`c6da046`](https://github.com/Kabilash01/Ark/commit/c6da046))
- **Day 10** — Qdrant Semantic Memory ([`85107cb`](https://github.com/Kabilash01/Ark/commit/85107cb))
- **Day 11** — Procedural Memory (JSON + FAISS) ([`881b035`](https://github.com/Kabilash01/Ark/commit/881b035))
- **Day 12** — Memory Agent unified interface ([`c989480`](https://github.com/Kabilash01/Ark/commit/c989480))
- **Day 13** — Redis Message Bus ([`2a34de6`](https://github.com/Kabilash01/Ark/commit/2a34de6))
- **Day 14** — Week 2 review: memory architecture audit ([`1fd67fc`](https://github.com/Kabilash01/Ark/commit/1fd67fc))
- **Day 15** — LangGraph state machine ([`ebcf7b5`](https://github.com/Kabilash01/Ark/commit/ebcf7b5))
- **Days 16–18** — Supervisor Agent, Planner Agent, Supervisor<->Planner integration ([`547e3af`](https://github.com/Kabilash01/Ark/commit/547e3af))
- **Days 19–20** — Router + topological dispatch, Heartbeat Monitor ([`f8a5593`](https://github.com/Kabilash01/Ark/commit/f8a5593))
- **Day 21** — Week 3 review: kernel architecture + end-to-end trace ([`9a668d0`](https://github.com/Kabilash01/Ark/commit/9a668d0))
- **Day 22** — Research tools: arXiv and Semantic Scholar with tested API handling ([`f48ec2c`](https://github.com/Kabilash01/Ark/commit/f48ec2c))
- **Day 23** — Research Agent: cite and store papers with local synthesis ([`3d68715`](https://github.com/Kabilash01/Ark/commit/3d68715))
- **Day 24** — C++ sandbox: run Python tools in isolated Docker jobs ([`3e258e3`](https://github.com/Kabilash01/Ark/commit/3e258e3))
- **Day 25** — C++ Coding Agent: repair against fixed sandbox tests ([`8cb9100`](https://github.com/Kabilash01/Ark/commit/8cb9100))
- **Day 26** — C++ Evaluation Agent: score artifacts with guide rubrics ([`406d525`](https://github.com/Kabilash01/Ark/commit/406d525))
- **Day 27** — Verify the native MVP and port REST and WebSocket services ([`49d815f`](https://github.com/Kabilash01/Ark/commit/49d815f))
- **Day 28** — Record native month-one verification and migration decisions ([`ce16148`](https://github.com/Kabilash01/Ark/commit/ce16148))
- **Day 29** — Retry transient failures and report unresolved tasks ([`20f85a4`](https://github.com/Kabilash01/Ark/commit/20f85a4))
- **Day 30** — Version agent prompts and models in Redis ([`7037b7c`](https://github.com/Kabilash01/Ark/commit/7037b7c))
- **Day 31** — Reflection Agent: learn from completed task outcomes ([`8661d09`](https://github.com/Kabilash01/Ark/commit/8661d09))
- **Day 32** — Conflict resolver: schedule competing tasks by priority ([`f11d0c9`](https://github.com/Kabilash01/Ark/commit/f11d0c9))
- **Day 33** — Privacy mode: enforce CLI and API task flags ([`74dbad5`](https://github.com/Kabilash01/Ark/commit/74dbad5))
- **Day 34** — Workers: add web search and scoped file operations ([`c1c342c`](https://github.com/Kabilash01/Ark/commit/c1c342c))
- **Day 35** — Week 5 review: record native verification evidence ([`49c36dc`](https://github.com/Kabilash01/Ark/commit/49c36dc))
- **Day 36** — Native API: complete memory listings and privacy settings ([`f4c9720`](https://github.com/Kabilash01/Ark/commit/f4c9720))
- **Day 37** — Runtime: add done conditions, CLI controls and live events ([`53ef18f`](https://github.com/Kabilash01/Ark/commit/53ef18f))
- **Day 38** — Dashboard: serve the live agent roster and task queue ([`fac9150`](https://github.com/Kabilash01/Ark/commit/fac9150))
- **Day 39** — Dashboard: inspect tasks, browse memory and roll back versions ([`89b8afa`](https://github.com/Kabilash01/Ark/commit/89b8afa))
- **Day 40** — ARKBench: add ten tasks and a native runner ([`7e52453`](https://github.com/Kabilash01/Ark/commit/7e52453))
- **Day 41** — ARKBench: record the live Month 2 baseline ([`d92360f`](https://github.com/Kabilash01/Ark/commit/d92360f))
- **Day 42** — Month 2 review: record results and draft the architecture section ([`641a819`](https://github.com/Kabilash01/Ark/commit/641a819))
- **Day 43** — Browser tool: control Chromium from the native runtime ([`c457d3f`](https://github.com/Kabilash01/Ark/commit/c457d3f))
- **Day 44** — Browser agent: navigate and extract website evidence ([`815ae60`](https://github.com/Kabilash01/Ark/commit/815ae60))
- **Day 45** — Run Bash commands in the native Docker sandbox ([`e446673`](https://github.com/Kabilash01/Ark/commit/e446673))
- **Day 46** — Queue agents at shared tool limits ([`26a2454`](https://github.com/Kabilash01/Ark/commit/26a2454))
- **Day 47** — Add native GitHub repository and pull request tools ([`b706093`](https://github.com/Kabilash01/Ark/commit/b706093))
- **Day 48** — Benchmark ten locked-test self-repair cases ([`7181a12`](https://github.com/Kabilash01/Ark/commit/7181a12))
- **Day 49** — Document Week 7 self-repair results ([`bfd191d`](https://github.com/Kabilash01/Ark/commit/bfd191d))
- **Day 50** — Read and write native PDF and Office documents ([`5e3b7d1`](https://github.com/Kabilash01/Ark/commit/5e3b7d1))
- **Day 51** — Audit source with offline Semgrep and Bandit ([`c745df2`](https://github.com/Kabilash01/Ark/commit/c745df2))
- **Day 52** — Add the native Neo4j citation graph ([`a31a676`](https://github.com/Kabilash01/Ark/commit/a31a676))
- **Day 53** — Preserve conflicting skill revisions and queue review ([`c2bfdb6`](https://github.com/Kabilash01/Ark/commit/c2bfdb6))
- **Day 54** — Add the native llama.cpp inference backend and benchmark ([`c40e502`](https://github.com/Kabilash01/Ark/commit/c40e502))
- **Day 55** — Verify the fourteen-component defensive workflow ([`ca7f8a1`](https://github.com/Kabilash01/Ark/commit/ca7f8a1))
- **Day 56** — Record Month 3 results and validate structured inference ([`b2527b3`](https://github.com/Kabilash01/Ark/commit/b2527b3))
- **Day 57** — Reconcile disputed research and code outputs ([`002107e`](https://github.com/Kabilash01/Ark/commit/002107e))
- **Day 58** — Measure defensive WebGoat source audit recall ([`0abe2ec`](https://github.com/Kabilash01/Ark/commit/0abe2ec))
- **Day 59** — Bound active memory and archive older episodes ([`185b8d6`](https://github.com/Kabilash01/Ark/commit/185b8d6))
- **Day 60** — Reduce memory retrieval overhead and record native timings ([`ee30022`](https://github.com/Kabilash01/Ark/commit/ee30022))
- **Day 61** — Keep local workflows running through service outages ([`fe78d08`](https://github.com/Kabilash01/Ark/commit/fe78d08))
- **Day 62** — Visualize task dependencies and measured agent scores ([`7ee7ba9`](https://github.com/Kabilash01/Ark/commit/7ee7ba9))
- **Day 63** — Record Week 9 verification and remaining limits ([`9f202a2`](https://github.com/Kabilash01/Ark/commit/9f202a2))
- **Day 64** — Score thirty ARKBench v0.5 tasks with versioned evidence ([`c1bd28b`](https://github.com/Kabilash01/Ark/commit/c1bd28b))
- **Day 65** — Measure reflection guidance on 112 coding runs ([`43a950f`](https://github.com/Kabilash01/Ark/commit/43a950f))
- **Day 66** — Balance research across two rated worker slots ([`7d7827e`](https://github.com/Kabilash01/Ark/commit/7d7827e))
- **Day 67** — Capture sanitized conflict outcomes ([`ac262c8`](https://github.com/Kabilash01/Ark/commit/ac262c8))
- **Day 68** — Stress test twenty concurrent tasks ([`2761fea`](https://github.com/Kabilash01/Ark/commit/2761fea))
- **Day 69** — Run fifty versioned ARKBench cases ([`62eb8e7`](https://github.com/Kabilash01/Ark/commit/62eb8e7))
- **Day 70** — Close out Month 4 results and papers ([`604ef8e`](https://github.com/Kabilash01/Ark/commit/604ef8e))
- **Days 71–72** — Add native scientific Vision Agent ([`983e526`](https://github.com/Kabilash01/Ark/commit/983e526))
- **Day 73** — Add native ML optimization agent ([`cdf5832`](https://github.com/Kabilash01/Ark/commit/cdf5832))
- **Day 74** — Run native linear regression experiments ([`623a90c`](https://github.com/Kabilash01/Ark/commit/623a90c))
- **Day 75** — Add restricted native DevOps runner ([`f2b9c34`](https://github.com/Kabilash01/Ark/commit/f2b9c34))
- **Day 76** — Add native CSV analysis agent ([`1d76cfd`](https://github.com/Kabilash01/Ark/commit/1d76cfd))
- **Day 78** — Add native hypothesis engine ([`0b99b5b`](https://github.com/Kabilash01/Ark/commit/0b99b5b))
- **Day 79** — Add native experiment designer ([`20dfc35`](https://github.com/Kabilash01/Ark/commit/20dfc35))
- **Day 80** — Add native scientific report generator ([`c39171a`](https://github.com/Kabilash01/Ark/commit/c39171a))
- **Day 81** — Add resumable SAGE checkpoints ([`ab7fc8e`](https://github.com/Kabilash01/Ark/commit/ab7fc8e))
- **Day 82** — Add persistent native task scheduler ([`02e2c19`](https://github.com/Kabilash01/Ark/commit/02e2c19))
- **Day 83** — Add native Materials Project client ([`7ec73e2`](https://github.com/Kabilash01/Ark/commit/7ec73e2))
- **Day 84** — Add checkpointed SAGE materials workflow ([`58914b1`](https://github.com/Kabilash01/Ark/commit/58914b1))
- **Day 85** — Ground hypotheses in cited evidence ([`0382dd8`](https://github.com/Kabilash01/Ark/commit/0382dd8))
- **Day 86** — Add native VLM runtime check ([`6a97ec1`](https://github.com/Kabilash01/Ark/commit/6a97ec1))
- **Day 87** — Carry SAGE sources into reports ([`06d84a2`](https://github.com/Kabilash01/Ark/commit/06d84a2))
- **Day 89** — Run a second SAGE hypothesis automatically ([`48181eb`](https://github.com/Kabilash01/Ark/commit/48181eb))
- **Day 90** — Add Vision figure captions to SAGE ([`739038f`](https://github.com/Kabilash01/Ark/commit/739038f))
- **Day 91** — Review native SAGE and VLM status ([`aa8e28f`](https://github.com/Kabilash01/Ark/commit/aa8e28f))
- **Day 92** — Add gated native prompt self-improvement ([`ca836da`](https://github.com/Kabilash01/Ark/commit/ca836da))
- **Day 93** — Add evidence-based capability discovery ([`48a947e`](https://github.com/Kabilash01/Ark/commit/48a947e))
- **Day 94** — Record native VLM training constraints ([`3f4b46c`](https://github.com/Kabilash01/Ark/commit/3f4b46c))
- **Day 95** — Add native local voice input ([`8dc52e6`](https://github.com/Kabilash01/Ark/commit/8dc52e6))
- **Day 96** — Audit all-agent integration readiness ([`c1d099b`](https://github.com/Kabilash01/Ark/commit/c1d099b))
- **Day 98** — Review Month 5 evidence and open work ([`8b9304b`](https://github.com/Kabilash01/Ark/commit/8b9304b))
- **Day 103** — Analyze matched self-improvement evidence ([`4ccb308`](https://github.com/Kabilash01/Ark/commit/4ccb308))
- **Day 105** — Clarify architecture results and limits ([`67b7f1d`](https://github.com/Kabilash01/Ark/commit/67b7f1d))
- **Day 106** — Document native execution and memory methods ([`a74a397`](https://github.com/Kabilash01/Ark/commit/a74a397))
- **Day 107** — Draft SAGE methods and evidence limits ([`19c4b12`](https://github.com/Kabilash01/Ark/commit/19c4b12))
- **Day 108** — Draft ARKBench methods and results ([`87264fe`](https://github.com/Kabilash01/Ark/commit/87264fe))
- **Day 109** — Add native MCP server ([`bcdbd6c`](https://github.com/Kabilash01/Ark/commit/bcdbd6c))
- **Day 110** — Review Week 15 deliverables ([`3df6714`](https://github.com/Kabilash01/Ark/commit/3df6714))
- **Day 111** — Prepare public release documentation ([`bf8747a`](https://github.com/Kabilash01/Ark/commit/bf8747a))
- **Day 112** — Prepare architecture paper submission package ([`5d0f354`](https://github.com/Kabilash01/Ark/commit/5d0f354))
- **Day 113** — Prepare evidence-based demo script ([`af2acfd`](https://github.com/Kabilash01/Ark/commit/af2acfd))
- **Day 114** — Prepare ARKBench preprint package ([`50004b0`](https://github.com/Kabilash01/Ark/commit/50004b0))
- **Day 115** — Record native VLM paper scope ([`978af5f`](https://github.com/Kabilash01/Ark/commit/978af5f))
- **Day 116** — Add conflict paper analysis protocol ([`6250def`](https://github.com/Kabilash01/Ark/commit/6250def))
- **Day 117** — Add C++ Obsidian and Notion export ([`a8cca3e`](https://github.com/Kabilash01/Ark/commit/a8cca3e))
- **Day 118** — Add native deployment guide ([`b4f56ac`](https://github.com/Kabilash01/Ark/commit/b4f56ac))
- **Day 119** — Add architecture map and status ([`69afd33`](https://github.com/Kabilash01/Ark/commit/69afd33))
- **Day 120** — Audit day-tagged commit history and release blockers ([`a38f2e9`](https://github.com/Kabilash01/Ark/commit/a38f2e9))

## Post-plan follow-up — 2026-11-25

- Routed explicit `EXECUTE` goals through the supervised native terminal worker
  in the restricted Docker sandbox. Planner and kernel tests cover the typed
  route and the native-tool opt-in boundary; a live planner-to-Docker run is
  still unverified.
- Added typed `security_agent` tasks to saved-DAG dispatch with a 64 KiB payload
  cap and host report-path rejection. Source scans still require Docker.
- Added typed saved-DAG document writes for DOCX, XLSX, and PDF. Output paths
  are derived from task IDs beneath `ARK_DOCUMENT_OUTPUT_DIR`; reads and
  planner-supplied paths remain unavailable through this route.
- Routed `memory_agent` `STORE` and `SEARCH` tasks through typed episodic
  operations using SQLite, rejecting arbitrary paths and unsupported actions.
- Routed `data_analysis_agent` through saved DAGs for bounded inline CSV input;
  host file paths and CSV payloads above 60 KiB are rejected.
- Routed scientific report generation through saved DAGs with supplied
  references and inline LaTeX output; citation-graph queries are disabled.
- Routed the ML Engineer's linear-regression experiment through saved DAGs with
  bounded inline numeric data and no MLflow side effects.
- Routed configured File System Agent workspace operations through saved DAGs;
  roots remain aliases from local configuration and write payloads are capped.
- Routed Capability Discovery through saved DAGs using bounded inline registry,
  reflection, and benchmark evidence without filesystem or network writes.
- Added read-only GitHub open-issue listing to saved DAGs; PR, push, clone, and
  commit actions remain excluded from that route.
- Routed Experiment Designer tasks through configured local inference with a
  single bounded hypothesis payload.
- Routed bounded DevOps build/run tasks through the existing network-disabled
  Docker execution path.
- Routed Hypothesis Engine tasks through the configured local model, semantic
  memory, and citation graph services.
- Added saved-DAG Coding Agent prompt proposals from stored failures; the
  ten-case A/B promotion gate remains separate.
- Routed read-only Materials Project summary queries through saved DAGs with
  bounded IDs/fields and a locally configured endpoint.
- Routed natural-language `DESIGN_EXPERIMENT` and `HYPOTHESIZE` goals to their
  typed specialists, with planner fixture coverage and existing bounded payload
  validation enforced at DAG dispatch.
- Routed natural-language Materials Project queries using explicitly supplied
  material IDs through the bounded saved-DAG client adapter.
- Routed self-improvement goals to bounded Coding Agent prompt proposals;
  evaluation and promotion remain gated by the existing A/B workflow.
- Routed natural-language remember/recall requests to typed episodic Memory
  Agent STORE and SEARCH tasks.
- Routed evaluation-only goals through `ark-cpp run` to the Evaluation Agent.

## No standalone day-tagged commit

The following roadmap days have no dedicated `[Day NN]` commit in the repository history. This is a history audit only: work may have been combined into another commit or remain open. Check [project status](docs/project_status.md) and [the Week 15 review](docs/week15_review.md) before treating an item as complete.

77, 88, 97, 99–102, 104

## Current release status

The repository is public and the default branch has been pushed through the Day 120 changelog audit. No `v1.0.0` tag, GitHub Release, paper submission, preprint upload, or demo publication is recorded. The all-20-agent runtime integration and the live service-dependent workflows remain open; see the status report for evidence and constraints.

