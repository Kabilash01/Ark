# ARK OS — Product Requirements, Design & Implementation Plan

**Project:** ARK OS (Autonomous Reasoning Kernel — Operating System)
**Optional Mode:** SAGE (Scientific Autonomous Generation Engine)
**Version:** 1.1 (corrected from v1.0)
**Status:** Draft for build kickoff
**Horizon:** 6-month build (20 deeply-built agents) → plugin-ready for 70+

### Changelog: v1.0 → v1.1
| Fix | Section | Change |
|---|---|---|
| #1 | §8 Architecture | Web Search + File System moved from Phase 1 to Phase 2 workers |
| #2 | §8 Kernel Tier | Reflection Agent removed from kernel; Agent Registry correctly placed |
| #3 | §18 Month 2 | Removed Phase 2 agents; Month 2 now correctly closes MVP loop only |
| #4 | §10 ARKMessage | Added missing fields: `timestamp`, `output`, `quality_score` |
| #5 | §13 Tech Stack | Added MLflow, Optuna, Semgrep, Bandit, Docker + Docker Compose |
| #6 | §13 Hardware | Added GPU VRAM verification note |

---

# Part I — Product Requirements Document (PRD)

## 1. Vision

> A self-orchestrating, multi-agent operating system with a persistent memory layer, a shared tool ecosystem, and domain-specialized agents — that operates like an OS, not a chatbot.

ARK OS is not a wrapper around an LLM. It is an operating system for AI agents, with:

- A **kernel** that schedules work and arbitrates between agents
- **Processes** (specialized agents) that run in parallel and hand off tasks
- A **unified four-tier memory subsystem** all agents read and write through one interface
- An **I/O layer** (web, terminal, files, APIs, sensors)
- A **strict inter-process communication protocol** (AgentSpec v1.0) every agent must follow

When ARK OS shifts from task execution to autonomous knowledge generation, it operates in **SAGE mode** — the same 20 agents, re-orchestrated toward forming hypotheses, running computational experiments, and producing publishable findings.

---

## 2. Problem Statement

Today's agent tools are fragmented and shallow:

| Existing Tool | Limitation |
|---|---|
| ChatGPT / Claude | Single turn, no durable memory, no autonomy |
| AutoGPT / BabyAGI | Single domain, brittle, no inter-agent communication |
| GitHub Copilot | Code only, reactive, no initiative |
| Devin | Coding only, closed source, no research/security domains |
| OpenManus / Manus | General agent but no OS-level persistent memory |

No system provides a unified OS where agents **share memory, hand off tasks, and improve each other's output over time**. That shared layer is the core contribution ARK OS makes.

---

## 3. Goals & Non-Goals

### Goals
- **G1.** Build a kernel that routes, plans, schedules, and arbitrates work across agents.
- **G2.** Build a unified four-tier memory layer (working, episodic, semantic, procedural) that all agents read and write through one interface.
- **G3.** Define and enforce a single inter-agent communication protocol (**AgentSpec v1.0**) — frozen before any agent is built.
- **G4.** Deliver 20 deeply-built agents across 3 phases, with a plugin architecture so adding agent #21+ takes ~2 hours, not 2 weeks.
- **G5.** Run the core stack locally on consumer hardware (RTX 5060, 32GB RAM, Ryzen 9) with an optional API call for the largest orchestration model only.
- **G6.** Produce evaluation benchmarks (ARKBench) and extract 4–5 publishable research contributions.

### Non-Goals
- **NG1.** Do **not** build 90 agents in 6 months. Stub agents are worthless; depth over breadth.
- **NG2.** Do **not** require wet-lab experiments — SAGE is restricted to computational experiments only.
- **NG3.** Do **not** target all scientific domains at once — pick one domain for the flagship paper.
- **NG4.** Do **not** use external APIs for security-sensitive work — Security Agent stays fully local.
- **NG5.** Do **not** modify the kernel once Phase 1 ships — new agents are plugins only.

---

## 4. Guiding Principles

