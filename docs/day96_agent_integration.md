# Day 96: all-agent integration status

The guide's single-task flagship integration has not passed and was not run as
a live task. `ark-cpp dispatch` now routes typed saved-DAG tasks through
adapters for memory, file system, document generation, security, CSV analysis,
ML linear regression, scientific report generation, terminal execution,
DevOps, experiment design, Materials Project summaries, GitHub issue listing,
capability discovery, self-improvement proposals, coding, evaluation, browser,
vision, and web search.
These routes are not all exposed
to natural-language planning. Hypothesis Engine is service-gated, and
self-improvement DAG tasks stop at proposal generation; GitHub is limited to
read-only issue listing, and Capability Discovery requires explicit evidence.
Ollama, Redis, Qdrant, and Neo4j are unreachable on loopback here, so the full
model and service-backed integration has not been demonstrated.

Day 96 therefore remains open. Completing it requires a common worker adapter
for the existing native modules, dependency-aware routing for the materials,
prediction, security, deployment, and paper stages, plus the local services
needed by those stages. Existing unit tests verify individual agents and DAG
execution; they do not establish that all 20 activate in one task.

## Follow-on implementation update — 2026-11-25

The normal `ark-cpp run` path now accepts an `EXECUTE` classification, constrains
the planner to `terminal_agent`, and dispatches its literal command through the
existing restricted Docker sandbox. The supervisor enables the native-tool DAG
path only for this explicit execution flow. Kernel and planner fixture tests
cover the intent/worker contract and native-tool opt-in.

This closes the earlier terminal-routing gap for one intent; it does not close
the all-agent criterion. Several specialists still need adapters or normal
planner routes, and the live planner → terminal → Docker → evaluator flow has
not been run here because model and service availability remains gated.

Saved-DAG `ark-cpp dispatch` also routes typed `security_agent` requests. Its
payload is capped at 64 KiB, uses inline source text or caller-supplied local
advisories, and rejects host report paths. The source scanner still requires
Docker. Typed `document_agent` saved-DAG tasks now support DOCX, XLSX, and PDF
writes with payload-derived content and filenames confined to
`ARK_DOCUMENT_OUTPUT_DIR`; read actions and caller-selected paths are rejected.
The previously planner-visible `memory_agent` now has explicit episodic
`STORE` and `SEARCH` adapters using the configured SQLite database, with
bounded action schemas and no task-selected database path.
The deterministic Data Analysis Agent also runs as a typed saved-DAG worker
using inline CSV input capped at 60 KiB; direct CLI analysis retains its larger
10 MiB file-input mode.
The Scientific Report Generator accepts bounded supplied study results and
references, returns LaTeX inline, and has citation-graph lookups disabled in
saved-DAG dispatch.
The ML Engineer also supports a deterministic inline linear-regression path
with explicit row, feature, trial, epoch, and total-compute limits; it does not
enable MLflow logging through saved DAGs.
The File System Agent now routes list/read/write tasks through the configured
workspace aliases, with relative-path validation and a 48 KiB saved-DAG write
limit.
Capability Discovery now accepts inline registry, reflection, and benchmark
evidence and answers a bounded `DISCOVER` question without writing a file.
GitHub saved-DAG support is limited to read-only open-issue listing for a
validated repository; PR creation, commits, pushes, and cloning remain outside
this route.
Experiment Designer tasks now accept a bounded hypothesis and call the
configured local inference provider; end-to-end execution requires that local
model service.
DevOps tasks now use the existing network-disabled container runner with a
bounded inline Docker build context and argument-array command; the Docker
daemon remains a runtime requirement.
Hypothesis Engine tasks now route a bounded domain to the configured local
inference, semantic memory, and citation graph services. Those backends remain
required for a live run.
Self-improvement tasks can generate Coding Agent proposals from stored failure
evidence; benchmark comparison and promotion remain in the separate ten-case
A/B workflow.
These are explicit saved-plan integrations; the normal natural-language
planner does not invent source files, document content, or automatically
schedule these specialist tasks. The live model-to-tool flows remain unverified
when their local services are unavailable.

## Natural-language route follow-up — 2026-11-25

`ark-cpp run` now also exposes bounded routes for experiment design, hypothesis
generation, Materials Project summaries, Coding Agent improvement proposals,
episodic memory STORE/SEARCH, Evaluation Agent assessment, and Security Agent
dependency checks. The Materials Project route requires user-supplied IDs;
security checks use user-supplied pinned versions and local advisories. These
routes do not establish the Day 96 single-task all-agent integration. Normal
planning still does not expose every typed saved-DAG specialist, and local
inference, database, vector, graph, Docker, and external API availability remain
necessary for live end-to-end verification of their respective flows.

Capability Discovery now accepts a natural-language question through the
planner. For question-only tasks, dispatch loads registry, reflection, and
benchmark evidence from configured local stores. It fails explicitly when the
agent registry is unavailable, so an empty map is not presented as proof that
ARK lacks a capability. The live path still needs Redis and a local model.
