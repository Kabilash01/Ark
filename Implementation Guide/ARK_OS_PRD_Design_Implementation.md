# ARK OS — Product Requirements, Design & Implementation Plan

**Project:** ARK OS (Autonomous Reasoning Kernel — Operating System)
**Optional Mode:** SAGE (Scientific Autonomous Generation Engine)
**Author:** Kabilash
**Version:** 1.0
**Status:** Draft for build kickoff
**Horizon:** 6-month build (20 deeply-built agents) → plugin-ready for 70+

---

# Part I — Product Requirements Document (PRD)

## 1. Vision

> A self-orchestrating, multi-agent operating system with a persistent memory layer, a shared tool ecosystem, and domain-specialized agents — that operates like an OS, not a chatbot.

ARK OS is not a wrapper around an LLM. It is an operating system for AI agents, with a kernel that schedules work, processes (agents) that run in parallel, a unified memory subsystem, an I/O layer (web, terminal, files, APIs), and a strict inter-process communication protocol.

When ARK OS shifts from task execution to autonomous knowledge generation, it operates in **SAGE mode** — the same agents, re-orchestrated toward forming hypotheses, running computational experiments, and producing publishable findings.

## 2. Problem Statement

Today's agent tools are fragmented and shallow:

| Existing Tool | Limitation |
|---|---|
| ChatGPT / Claude | Single turn, no durable memory, no autonomy |
| AutoGPT / BabyAGI | Single domain, brittle, no inter-agent communication |
| GitHub Copilot | Code only, reactive, no initiative |
| Devin | Coding only, closed, no research/security |
| OpenManus / Manus | General but no OS-level persistent memory |

No system provides a unified OS where agents **share memory, hand off tasks, and improve each other's output over time**. That shared layer is the contribution ARK OS makes.

## 3. Goals & Non-Goals

### Goals
- **G1.** Build a kernel that routes, plans, schedules, and arbitrates work across agents.
- **G2.** Build a unified four-type memory layer (working, episodic, semantic, procedural) that all agents read and write.
- **G3.** Define and enforce a single inter-agent communication protocol (**AgentSpec v1.0**).
- **G4.** Deliver 20 deeply-built agents across 3 phases, with a plugin architecture so agent #21+ is trivial to add.
- **G5.** Run the core stack locally on consumer hardware (RTX 5060 16GB, 32GB RAM, Ryzen 9) with an optional API for the largest orchestration model.
- **G6.** Produce evaluation benchmarks and extract 4–5 publishable research contributions.

### Non-Goals
- **NG1.** Do **not** build 90 agents in 6 months. Stub agents are worthless.
- **NG2.** Do **not** require wet-lab experiments — SAGE is restricted to computational experiments.
- **NG3.** Do **not** target all scientific domains at once — pick one for the flagship paper.
- **NG4.** Do **not** depend on closed/paid models for security-sensitive work — those stay local.

## 4. Guiding Principles
1. **Memory first.** The Memory Agent is the foundation; every other agent sits on top of it.
2. **Protocol before agents.** AgentSpec v1.0 is frozen before agent #1 is written.
3. **Depth over breadth.** 20 capable agents beat 90 shallow wrappers.
4. **Pluggability.** Adding agent #91 should take ~2 hours, not 2 weeks.
5. **Three-tier separation.** Orchestration, domain workers, and meta (self-improving) agents are distinct layers.

## 5. Users & Use Cases
- **Primary user:** the builder/researcher (you), running autonomous knowledge work and discovery.
- **Future users:** community contributors adding plugin agents; labs/companies running knowledge-work pipelines.

**Representative task:**
> "Research transformer architectures and build me a working implementation."

Flows through Supervisor → Planner → (Research ∥ Memory) → Coding → Evaluation → (Debug retry | Reflection). Target: ~4–8 minutes autonomous, end-to-end.

## 6. Success Metrics
- **M1.** Memory persists correctly across sessions (store/retrieve/forget verified).
- **M2.** Planner decomposes a complex goal into a correct task DAG ≥ 90% of trial goals.
- **M3.** Coding Agent self-repair loop fixes injected regressions on open-source repos at a measurable rate.
- **M4.** End-to-end "research → implement" task completes autonomously with passing evaluation.
- **M5.** ARK OS at month 6 outperforms ARK OS at month 1 on the same benchmark set (continual-improvement evidence).
- **M6.** ≥ 1 architecture paper submitted; pipeline for 4–5 total.

