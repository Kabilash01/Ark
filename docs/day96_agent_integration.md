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
