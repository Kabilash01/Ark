# ARK OS — Pre-Build Clarification Questions

**Purpose:** Answers to these questions are needed before or during build.
**Priority order:** Critical blockers first, then design gaps by phase.

---

## SECTION 1 — CRITICAL BLOCKERS (Answer before Week 1 starts)

These four answers change actual architecture decisions. Nothing can be finalized without them.

---

### Q1. Which GPU do you have — RTX 5060 or RTX 5060 Ti?

**Why this matters:**
The RTX 5060 base has **8GB VRAM**. The RTX 5060 Ti has **16GB VRAM**.
Qwen2.5-Coder-14B needs ~9GB — it will not load on an 8GB card.

| If you have | Coding Agent local model | Impact |
|---|---|---|
| RTX 5060 Ti (16GB) | Qwen2.5-Coder-14B locally | Full plan as written in v1.1 |
| RTX 5060 (8GB) | Must use Qwen2.5-Coder-7B locally | Groq needed for 14B-level code tasks |

**Your answer:**
- [ ] RTX 5060 Ti — 16GB VRAM
- [ ] RTX 5060 (base) — 8GB VRAM
- [ ] Other: _______________

---

### Q2. What is your operating system?

**Why this matters:**
All docs assume Ubuntu Linux. Ollama, Docker, Qdrant, and the Python toolchain all have
Linux-native install paths. Windows requires WSL2 or different setup entirely.

**Your answer:**
- [ ] Ubuntu Linux (native)
- [ ] Windows 11 with WSL2
- [ ] Windows 11 (no WSL2)
- [ ] Other Linux distro: _______________

---

### Q3. Do you have a Groq API key, and what is your free-tier status?

**Why this matters:**
Supervisor, Planner, and Reflection agents are designed to use Qwen2.5-72B via Groq.
If Groq is unavailable, these agents need a local fallback (Qwen2.5-14B) from Day 1,
which changes the memory budget and model-swap strategy.

**Your answer:**
- [ ] Yes — I have a Groq API key with free tier available
- [ ] Yes — I have a key but the free tier is exhausted (paid plan)
- [ ] No — I will use local fallback (Qwen2.5-14B) instead
- [ ] Not yet — I will create one this week

---

### Q4. What Python version will you use?

**Why this matters:**
Key libraries have version-specific constraints:
- `unsloth` works best on Python 3.11
- `bitsandbytes` (QLoRA) has issues on Python 3.13
- `langgraph` targets 3.11+

**Your answer:**
- [ ] Python 3.11 (recommended)
- [ ] Python 3.12
- [ ] Python 3.13
- [ ] I will follow whatever you recommend

---

## SECTION 2 — DESIGN GAPS (Answer before the relevant phase begins)

These do not block Week 1 but must be decided before the component is built.

---

### Q5. What methods should BaseAgent expose beyond `run()`?

Every agent inherits `BaseAgent`. The minimum interface needs to be frozen in Week 1.
The `run()` method signature is: `run(message: ARKMessage) -> ARKMessage`.

Beyond `run()`, should BaseAgent also enforce:

**Your answer (check all that apply):**
- [ ] `register(registry) -> None` — agent registers itself on startup
- [ ] `health_check() -> dict` — returns agent status for the registry
- [ ] `on_error(message, exception) -> ARKMessage` — standard error handler
- [ ] `validate_message(message: ARKMessage) -> bool` — schema validation before run
- [ ] `get_capabilities() -> list[str]` — returns what this agent can handle
- [ ] Keep it minimal — only `run()` is required, rest is optional per agent

---

### Q6. What fields should the Agent Registry store per agent?

The Agent Registry is a kernel service tracking all live agents. What information does
it need to store for the Supervisor to make good routing decisions?

**Proposed fields (confirm or modify):**
```
name          str    — unique agent identifier (e.g. "coding_agent")
capabilities  list   — what intent types it handles (e.g. ["CODE_GENERATION", "DEBUG"])
model         str    — which LLM it uses (e.g. "qwen2.5-coder-14b")
status        str    — IDLE | BUSY | ERROR | OFFLINE
current_task  str    — task_id it is currently processing (empty if IDLE)
last_heartbeat str   — ISO timestamp of last health check ping
```