1. **Memory first.** Build the Memory Agent before any other agent. Every agent depends on it; build Supervisor first and you'll rewrite it when memory changes.
2. **Protocol before agents.** Freeze AgentSpec v1.0 before writing agent #1. One format, no exceptions.
3. **Depth over breadth.** 20 capable agents beat 90 shallow wrappers every time.
4. **Pluggability.** Adding agent #91 should take ~2 hours — a new class that extends BaseAgent and registers itself.
5. **Three-tier separation.** Kernel, domain workers, and intelligence (meta/self-improving) agents are strictly distinct layers.
6. **Stateless agents.** Every agent is stateless. All state lives in the Memory Agent. Agents can crash and restart without context loss.

---

## 5. Users & Use Cases

- **Primary user:** the builder/researcher (you), running autonomous knowledge work, coding, and scientific discovery.
- **Future users:** community contributors adding plugin agents; labs and companies running knowledge-work pipelines.

**Representative task:**
> "Research transformer architectures and build me a working implementation."

Expected flow: Supervisor → Planner → (Research ∥ Memory in parallel) → Coding → Evaluation → (Debug retry if fail | Reflection if pass).
**Target:** ~4–8 minutes, end-to-end, autonomous.

---

## 6. Success Metrics

| ID | Metric | Target |
|---|---|---|
| M1 | Memory persistence | store/retrieve/forget verified across process restarts |
| M2 | Planner DAG accuracy | Correct task DAG for ≥ 90% of trial goals |
| M3 | Coding Agent self-repair | Measurable fix rate on injected regressions in open-source repos |
| M4 | End-to-end MVP task | "Research X → implement it" completes autonomously with passing Evaluation |
| M5 | Continual improvement | ARK OS at Month 6 outperforms Month 1 on same benchmark (ARKBench) |
| M6 | Research output | ≥ 1 architecture paper submitted; pipeline for 4–5 total |

---

## 7. Publications (Target Outputs)

| # | Paper Title | Target Venue |
|---|---|---|
| 1 | ARK OS Architecture — kernel, AgentSpec, four-memory design, ARKBench | NeurIPS / ICLR |
| 2 | Inter-Agent Conflict Resolution in Autonomous Multi-Agent Systems | AAMAS |
| 3 | Autonomous Hypothesis Generation and Ranking from Scientific Literature (SAGE) | Nature Machine Intelligence |
| 4 | ARKBench — Multi-Domain Benchmark for Autonomous AI Agents | ACL / NeurIPS Datasets |
| 5 | Domain Case Study — Climate or Materials Science Discovery via SAGE | Domain-specific top journal |

---

# Part II — Design Document

## 8. System Architecture (Three Tiers)

> **FIX #1 + #2 applied here** — Web Search and File System are Phase 2 workers (not Phase 1). Reflection Agent is a Phase 2 worker (not a kernel agent). Agent Registry is correctly placed in the kernel.

```
ARK OS
│
├── TIER 1 — KERNEL (build first, always running, non-negotiable)
│   ├── Supervisor Agent   — routes tasks, prioritizes, arbitrates, final authority
│   ├── Planner Agent      — breaks goals into task DAGs with dependencies
│   ├── Memory Agent       — unified R/W across all 4 memory types for every agent
│   ├── Evaluation Agent   — scores outputs, triggers retry or escalation
│   ├── Agent Registry     — live catalog: what agents exist, capabilities, load, status
│   └── [Research + Coding are Phase 1 WORKERS, not kernel agents]
│
├── TIER 2 — DOMAIN WORKER AGENTS (started on-demand by kernel)
│   ├── Phase 1 (Month 1–2)    Research, Coding
│   ├── Phase 2 (Month 3–4)    Web Search, Browser, Terminal, File System,
│   │                           GitHub, Document, Security, Reflection
│   └── Phase 3 (Month 5–6)    Vision, ML Engineer, DevOps, Data Analysis,
│                               Self-Improvement, Capability Discovery
│
└── TIER 3 — TOOL ECOSYSTEM (tools have no agency; agents call tools)
    Playwright · Bash · Git · File System · APIs · Code Sandbox · Scanners
```

**Key principle:** Agents are stateless. All state lives in the Memory Agent. A crashed agent restarts clean — the Memory Agent holds everything it needs to continue.