## 7. Publications (Target Outputs)
| # | Paper | Venue |
|---|---|---|
| 1 | ARK OS Architecture — kernel, AgentSpec, four-memory design | NeurIPS / ICLR |
| 2 | Agent Coordination Under Conflict | AAMAS |
| 3 | Hypothesis Generation & Ranking (SAGE) | Nature Machine Intelligence |
| 4 | ARKBench — Scientific Discovery / Multi-Domain Benchmark | ACL / NeurIPS Datasets |
| 5 | Domain Case Study (Climate or Materials) | Domain-specific top journal |

---

# Part II — Design Document

## 8. System Architecture (Three Tiers)

```
ARK OS
│
├── CORE KERNEL (build first, non-negotiable)
│   ├── Supervisor Agent   — routes, prioritizes, arbitrates, final authority
│   ├── Planner Agent      — breaks goals into task DAGs
│   ├── Memory Agent       — unified R/W across all 4 memory types
│   ├── Evaluation Agent   — scores outputs, triggers retry/escalation
│   ├── Reflection Agent   — post-task learning, updates strategy
│   └── Agent Registry     — knows what agents exist + their capabilities
│
├── PHASE 1 WORKERS (MVP)         Research, Coding, Web Search, File System
├── PHASE 2 WORKERS (Core)        Browser, Terminal, GitHub, Document, Security
├── PHASE 3 WORKERS (Advanced)    Vision, ML Engineer, DevOps, Data Analysis,
│                                 Self-Improvement, Capability Discovery
│
└── PLUGIN LAYER  — any agent following AgentSpec v1.0 drops in here
```

## 9. The 20 Agents (Locked)

### Phase 1 — Kernel (Month 1–2)
| # | Agent | Single Responsibility |
|---|---|---|
| 1 | Supervisor | Routes tasks, arbitrates conflicts, final authority |
| 2 | Planner | Breaks any goal into a DAG of subtasks |
| 3 | Memory | R/W across all 4 memory types for every agent |
| 4 | Research | arXiv, Semantic Scholar, RAG, hypothesis generation |
| 5 | Coding | Write, test, debug, evolve code autonomously |
| 6 | Evaluation | Scores every output, triggers retry/escalation |

### Phase 2 — Muscle (Month 3–4)
| # | Agent | Single Responsibility |
|---|---|---|
| 7 | Web Search | Real-time web grounding via search APIs |
| 8 | Browser | Full browser control via Playwright |
| 9 | Terminal | Bash execution, process management |
| 10 | File System | Read, write, organize, watch local files |
| 11 | GitHub | PRs, commits, issues, repo analysis |
| 12 | Document | PDF, DOCX, XLSX — parse and generate |
| 13 | Security | CVE scanning, threat detection, red-team sims |
| 14 | Reflection | Post-task learning, updates agent strategies |

### Phase 3 — Intelligence (Month 5–6)
| # | Agent | Single Responsibility |
|---|---|---|
| 15 | Vision | Screenshot analysis, image understanding |
| 16 | ML Engineer | Experiment tracking, HPO, training runs |
| 17 | DevOps | Docker, CI/CD, deployment pipelines |
| 18 | Data Analysis | Pandas, visualization, report generation |
| 19 | Self-Improvement | Makes other agents better over time |
| 20 | Capability Discovery | Knows what ARK can and cannot do |

> Design note: narrow utilities (Clipboard, OCR, Backup, Reminder, Journal, Expense, single-app messengers) are **tools or sub-functions**, not agents. They collapse into broader agents (Computer Use, File System, Calendar, Finance, Communication), reducing ~15 proposed agents into ~7 more-capable ones.

## 10. Inter-Agent Communication — AgentSpec v1.0

**This is the most critical design decision. Freeze it before writing agent #1.**

```python
class ARKMessage:
    task_id: str           # unique task identifier
    sender: str            # which agent sent this
    receiver: str          # which agent receives this
    intent: str            # RESEARCH | CODE | SEARCH | EXECUTE | STORE | ...
    payload: dict          # the actual content
    memory_context: list   # relevant memories injected by Memory Agent
    priority: int          # 1-10
    parent_task_id: str    # for subtask chains
    status: str            # PENDING | RUNNING | DONE | FAILED | RETRY
    retry_count: int       # current attempts
    max_retries: int       # cap before escalation
```