**Your answer:**
- [ ] These fields are correct — use them
- [ ] Add this field: _______________
- [ ] Remove this field: _______________
- [ ] I am not sure — you decide

---

### Q7. How should the Evaluation Agent score different output types?

The quality score (0.0–1.0) needs a rubric per output type. Without a rubric,
scores will be arbitrary and unreproducible.

**Proposed rubrics (confirm or modify):**

| Output Type | Scoring Criteria |
|---|---|
| Research summary | Citation accuracy (40%) + Coverage (30%) + Clarity (30%) |
| Generated code | Tests pass (50%) + No syntax errors (20%) + Style (15%) + Security (15%) |
| Security audit | CVE recall (50%) + False positive rate (30%) + Report completeness (20%) |
| Hypothesis | Novelty vs literature (50%) + Feasibility (30%) + Clarity (20%) |

**Your answer:**
- [ ] These rubrics are correct — use them
- [ ] Modify this row: _______________
- [ ] I am not sure — you decide

---

### Q8. Which embedding model should be used for Qdrant (semantic memory)?

Two models are mentioned in different documents. Need to pick one and stick with it.

| Model | Dims | Speed | Quality | Size |
|---|---|---|---|---|
| `all-MiniLM-L6-v2` | 384 | Very fast | Good | 80MB |
| `BAAI/bge-large-en-v1.5` | 1024 | Medium | Better | 670MB |

**Your answer:**
- [ ] `all-MiniLM-L6-v2` — speed matters more at this stage
- [ ] `BAAI/bge-large-en-v1.5` — quality matters more for research RAG
- [ ] I am not sure — you decide

---

### Q9. Which scientific domain will SAGE target for the flagship paper?

The docs list three options but never pick one. This decision affects which
datasets, APIs, and domain-specific tools the Experiment Designer will use.

| Domain | Open Datasets | Experiment Type | Publication Target |
|---|---|---|---|
| Materials Science | Materials Project API, AFLOW | DFT property prediction, synthesis routes | Nature Materials / npj Comp. Mat. |
| Climate | NASA GDDP, NOAA ERA5 | Temperature/precipitation modeling | Nature Climate Change |
| Agriculture | FAO, ICAR, NFHS | Yield prediction, soil analysis | Indian + international journals |

**Your answer:**
- [ ] Materials Science
- [ ] Climate
- [ ] Agriculture (India-relevant, strong for SRIHER submissions)
- [ ] Decide later — SAGE is Phase 3, not urgent now

---

### Q10. How should the Redis message bus be structured?

The docs say Redis is used for "working memory + message bus" but never specify
the topology. Two options:

**Option A — Channel-per-agent (Pub/Sub)**
```
Each agent subscribes to its own channel: "agent:coding_agent"
Supervisor publishes to "agent:coding_agent" to send a task
Agent publishes result to "agent:supervisor" when done
```

**Option B — Shared task queue (Redis Lists)**
```
One queue per intent type: "queue:CODE_GENERATION", "queue:RESEARCH"
Agents pop from their queue with BLPOP (blocking pop)
Results pushed to "results:{task_id}"
Supervisor polls results
```

**Your answer:**
- [ ] Option A — Pub/Sub (lower latency, simpler routing)
- [ ] Option B — Task queues (more durable, easier to monitor load)
- [ ] I am not sure — you decide

---

### Q11. What is the schema for a Procedural Memory "skill" record?

Procedural memory stores how-to skills retrieved by intent. The schema needs
to be defined before the Memory Agent's procedural tier is implemented.

**Proposed schema:**
```python
class Skill(TypedDict):
    skill_id:    str    # unique ID
    intent:      str    # what task this skill handles
    description: str    # one-line summary
    steps:       list   # ordered list of steps or code snippets
    success_rate: float # % of times this skill led to score >= threshold
    use_count:   int    # how many times retrieved
    last_used:   str    # ISO timestamp
    source:      str    # "reflection_agent" | "manual" | "self_improvement"
```

**Your answer:**
- [ ] This schema is correct — use it
- [ ] Add this field: _______________
- [ ] Remove this field: _______________
- [ ] I am not sure — you decide