---

## 9. The 20 Agents (Locked)

### Phase 1 — Kernel + First Workers (Month 1–2)

| # | Agent | Single Responsibility |
|---|---|---|
| 1 | Supervisor | Routes tasks, arbitrates conflicts, maintains final authority |
| 2 | Planner | Decomposes any goal into a directed acyclic graph (DAG) of subtasks |
| 3 | Memory | Unified R/W across all 4 memory tiers for every agent |
| 4 | Research | arXiv, Semantic Scholar, RAG pipeline, literature synthesis |
| 5 | Coding | Write, test, debug, and evolve code autonomously |
| 6 | Evaluation | Scores every agent output 0–1; triggers retry or acceptance |

> **Note:** Agent Registry is a kernel service (always running), not a numbered worker agent.

### Phase 2 — Core Workers (Month 3–4)

| # | Agent | Single Responsibility |
|---|---|---|
| 7 | Web Search | Real-time web grounding via Tavily / SerpAPI |
| 8 | Browser | Full browser automation via Playwright |
| 9 | Terminal | Bash execution, process management, output parsing |
| 10 | File System | Read, write, organize, watch, and index local files |
| 11 | GitHub | PRs, commits, issues, repository health analysis |
| 12 | Document | PDF, DOCX, XLSX — parse and generate structured content |
| 13 | Security | CVE feeds, vulnerability scanning, threat detection, audit reports |
| 14 | Reflection | Post-task learning — updates agent strategies from outcomes |

### Phase 3 — Intelligence Layer (Month 5–6)

| # | Agent | Single Responsibility |
|---|---|---|
| 15 | Vision | Screenshot analysis, image understanding, OCR pipeline |
| 16 | ML Engineer | Experiment execution, MLflow tracking, Optuna HPO |
| 17 | DevOps | Docker management, CI/CD orchestration, deployment automation |
| 18 | Data Analysis | Pandas pipelines, visualization, automated report generation |
| 19 | Self-Improvement | Analyzes Reflection logs; proposes and tests agent prompt upgrades |
| 20 | Capability Discovery | Maintains live map of what ARK OS can and cannot do |

> **Design note:** Narrow utilities (Clipboard, OCR standalone, Backup, Reminder, Journal, Expense Tracker, single-app messengers) are **tools or sub-functions inside existing agents**, not separate agents. This collapses ~15 of the originally proposed agents into ~7 more capable ones.

---

## 10. Inter-Agent Communication — AgentSpec v1.0

**This is the most critical design decision. Freeze this before writing agent #1. Every agent reads this format. Every agent writes this format. No exceptions.**

> **FIX #4 applied here** — Added `timestamp`, `output`, and `quality_score` fields missing from v1.0.

```python
class ARKMessage(TypedDict):
    # ── Routing ──────────────────────────────────────────────────
    task_id:        str    # unique task identifier (ark_{date}_{seq})
    sender:         str    # originating agent name
    receiver:       str    # target agent name
    parent_task_id: str    # for subtask chains; empty string if root task

    # ── Intent & Content ─────────────────────────────────────────
    intent:         str    # RESEARCH | CODE | SEARCH | EXECUTE |
                           # STORE | EVALUATE | REFLECT | DISCOVER
    payload:        dict   # task-specific content (instruction, files, etc.)
    memory_context: list   # relevant memories injected by Memory Agent

    # ── Control ──────────────────────────────────────────────────
    priority:       int    # 1 (low) to 10 (critical)
    status:         str    # PENDING | RUNNING | DONE | FAILED | RETRY
    retry_count:    int    # current attempt number
    max_retries:    int    # cap before Supervisor escalation (default: 3)

    # ── Output (populated on completion) ─────────────────────────
    output:         dict   # agent result — populated when status = DONE
    quality_score:  float  # 0.0–1.0 set by Evaluation Agent; -1.0 if not yet scored
    timestamp:      str    # ISO 8601 creation time — used by episodic memory
```