Wire example:
```json
{
  "task_id": "ark_20260615_001",
  "sender": "planner_agent",
  "receiver": "coding_agent",
  "intent": "CODE_GENERATION",
  "payload": {
    "instruction": "Write a FastAPI endpoint for ...",
    "context_files": ["main.py", "models.py"]
  },
  "memory_context": [],
  "priority": 8,
  "parent_task_id": "ark_20260615_000",
  "status": "PENDING",
  "retry_count": 0,
  "max_retries": 3
}
```

**Every agent reads this format. Every agent writes this format. No exceptions.**

## 11. Memory Architecture (Four Types)

The Memory Agent is the single most important agent in the system.

| Type | Holds | Backing Store |
|---|---|---|
| Working | Current task context | Python dict + Redis |
| Episodic | What happened in past runs | SQLite + timestamps |
| Semantic | Facts, knowledge, papers | Qdrant vector DB |
| Procedural | How to do things (skills) | JSON skill library |

**Memory Agent API (minimum):**
- `store(memory_type, key, value, metadata)`
- `retrieve(query, memory_type, top_k)` — semantic search for semantic/episodic
- `forget(memory_type, key | policy)` — TTL / relevance-based eviction
- `inject_context(task)` — populate `memory_context` on outgoing messages

**Acceptance:** memory must persist and be retrievable across process restarts.

## 12. Model Strategy

| Layer | Model | Role | Where |
|---|---|---|---|
| Orchestration | Qwen2.5-72B (API) | Supervisor, Planner, Reflection (strongest reasoning) | Groq API (free tier) |
| Local fast | Qwen2.5-7B | Memory, Evaluation, Search (high-frequency, low-latency) | Ollama, local |
| Code | Qwen2.5-Coder-14B | Coding, debugging, review | Ollama, local |
| Vision | LLaVA-1.6 / Qwen2-VL-7B | Vision, screenshots, OCR | local |
| Voice | Whisper-large-v3 | STT pipeline | local |

- 72B via API for the "brain"; Qwen2.5-14B local is the offline fallback.
- Security-sensitive work never leaves the machine.

## 13. Infrastructure / Tech Stack

```
Orchestration   LangGraph (agent state machines + DAG), LangChain (tool abstractions)
Memory          Qdrant (semantic), Redis (working + task queue), SQLite (episodic + logs),
                Neo4j (optional knowledge graph for Research Agent)
Model serving   Ollama (local), Groq API (72B orchestration)
Tools           Playwright (browser), Paramiko (terminal/SSH), GitPython (GitHub)
Interface       FastAPI (REST), WebSocket (real-time monitoring dashboard)
```

### Hardware fit
| Component | Spec | Verdict |
|---|---|---|
| RTX 5060 16GB | Qwen2.5-7B ≈ 5GB, Coder-14B ≈ 9GB | Both fit |
| 32GB RAM | Redis + Qdrant + 3 Ollama models | Comfortable |
| Ryzen 9 | LangGraph + FastAPI orchestration | Overkill (good) |
| Ubuntu Linux | Native Ollama, Docker, all tools | Ideal |

Phase 1+2 stack runs **fully local**; only the 72B orchestrator optionally calls out.

## 14. Reference Task Flow

```
User: "Research transformer architectures and build a working implementation"
   → Supervisor (intent + priority)
   → Planner (DAG: research → summarize → implement → test → evaluate)
   → Research ∥ Memory (parallel: read papers / load prior context)
   → Coding (implements)
   → Evaluation (scores)
        ├─ fail → Debugging retry (bounded by max_retries)
        └─ pass → Reflection (log what worked, update strategy)
```

## 15. SAGE Mode — Scientific Discovery Loop

SAGE reuses existing agents with a discovery-oriented orchestration goal. Only **3 new components** are added:

1. **Hypothesis Engine** (extends Planner) — reads 50+ papers, finds contradictions/gaps, ranks hypotheses by novelty × feasibility.
2. **Experiment Designer** (extends ML Engineer) — turns a hypothesis into a computational experiment (dataset, method, metrics, baseline), emits runnable code for the Coding Agent.
3. **Scientific Report Generator** (extends Document Agent) — assembles hypothesis + experiment + results into a LaTeX paper draft with auto-formatted citations.

```
Literature (Research+RAG) → Hypothesis (Planner+Reflection) → Experiment (ML Eng+Terminal)
        ↑                                                              │
   Knowledge Base (Memory) ← Report Gen (Document) ← Analysis (Data Analysis)
```