---

### Q12. How will the Self-Improvement Agent modify agent behavior?

The Self-Improvement Agent is described as running "A/B tests on agent prompts"
but the mechanism is unspecified. Three options:

**Option A — System prompt versioning**
Agent prompts are stored as versioned files. Self-Improvement proposes edits.
Evaluation Agent runs both versions on benchmark tasks. Better version replaces the old one.

**Option B — LoRA adapter swaps**
Self-Improvement trains new LoRA adapters on recent task data.
Better-performing adapter replaces the current one for that agent.

**Option C — Retrieval strategy tuning**
Self-Improvement adjusts memory retrieval parameters (top_k, similarity threshold)
and strategy weights, not the model or prompt itself.

**Your answer:**
- [ ] Option A — Prompt versioning (fastest to implement, Month 6 scope)
- [ ] Option B — LoRA adapter swaps (most powerful, needs GPU time)
- [ ] Option C — Retrieval tuning (safest, no model changes)
- [ ] Combination: _______________
- [ ] I am not sure — you decide

---

### Q13. What counts as an inter-agent "conflict" for Paper #2?

Paper #2 is titled "Inter-Agent Conflict Resolution in Autonomous Multi-Agent Systems"
but conflict is never defined in the design docs. Need a definition before building
the Supervisor's arbitration logic.

**Proposed conflict types:**
```
Type 1 — Resource conflict
  Two agents request the same tool simultaneously
  (e.g. Browser Agent + Web Search Agent both try to open a browser)

Type 2 — Output conflict
  Two agents produce contradictory outputs for the same task
  (e.g. Research Agent says "X is true", Coding Agent's test shows "X is false")

Type 3 — Priority conflict
  Two tasks arrive simultaneously with equal priority;
  Supervisor must pick one to run first

Type 4 — Memory conflict
  Agent A writes "skill S = approach 1" while Agent B writes "skill S = approach 2"
  to the same procedural memory key
```

**Your answer:**
- [ ] All four types — build arbitration for each
- [ ] Only Type 1 + Type 3 for now (most common, easiest to demonstrate)
- [ ] Modify the type list: _______________
- [ ] I am not sure — you decide

---

## SECTION 3 — OPTIONAL / NICE-TO-HAVE (No urgency, but good to decide early)

---

### Q14. Do you want a voice (STT) interface in Phase 3?

The v1.1 doc lists Whisper-large-v3 as optional. Adds ~1.5GB VRAM when loaded.

- [ ] Yes — include Whisper STT pipeline in Phase 3
- [ ] No — skip voice, focus on text and API interface only

---

### Q15. Do you want Neo4j (knowledge graph) or skip it?

Neo4j is listed as "optional" for the Research Agent. Qdrant already handles
semantic search. Neo4j adds explicit relationship traversal (citation graphs,
entity-relationship maps) but adds another Docker service to maintain.

- [ ] Yes — include Neo4j for citation graph and knowledge graph queries
- [ ] No — Qdrant is sufficient; keep the stack simpler

---

### Q16. Who is the second author (Pavitra P)?

v1.1 lists Pavitra P (E0323038) as co-author. What is their role in the build?

- [ ] Co-builder — working on specific agents or components
- [ ] Co-author for papers only — not building code
- [ ] Not involved at this stage
- [ ] Clarify: _______________

---

## Answer Sheet Summary

Fill this in after going through each question:

```
Q1  GPU:             _______________
Q2  OS:              _______________
Q3  Groq:            _______________
Q4  Python:          _______________
Q5  BaseAgent:       _______________
Q6  Registry fields: _______________
Q7  Eval rubric:     _______________
Q8  Embedding model: _______________
Q9  SAGE domain:     _______________
Q10 Redis topology:  _______________
Q11 Skill schema:    _______________
Q12 Self-Improve:    _______________
Q13 Conflict types:  _______________
Q14 Voice:           _______________
Q15 Neo4j:           _______________
Q16 Pavitra:         _______________
```

---

*Generated by Claude Code after reading all ARK OS Implementation Guide documents.*
*Answer Q1–Q4 first — they block the Week 1 scaffold.*