**Wire example:**
```json
{
  "task_id": "ark_20260615_001",
  "sender": "planner_agent",
  "receiver": "coding_agent",
  "intent": "CODE_GENERATION",
  "payload": {
    "instruction": "Write a FastAPI endpoint for user authentication",
    "context_files": ["main.py", "models.py"]
  },
  "memory_context": [],
  "priority": 8,
  "parent_task_id": "ark_20260615_000",
  "status": "PENDING",
  "retry_count": 0,
  "max_retries": 3,
  "output": {},
  "quality_score": -1.0,
  "timestamp": "2026-06-15T10:30:00Z"
}
```

---

## 11. Memory Architecture (Four Tiers)

**The Memory Agent is the single most important agent in the system. Build it first.**

All agents read and write through the Memory Agent only — never touching storage directly. This keeps agents stateless and the memory layer the single source of truth.

| Tier | What It Holds | Backing Store | Retention |
|---|---|---|---|
| Working | Current task context, active variables | Python dict + Redis | Task duration only |
| Episodic | History of past tasks, outcomes, timelines | SQLite + timestamps | Persistent across sessions |
| Semantic | Facts, paper knowledge, code patterns | Qdrant vector DB | Persistent, similarity retrieval |
| Procedural | How-to skills, agent strategy library | JSON store + FAISS index | Persistent, intent retrieval |

**Memory Agent API (minimum viable interface):**
```python
store(memory_type, key, value, metadata)
retrieve(query, memory_type, top_k)    # semantic search for semantic/episodic
forget(memory_type, key | policy)      # TTL or relevance-based eviction
inject_context(task)                   # populates memory_context on outgoing ARKMessages
```

**Acceptance criterion:** Memory must persist and be fully retrievable across process restarts with zero data loss.

---

## 12. Model Strategy

| Layer | Model | Agents | Runtime |
|---|---|---|---|
| Orchestration | Qwen2.5-72B | Supervisor, Planner, Reflection, Self-Improvement | Groq API (free tier, ~500 tok/s) |
| Local General | Qwen2.5-7B | Memory, Evaluation, Web Search, File System, Security, Data Analysis | Ollama local |
| Local Code | Qwen2.5-Coder-14B | Coding, Debugging, GitHub, ML Engineer, DevOps | Ollama local |
| Local Vision | Qwen2-VL-7B | Vision, Screenshot Analysis, OCR | Ollama local |
| Local Speech | Whisper-large-v3 | Voice / STT pipeline (optional Phase 3) | faster-whisper local |

**Strategy notes:**
- Qwen2.5-72B via Groq is the reasoning brain — used only for orchestration and reflection decisions.
- Qwen2.5-14B local is the offline fallback if Groq is unavailable.
- Security Agent never calls external APIs under any circumstance — fully local only.
- Models are loaded on-demand by Ollama and swapped based on task type to fit within VRAM budget.

---

## 13. Infrastructure & Tech Stack

> **FIX #5 applied here** — Added MLflow, Optuna, Semgrep, Bandit, Docker + Docker Compose.

```
┌─────────────────────────────────────────────────────────────────┐
│  LAYER                  TECHNOLOGY                              │
├─────────────────────────────────────────────────────────────────┤
│  Orchestration          LangGraph (state machines + DAG)        │
│                         LangChain (tool abstractions)           │
├─────────────────────────────────────────────────────────────────┤
│  Memory                 Qdrant      — semantic + procedural      │
│                         Redis       — working memory + task queue│
│                         SQLite      — episodic memory + logs     │
│                         Neo4j       — knowledge graph (optional) │
├─────────────────────────────────────────────────────────────────┤
│  Model Serving          Ollama      — local model management     │
│                         Groq API    — 72B orchestration calls    │
├─────────────────────────────────────────────────────────────────┤
│  Agent Tools            Playwright  — browser automation         │
│                         Paramiko    — terminal / SSH             │
│                         GitPython   — GitHub operations          │
│                         pypdf       — PDF ingestion              │
│                         python-docx — DOCX generation            │
│                         openpyxl    — XLSX processing            │
├─────────────────────────────────────────────────────────────────┤
│  ML / Experiment        MLflow      — experiment tracking        │
│                         Optuna      — hyperparameter optimization│
├─────────────────────────────────────────────────────────────────┤
│  Security Tools         Semgrep     — static code analysis       │
│                         Bandit      — Python vulnerability scan  │
│                         python-nmap — network scanning (local)   │
├─────────────────────────────────────────────────────────────────┤
│  Interface              FastAPI     — REST API for user tasks    │
│                         WebSocket   — live agent monitoring UI   │
├─────────────────────────────────────────────────────────────────┤
│  Containerization       Docker + Docker Compose                  │
│                         — full system reproducibility            │
│                         — isolated code execution sandbox        │
│                         — SAGE experiment versioning             │
└─────────────────────────────────────────────────────────────────┘
```