**Target domain (pick one):** Materials Science or Climate (fully open APIs, computational experiments, large gaps). Agriculture is the high-impact, India-relevant sleeper pick (FAO/ICAR/NFHS).

### SAGE Risk Register
| Risk | Mitigation |
|---|---|
| Trivial hypotheses | Novelty scoring vs. literature using citation graph |
| Experiments too long | Restrict to computational experiments only |
| Irreproducible results | Every experiment logged, versioned, dockerized |
| Scope too broad | One domain for the paper; architecture generalizes |
| 6 months too short | SAGE is a Phase 3 add-on; core ARK OS ships first |

---

# Part III — Implementation Phases

## 16. Phase Overview
```
Phase 1 MVP        6 agents    Month 1-2   The Kernel
Phase 2 Core      +8 agents    Month 3-4   The Muscle
Phase 3 Advanced  +6 agents    Month 5-6   The Intelligence + SAGE
────────────────────────────────────────────────────────
Total                20 agents               Plugin-ready for 70+
```

## 17. Month 1 — Foundation & Kernel (Week-by-Week)

**Week 1 — Foundation**
- Install Ollama; pull Qwen2.5-7B + Qwen2.5-Coder-14B
- Stand up Qdrant + Redis via Docker
- Freeze the **ARKMessage** schema (AgentSpec v1.0)
- Build the `BaseAgent` class all agents inherit from

**Week 2 — Memory Agent (first, always)**
- Implement 4-tier memory
- store / retrieve / forget API + `inject_context`
- Acceptance: memory persists across sessions

**Week 3 — Supervisor + Planner**
- LangGraph state machine for Supervisor
- Task DAG generation in Planner
- Acceptance: a complex goal decomposes correctly

**Week 4 — Research + Coding Agents**
- arXiv API + Qdrant RAG for Research
- Code generation + test loop for Coding
- Integration test: "Research X, implement it"

**Month 1 publishable output:** initial architecture write-up + memory-persistence results.

## 18. Month 2 — Complete MVP Loop
- Evaluation Agent: scoring rubric, retry/escalation triggers
- Close the Supervisor→Planner→workers→Evaluation loop
- Web Search + File System workers online
- Benchmark on research-synthesis quality
- **Output:** research-synthesis benchmark.

## 19. Month 3 — Muscle, Part 1
- Browser Agent (Playwright), Terminal Agent (bash/process mgmt)
- GitHub Agent (PRs, issues, repo analysis)
- Coding Agent self-repair loop hardened
- **Output:** self-repair evaluation on open-source repos.

## 20. Month 4 — Muscle, Part 2
- Document Agent (PDF/DOCX/XLSX parse + generate)
- Security Agent (CVE feeds, vuln scan on own systems, red-team sims)
- Reflection Agent (post-task learning updates strategies)
- **Output:** autonomous security audit methodology.

## 21. Month 5 — Intelligence + SAGE Components
- Vision, ML Engineer, DevOps, Data Analysis agents
- Add SAGE: Hypothesis Engine, Experiment Designer, Scientific Report Generator
- Wire Research → Discovery loop (hypothesis → experiment → result → knowledge base)
- **Output:** end-to-end discovery case study.

## 22. Month 6 — Integration, Evaluation, Paper
- Self-Improvement + Capability Discovery agents
- Full ARK OS + SAGE integration testing
- ARKBench: per-agent + end-to-end benchmarks
- WebSocket dashboard to observe live agent activity
- Month-6-vs-month-1 improvement study
- **Output:** architecture paper + benchmark + case study submissions.

## 23. The Single Most Important Sequencing Rule
> **Build the Memory Agent first — before Supervisor, before anything.** Every agent depends on memory. Build Supervisor first and you'll rewrite it when memory changes.

## 24. Immediate Next Step (This Week)
Write **AgentSpec v1.0** — the message format, memory interface, and registration protocol every agent must follow. Then scaffold:
- Repository structure
- `BaseAgent` class
- `ARKMessage` schema
- Memory Agent implementation (Week 1–2 deliverable)

## 25. Definition of Done (per agent)
1. Implements `BaseAgent`, speaks AgentSpec v1.0 exactly.
2. Registers itself + capabilities with the Agent Registry.
3. Reads/writes memory via the Memory Agent only.
4. Has unit tests + at least one integration test in the task loop.
5. Emits structured logs to episodic memory.
6. Passes its slice of ARKBench.

---

*End of document.*
