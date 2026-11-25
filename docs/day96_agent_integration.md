# Day 96: all-agent integration status

The guide's single-task flagship integration has not passed and was not run as
a live task. The source currently makes that test impossible through the normal
`ark-cpp run` path:

- `run_goal` passes at most `research_agent`, `coding_agent`, and
  `evaluation_agent` to the planner (or only `coding_agent` for code-only work).
  `memory_agent` appears in the standalone `plan` command, but its task handler
  is not implemented in the shared dispatch path.
- The generic dispatcher has explicit branches for coding, evaluation, web
  search, browser, and vision; other agent names fall through to the Research
  Agent. The normal run path validates plans without enabling the native
  document, terminal, and security worker set.
- Specialist C++ modules such as DevOps, ML Engineer, Data Analysis, GitHub,
  Materials Project, Experiment Designer, and Scientific Report are currently
  exposed as direct CLI workflows rather than unified planner workers.
- Ollama, Redis, Qdrant, and Neo4j are unreachable on loopback in this
  environment, so model-backed planning and the requested services are also
  unavailable for a live demo.

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
the all-agent criterion. Document and security workers still need typed,
bounded inputs and dedicated adapters, and the DevOps, analysis, GitHub, and
science specialists remain direct workflows. The live planner → terminal →
Docker → evaluator flow has not been run here because model and service
availability remains gated.

Saved-DAG `ark-cpp dispatch` also routes typed `security_agent` requests. Its
payload is capped at 64 KiB, uses inline source text or caller-supplied local
advisories, and rejects host report paths. The source scanner still requires
Docker. Typed `document_agent` saved-DAG tasks now support DOCX, XLSX, and PDF
writes with payload-derived content and filenames confined to
`ARK_DOCUMENT_OUTPUT_DIR`; read actions and caller-selected paths are rejected.
These are explicit saved-plan integrations; the normal natural-language
planner does not invent source files, document content, or automatically
schedule these specialist tasks. The live model-to-tool flows remain unverified
when their local services are unavailable.