### Hardware Fit

> **FIX #6 note** — Verify your exact GPU model before locking the model plan. RTX 5060 (base) = 8GB VRAM. RTX 5060 Ti = 16GB VRAM. These require different model strategies.

| Component | Spec | Verdict |
|---|---|---|
| GPU — if RTX 5060 Ti (16GB) | Qwen2.5-7B ≈ 5GB + Coder-14B ≈ 9GB | ✅ Both fit; full plan as written |
| GPU — if RTX 5060 (8GB) | Coder-14B does NOT fit (9GB) | ⚠️ Use Qwen2.5-Coder-7B locally; call 14B via Groq |
| RAM — 32GB | Redis + Qdrant + 3 Ollama model slots | ✅ Comfortable |
| CPU — Ryzen 9 8945HS | LangGraph + FastAPI + Docker orchestration | ✅ Overkill (excellent) |
| OS — Ubuntu Linux | Native Ollama, Docker, all Python tooling | ✅ Ideal platform |

**Phase 1 + 2 stack runs fully locally. Only the 72B orchestrator optionally calls out to Groq.**

### Docker Services Stack

```yaml
# docker-compose.yml services
qdrant:latest          # port 6333  — vector memory
redis:7-alpine         # port 6379  — working memory + message bus
mlflow:latest          # port 5000  — SAGE experiment tracking
ark-os-api:latest      # port 8000  — FastAPI REST interface
ark-os-dashboard       # port 3000  — WebSocket monitoring UI
ark-sandbox            # isolated   — safe code execution environment
```

---

## 14. Reference Task Flow

```
User: "Research transformer architectures and build a working implementation"

   → Supervisor Agent
     (classifies intent, sets priority, checks capability registry)

   → Planner Agent
     (generates task DAG: research → summarize → implement → test → evaluate)

   → [Research Agent] ∥ [Memory Agent loads prior context]   ← parallel
     (Research reads papers + builds RAG context)

   → Coding Agent
     (implements using research output + memory context)

   → Evaluation Agent
     (scores output 0–1 against quality rubric)
         │
         ├─ score < threshold → Debugging retry (bounded by max_retries = 3)
         │                      → Escalate to Supervisor if retries exhausted
         │
         └─ score ≥ threshold → Reflection Agent
                                 (logs what worked, updates strategy weights)
                               → Memory Agent
                                 (stores outcome in episodic + semantic tiers)
                               → Return result to user

Total autonomous time: ~4–8 minutes
```

---

## 15. SAGE Mode — Scientific Discovery Loop

SAGE reuses all 20 existing ARK OS agents with a discovery-oriented orchestration goal. Only **3 new components** are added on top:

| Component | Extends | Function |
|---|---|---|
| Hypothesis Engine | Planner Agent | Reads 50+ papers, identifies contradictions and gaps, generates hypotheses ranked by novelty × feasibility score |
| Experiment Designer | ML Engineer Agent | Translates a hypothesis into a full computational experiment spec — dataset, method, baselines, metrics — and emits runnable code |
| Scientific Report Generator | Document Agent | Assembles hypothesis + experiment + results into a LaTeX paper draft with auto-formatted citations |

**The SAGE discovery loop:**

```
Literature (Research + RAG Agent)
         │
         ▼
Gap Analysis → Hypothesis Ranking (Hypothesis Engine + Reflection)
         │
         ▼
Experiment Design (Experiment Designer + Coding Agent)
         │
         ▼
Execution + Tracking (ML Engineer + Terminal + DevOps)
         │
         ▼
Result Analysis (Data Analysis + Vision Agent)
         │
         ▼
Report Generation (Scientific Report Generator → LaTeX draft)
         │
         ▼
Peer Review Simulation (Evaluation Agent → revision triggers)
         │
         ▼
Knowledge Base Update (Memory Agent → loop restarts if inconclusive)
```

**Target domain — pick one:**
- **Materials Science** — fully open APIs (Materials Project), large research gaps, computational experiments only
- **Climate** — NASA/NOAA open data, simulation-based experiments, societal impact
- **Agriculture** — high-impact India-relevant pick (FAO, ICAR, NFHS datasets), strong for Indian journal submissions

### SAGE Risk Register

| Risk | Severity | Mitigation |
|---|---|---|
| Hypotheses generated are trivially known | High | Novelty scoring against full Semantic Scholar citation graph before ranking |
| Experiments take too long to run | Medium | Restrict to computational experiments; time-box all runs to 30 minutes max |
| Results are irreproducible | Low | Every run Dockerized, seeded, and logged in MLflow; one-command replay |
| Scope too broad across domains | Medium | One domain for flagship paper; architecture generalizes (documented separately) |
| SAGE not ready in 6 months | Low | SAGE is Phase 3 add-on; core ARK OS ships independently at Month 4 |

---

# Part III — Implementation Phases

## 16. Phase Overview

```
Phase 1 — Kernel        6 agents    Month 1–2    Foundation — memory, routing, planning,
                                                  first research + coding workers, evaluation

Phase 2 — Muscle       +8 agents    Month 3–4    Web, browser, terminal, file system,
                                                  GitHub, document, security, reflection

Phase 3 — Intelligence +6 agents    Month 5–6    Vision, ML engineer, DevOps, data analysis,
                                                  self-improvement, capability discovery + SAGE

─────────────────────────────────────────────────────────────────────────────────────────────
Total                   20 agents                Plugin-ready for 70+ additional agents
```

---

## 17. Month 1 — Foundation & Memory Agent (Week-by-Week)

**Week 1 — Environment Setup**
- Install Ollama; pull Qwen2.5-7B + Qwen2.5-Coder-14B (or 7B if 8GB VRAM)
- Stand up Qdrant + Redis + MLflow via Docker Compose
- Create repository structure + CI pipeline skeleton
- Freeze **ARKMessage** schema — AgentSpec v1.0 (no changes allowed after this)
- Build `BaseAgent` abstract class that all 20 agents will inherit from

**Week 2 — Memory Agent (build before everything else)**
- Implement all four memory tiers: Redis (working), SQLite (episodic), Qdrant (semantic), JSON+FAISS (procedural)
- Implement full Memory Agent API: `store`, `retrieve`, `forget`, `inject_context`
- **Acceptance:** Memory persists and retrieves correctly across full process restarts
- Write 20+ test cases covering all memory types and edge cases

**Week 3 — Supervisor + Planner Agents**
- LangGraph state machine for Supervisor (intent classification, routing, arbitration)
- Task DAG generation in Planner using Qwen2.5-72B via Groq
- Priority assignment and dependency graph construction
- **Acceptance:** A complex multi-step goal decomposes into a correct, acyclic task DAG

**Week 4 — Research + Coding Agents**
- Research Agent: arXiv API + Semantic Scholar + Qdrant RAG pipeline
- Coding Agent: code generation + unit test writing + self-repair loop (extend DevO)
- Wire both agents to the message bus and Memory Agent
- **Acceptance:** Integration test — "Research X, implement it" runs end-to-end

**Month 1 output:** Repository up, BaseAgent class, ARKMessage schema, Memory Agent with tests, Supervisor + Planner routing verified.

---

## 18. Month 2 — Complete the MVP Loop

> **FIX #3 applied here** — Web Search and File System removed (they are Phase 2 agents). Month 2 is solely about completing and hardening the 6-agent MVP kernel loop.

- **Evaluation Agent:** scoring rubric design (correctness, completeness, style, safety), retry trigger logic, escalation to Supervisor after max retries
- **Close the full loop:** Supervisor → Planner → Research ∥ Memory → Coding → Evaluation → (retry | Reflection path) → Memory update
- **Agent Registry:** live capability catalog, agent health checks, load reporting
- **Dashboard v0.1:** WebSocket panel showing live task queue, agent states, memory usage
- **ARKBench v0.1:** First 10 benchmark tasks (5 research-synthesis, 5 coding)
- **Month 1 vs Month 2 comparison:** baseline quality scores recorded for continual-improvement tracking

**Month 2 output:** Fully working 6-agent MVP. End-to-end task completes autonomously. Architecture write-up started. Phase 1 benchmark results recorded.

---

## 19. Month 3 — Phase 2 Workers, Part 1

- **Web Search Agent** (Agent #7): Tavily / SerpAPI integration, result ranking, citation grounding, < 3s latency target
- **Browser Agent** (Agent #8): Playwright full browser control, screenshot capture, form interaction, structured data extraction
- **Terminal Agent** (Agent #9): Bash execution inside Docker sandbox, process management, stdout/stderr parsing and interpretation
- **GitHub Agent** (Agent #11): PR creation, issue triage, commit analysis, repository health scoring via GitPython
- **File System Agent** (Agent #10): File read/write/watch, directory indexing, content search, change monitoring

**Month 3 output:** 11 agents operational. Coding Agent self-repair evaluation on open-source repos published internally.

---

## 20. Month 4 — Phase 2 Workers, Part 2

- **Document Agent** (Agent #12): PDF ingestion (pypdf), DOCX generation (python-docx), XLSX processing (openpyxl), structured knowledge extraction
- **Security Agent** (Agent #13): CVE feed integration, Semgrep static analysis, Bandit Python scanning, OWASP-based audit report generation — **fully local, no external API calls**
- **Reflection Agent** (Agent #14): Post-task log analysis, strategy weight updates, prompt optimization proposals fed to Self-Improvement Agent queue
- **ARKBench v0.5:** Add 10 security tasks + 10 browser/document tasks
- **14-agent integration test:** complex multi-domain task spanning 5+ agents

**Month 4 output:** 14 agents operational. Autonomous security audit methodology documented. ARKBench v0.5 released internally.

---

## 21. Month 5 — Phase 3 Intelligence Layer + SAGE Components

- **Vision Agent** (Agent #15): Qwen2-VL-7B integration, screenshot understanding, scientific figure interpretation, OCR pipeline
- **ML Engineer Agent** (Agent #16): MLflow experiment tracking, Optuna HPO, training run management, experiment logging and comparison
- **DevOps Agent** (Agent #17): Docker container lifecycle, GitHub Actions CI/CD orchestration, deployment pipeline automation
- **Data Analysis Agent** (Agent #18): Pandas/NumPy pipelines, matplotlib/plotly visualization, automated report generation
- **SAGE Components:** Hypothesis Engine, Experiment Designer, Scientific Report Generator — wired into the existing agent ecosystem
- **SAGE Loop v0.1:** Literature → Hypothesis → Experiment → Analysis loop operational

**Month 5 output:** 18 agents operational. First SAGE run attempted on chosen domain. End-to-end discovery case study in progress.

---

## 22. Month 6 — Integration, Evaluation, Paper Submissions

- **Self-Improvement Agent** (Agent #19): Analyzes Reflection logs, runs A/B tests on agent prompts, measures quality delta — demonstrates ARK OS improves over time
- **Capability Discovery Agent** (Agent #20): Builds and maintains the live capability map; answers "can ARK do X?" queries accurately
- **Full 20-agent integration testing:** end-to-end tests across all domains
- **SAGE domain case study:** complete run on chosen domain; evaluate hypothesis novelty; prepare findings
- **ARKBench final:** 50+ tasks across all domains, reproducible, ready for public release
- **Month 1 vs Month 6 improvement study:** quantify continual-improvement evidence (M5 metric)
- **Dashboard v1.0:** Production-ready WebSocket monitoring UI
- **Paper submissions:** Architecture paper (Paper #1) + SAGE Hypothesis Engine (Paper #3) submitted; ARKBench released on arXiv (Paper #4 preprint)

**Month 6 output:** Complete ARK OS + SAGE. Public GitHub repository. ≥ 3 papers submitted or in submission.

---

## 23. The Single Most Important Sequencing Rule

> **Build the Memory Agent first — before Supervisor, before Planner, before anything else.**
> Every agent depends on memory. Build Supervisor first and you will rewrite it when memory changes. Memory is the foundation. Everything sits on top of it.

---

## 24. Immediate Next Step (This Week)

Write **AgentSpec v1.0** — the message format, memory interface, and registration protocol every agent must follow. Then scaffold:

```
ark-os/
├── core/
│   ├── base_agent.py          ← BaseAgent abstract class
│   ├── ark_message.py         ← ARKMessage TypedDict + validation
│   ├── message_bus.py         ← Redis Pub/Sub wrapper
│   └── agent_registry.py      ← capability catalog service
├── memory/
│   ├── memory_agent.py        ← Memory Agent (build first)
│   ├── working_memory.py      ← Redis tier
│   ├── episodic_memory.py     ← SQLite tier
│   ├── semantic_memory.py     ← Qdrant tier
│   └── procedural_memory.py   ← JSON + FAISS tier
├── kernel/
│   ├── supervisor_agent.py
│   ├── planner_agent.py
│   └── evaluation_agent.py
├── workers/
│   ├── phase1/
│   │   ├── research_agent.py
│   │   └── coding_agent.py
│   ├── phase2/            ← populated Month 3–4
│   └── phase3/            ← populated Month 5–6
├── sage/
│   ├── hypothesis_engine.py
│   ├── experiment_designer.py
│   └── report_generator.py
├── tools/                     ← tool wrappers (no agency)
│   ├── browser_tool.py
│   ├── terminal_tool.py
│   ├── github_tool.py
│   └── security_tool.py
├── api/
│   └── main.py                ← FastAPI REST interface
├── dashboard/                 ← WebSocket monitoring UI
├── benchmarks/
│   └── arkbench/              ← ARKBench task definitions
├── docker-compose.yml
├── Dockerfile
└── README.md
```

---

## 25. Definition of Done (Per Agent)

An agent is complete only when ALL of the following are true:

1. ✅ Extends `BaseAgent` and implements the required `run(message: ARKMessage)` method
2. ✅ Speaks AgentSpec v1.0 exactly — sends and receives valid ARKMessage objects only
3. ✅ Self-registers with Agent Registry on startup (name, capabilities, model, status)
4. ✅ Reads all context via Memory Agent — never touches storage directly
5. ✅ Writes all outputs and logs to Memory Agent — episodic + semantic tiers
6. ✅ Has unit tests covering happy path, failure path, and retry behavior
7. ✅ Has at least one integration test in the full task loop with another agent
8. ✅ Passes its assigned slice of ARKBench with documented score
9. ✅ Emits structured logs with `task_id`, `timestamp`, `quality_score` on every run

---

## 26. ARKBench — Evaluation Framework

ARKBench is the evaluation suite for ARK OS. Every agent has a benchmark slice. The full suite covers all four domains.

| Category | Tasks | Key Metrics |
|---|---|---|
| Research Tasks | 10 | ROUGE/BERTScore vs human reviews, citation accuracy, hypothesis novelty |
| Coding Tasks | 15 | SWE-bench-lite pass rate, bug fix accuracy, test coverage delta |
| Security Tasks | 10 | CVE detection recall, false positive rate, audit completeness |
| Scientific Discovery | 5 | Hypothesis novelty score, experiment validity, result reproducibility |
| Agent Coordination | 10 | Multi-agent task completion rate, latency, memory accuracy, retry rate |
| **Total** | **50** | — |

**Baseline comparisons:** AutoGPT, BabyAGI, Devin (coding only), OpenManus — all evaluated on the same ARKBench suite.

---

*End of document — ARK OS PRD + Design + Implementation v1.1*
*Corrections applied: 6 issues from v1.0 review — see changelog at top.*
