# ARK OS — Pre-Build Clarification Answers
**Status:** FINALIZED — all 16 questions answered
**Version:** v1.0
**Date:** June 2026

---

## CRITICAL IMPACT SUMMARY (Read This First)

Three answers below change the architecture significantly from v1.1:

| Change | Impact |
|---|---|
| RTX 5060 8GB (not 16GB) | Qwen2.5-Coder-14B cannot load locally. Replaced with Qwen2.5-Coder-7B. Groq handles 14B-level code tasks when needed. |
| Both OS (Ubuntu + Windows) | Docker Compose must work on both. Dev primarily on Ubuntu native; Windows uses WSL2. Path handling must be OS-agnostic. |
| SAGE = all domains | SAGE pipeline stays domain-agnostic by design. Materials Science is the flagship paper domain. Others are case studies. |

---

## SECTION 1 — CRITICAL BLOCKERS

---

### Q1. GPU — RTX 5060 (base) — 8GB VRAM

**Decision and full impact:**

```
VRAM Budget (8GB hard ceiling):
─────────────────────────────────────────────────
Model                        Size (Q4_K_M)    Fits?
─────────────────────────────────────────────────
Gemma 4 26B-A4B QAT          ~4.5 GB          ✅ YES
Qwen2.5-Coder-7B             ~4.5 GB          ✅ YES
Qwen2-VL-7B                  ~4.8 GB          ✅ YES
Qwen2.5-Coder-14B            ~9.0 GB          ❌ NO — exceeds VRAM

Key rule: only ONE model loaded at a time. Ollama hot-swaps on demand.
─────────────────────────────────────────────────
```

**Updated model strategy for 8GB:**

| Agent Group | Model | Where |
|---|---|---|
| Supervisor, Planner, Reflection, Self-Improvement | Qwen2.5-72B | Groq API |
| General workers (Memory, Eval, Research, Web, File, Security) | Gemma 4 26B-A4B QAT | Ollama local |
| Code workers (Coding, Debugging, GitHub, DevOps) | Qwen2.5-Coder-7B | Ollama local |
| Vision (Vision Agent, OCR, Screenshots) | Qwen2-VL-7B | Ollama local |
| Orchestration fallback (if Groq down) | Qwen2.5-14B via Groq | Groq API |
| Claude API (Supervisor/Planner premium) | claude-sonnet-4-6 | Anthropic API |

**Model swap strategy:**
```python
# Ollama loads/unloads automatically based on request
# Never load code + vision simultaneously — they compete for 8GB
# Model priority order when VRAM pressure detected:
#   1. Unload vision model (least frequent)
#   2. Load code model (on CODE_GENERATION intent)
#   3. Swap back to general after code task completes

# In ARKModelRegistry:
VRAM_SWAP_POLICY = {
    "general":  ["gemma4:26b-instruct-qat"],          # default resident
    "code":     ["qwen2.5-coder:7b"],                 # loaded on demand
    "vision":   ["qwen2-vl:7b"],                      # loaded on demand
    "orchestration": "groq:qwen2.5-72b",              # always API
}
```

**What you lose vs 16GB plan:**
- Qwen2.5-Coder-14B → replaced by Coder-7B (still strong, not as deep)
- Cannot run code + vision simultaneously locally (must swap)
- Groq dependency for large code tasks is higher

**What you gain:**
- Gemma 4 26B-A4B still fully works (MoE, only 4.5GB active)
- All 20 agents still functional — just with hot-swap model management
- Forces cleaner agent design (one active model = no VRAM conflicts)

---

### Q2. Operating System — Both Ubuntu (native) + Windows 11 WSL2

**Decision:**
- **Primary development environment: Ubuntu Linux (native)**
- **Secondary: Windows 11 with WSL2** — all Docker commands work identically in WSL2

**What this means for the build:**

```
Rule 1: All file paths use forward slashes — never Windows backslashes
        ✅ "/home/user/ark-os/"  ❌ "C:\Users\user\ark-os\"

Rule 2: Docker Compose works identically on Ubuntu native and WSL2
        No platform-specific flags needed

Rule 3: Ollama install paths differ — handle in setup script:
        Ubuntu:  /usr/local/bin/ollama
        WSL2:    same as Ubuntu (Ollama supports WSL2 natively)

Rule 4: Python virtual environment created on Ubuntu
        If switching to Windows, recreate venv in WSL2 — don't share

Rule 5: GPU passthrough in WSL2
        WSL2 supports NVIDIA GPU via CUDA on Windows 11
        Requires: NVIDIA driver 535+ on Windows host
        Ollama in WSL2 will see RTX 5060 correctly
```

**Setup script will auto-detect OS:**
```python
import platform
import subprocess

def detect_environment():
    if platform.system() == "Linux":
        # Check if running in WSL2
        with open("/proc/version") as f:
            if "microsoft" in f.read().lower():
                return "wsl2"
        return "ubuntu_native"
    elif platform.system() == "Windows":
        return "windows_native"  # not supported — use WSL2
    return "unknown"
```

---

### Q3. API Keys — Groq ✅ + Claude API ✅ + All available

**Confirmed API stack:**

```
ANTHROPIC_API_KEY  →  claude-sonnet-4-6 (Supervisor premium calls)
                       claude-haiku-4-5  (Evaluation Agent scoring)

GROQ_API_KEY       →  qwen2.5-72b-versatile (main orchestration)
                       llama-3.3-70b-versatile (fallback if Qwen quota hit)

LOCAL (Ollama)     →  gemma4, qwen2.5-coder-7b, qwen2-vl-7b (no key needed)
```

**Priority routing logic for Supervisor:**
```
1. Try claude-sonnet-4-6 (best instruction following, JSON reliability)
2. If Claude quota/rate limit → fall to Groq Qwen2.5-72B
3. If Groq down → fall to Qwen2.5-14B via Together AI or local 14B
4. Never fall below 14B for Supervisor/Planner (too critical)
```

**Cost ceiling recommendation:**
Set a monthly hard cap in both Anthropic and Groq dashboards.
Estimated normal usage: < $5/month Anthropic + $0 Groq (free tier).

---

### Q4. Python Version — 3.11 (Recommended and Decided)

**Reason:**

```
Python 3.11 is the decision. Here is why:

Library              3.11    3.12    3.13
─────────────────────────────────────────
unsloth (QLoRA)      ✅      ⚠️      ❌
bitsandbytes         ✅      ✅      ❌
langgraph            ✅      ✅      ✅
langchain            ✅      ✅      ✅
qdrant-client        ✅      ✅      ✅
ollama               ✅      ✅      ✅
playwright           ✅      ✅      ⚠️
mlflow               ✅      ✅      ⚠️
optuna               ✅      ✅      ✅

3.11 = everything works. 3.13 = bitsandbytes breaks (kills QLoRA).
```

**Environment setup:**
```bash
# Ubuntu / WSL2
sudo apt install python3.11 python3.11-venv python3.11-dev

# Create ARK OS virtual environment
python3.11 -m venv ark-env
source ark-env/bin/activate

# Verify
python --version  # Must show Python 3.11.x
```

**Pin in pyproject.toml:**
```toml
[tool.poetry.dependencies]
python = ">=3.11,<3.12"
```

---

## SECTION 2 — DESIGN GAPS

---

### Q5. BaseAgent Interface — Full 5-Method Interface (Recommended and Decided)

**Decision: Enforce all 5 methods. Reasoning below.**

```python
from abc import ABC, abstractmethod
from typing import Optional
from ark_os.core.ark_message import ARKMessage

class BaseAgent(ABC):
    """
    Every ARK OS agent MUST inherit this class.
    All 5 methods are enforced — no optional skipping.
    This is the contract that makes the Agent Registry + Supervisor work.
    """

    # ── Required — must implement ──────────────────────────────────────

    @abstractmethod
    def run(self, message: ARKMessage) -> ARKMessage:
        """
        Execute the agent's core task.
        Input: ARKMessage with status=PENDING
        Output: ARKMessage with status=DONE|FAILED, output populated
        """
        pass

    @abstractmethod
    def get_capabilities(self) -> list[str]:
        """
        Return list of intent types this agent handles.
        Used by Agent Registry for Supervisor routing.
        Example: ["CODE_GENERATION", "CODE_REVIEW", "DEBUG"]
        """
        pass

    # ── Provided — default implementations, override if needed ─────────

    def register(self, registry) -> None:
        """
        Self-register with Agent Registry on startup.
        Default: auto-registers using agent_name + get_capabilities().
        """
        registry.register(
            name=self.agent_name,
            capabilities=self.get_capabilities(),
            model=self.model_name,
            status="IDLE"
        )

    def health_check(self) -> dict:
        """
        Return current agent status for registry heartbeat.
        Default: returns IDLE if not overridden.
        """
        return {
            "status": "IDLE",
            "current_task": None,
            "last_heartbeat": datetime.utcnow().isoformat()
        }

    def validate_message(self, message: ARKMessage) -> bool:
        """
        Validate incoming ARKMessage before run().
        Default: checks required fields exist and status=PENDING.
        """
        required = ["task_id", "sender", "receiver", "intent",
                    "payload", "status", "timestamp"]
        return (
            all(field in message for field in required)
            and message["status"] == "PENDING"
            and message["receiver"] == self.agent_name
        )

    def on_error(self, message: ARKMessage, exception: Exception) -> ARKMessage:
        """
        Standard error handler — wraps exception into failed ARKMessage.
        Supervisor checks status=FAILED and decides retry vs escalate.
        """
        message["status"] = "FAILED"
        message["output"] = {
            "error_type": type(exception).__name__,
            "error_message": str(exception),
            "agent": self.agent_name
        }
        message["quality_score"] = 0.0
        return message
```

**Why all 5:**
- `run()` + `get_capabilities()` — abstract, must implement (can't default these)
- `register()` — without this, Agent Registry can't route; default is safe
- `health_check()` — without this, Supervisor can't detect dead agents
- `validate_message()` — catches malformed ARKMessages before they corrupt agent state
- `on_error()` — without this, every agent writes different error formats; Supervisor can't parse

---

### Q6. Agent Registry Fields — Proposed Fields + 2 Additions (Decided)

**Decision: Use all proposed fields + add `load_count` and `avg_quality_score`.**

```python
class AgentRecord(TypedDict):
    # ── Proposed fields (all confirmed) ───────────────────────────────
    name:            str    # unique agent ID — e.g. "coding_agent"
    capabilities:    list   # intent types — e.g. ["CODE_GENERATION", "DEBUG"]
    model:           str    # LLM used — e.g. "qwen2.5-coder:7b"
    status:          str    # IDLE | BUSY | ERROR | OFFLINE
    current_task:    str    # task_id in progress; "" if IDLE
    last_heartbeat:  str    # ISO 8601 timestamp of last health ping

    # ── Added fields ───────────────────────────────────────────────────
    load_count:      int    # total tasks handled since startup
                            # → Supervisor uses for load balancing
                            #   (avoids always sending to same agent)

    avg_quality_score: float  # rolling average of Evaluation Agent scores
                               # → Supervisor deprioritizes agents with
                               #   consistently low scores (< 0.6)
```

**Why `load_count`:** If two agents handle the same capability (e.g., two Research Agent instances), Supervisor picks the one with lower load_count. Enables horizontal scaling later.

**Why `avg_quality_score`:** Self-Improvement Agent reads this. If an agent's rolling average drops below 0.6 over 20 tasks, it triggers a prompt review. This is the feedback loop that makes the system self-correcting.

---

### Q7. Evaluation Rubrics — Proposed Rubrics Confirmed + 1 Addition (Decided)

**Decision: Use all proposed rubrics. Add "Data Analysis" output type for SAGE Mode.**

| Output Type | Criteria | Weights |
|---|---|---|
| Research summary | Citation accuracy + Coverage + Clarity | 40% + 30% + 30% |
| Generated code | Tests pass + No syntax errors + Style + Security | 50% + 20% + 15% + 15% |
| Security audit | CVE recall + False positive rate + Report completeness | 50% + 30% + 20% |
| Hypothesis | Novelty vs literature + Feasibility + Clarity | 50% + 30% + 20% |
| **Data analysis** (added) | **Statistical validity + Visualization quality + Interpretability** | **50% + 25% + 25%** |

**Implementation approach:**
```python
class EvaluationRubric:
    """
    Evaluation Agent uses LLM-as-judge pattern.
    Each rubric criterion is a structured prompt sent to claude-haiku-4-5.
    Scores are averaged per weight to produce final quality_score (0.0–1.0).
    """
    RETRY_THRESHOLD = 0.65    # below this → send back for retry
    ACCEPT_THRESHOLD = 0.80   # above this → pass to next agent
    # Between 0.65–0.80: accept with warning logged to Reflection Agent
```

---

### Q8. Embedding Model — BAAI/bge-large-en-v1.5 (Decided)

**Decision: `BAAI/bge-large-en-v1.5` — quality over speed.**

**Reasoning:**
```
ARK OS Semantic Memory stores:
  - Research papers (long, dense academic text)
  - Code patterns (requires structural understanding)
  - Hypothesis records (nuanced scientific language)

all-MiniLM-L6-v2 (384-dim):
  → Good for short sentences, FAQ-style retrieval
  → Loses nuance in long academic paragraphs
  → Wrong choice for research RAG

BAAI/bge-large-en-v1.5 (1024-dim):
  → Specifically trained on academic and technical text
  → Best-in-class on BEIR benchmark (research retrieval)
  → 670MB — loads once at startup, sits in RAM (your 32GB handles this)
  → Retrieval quality directly affects Research Agent and SAGE Mode

Verdict: The 670MB cost is worth it. Research quality is the core claim.
```

**Qdrant collection config:**
```python
from qdrant_client.models import VectorParams, Distance

EMBEDDING_MODEL = "BAAI/bge-large-en-v1.5"
EMBEDDING_DIM   = 1024

client.create_collection(
    collection_name="ark_semantic_memory",
    vectors_config=VectorParams(
        size=EMBEDDING_DIM,
        distance=Distance.COSINE
    )
)
```

---

### Q9. SAGE Domain — All Domains, Materials Science as Flagship Paper (Decided)

**Decision: SAGE is domain-agnostic by architecture. Flagship paper = Materials Science.**

**Why Materials Science for the paper:**
```
Domain           Open API?   Computational only?   India relevance   Publication speed
──────────────────────────────────────────────────────────────────────────────────────
Materials Sci.   ✅ Yes       ✅ Yes (DFT calc.)    Medium            Fast (npj, nature)
Climate          ✅ Yes       ✅ Yes (simulation)   High              Medium
Agriculture      ⚠️ Partial   ⚠️ Partial            Very High         Medium (Indian j.)
──────────────────────────────────────────────────────────────────────────────────────

Materials Science wins because:
1. Materials Project API — 150K+ materials, fully open, no auth issues
2. DFT calculations are purely computational — no lab needed
3. Clear novelty metrics — band gap, formation energy, stability
4. npj Computational Materials is Q1 journal — high impact, fast review
5. Claude can be used as the reasoning layer; SAGE generates the hypothesis
```

**Domain integration plan:**
```
Phase 3 Month 5:  SAGE pipeline domain-agnostic (works for any domain)
Phase 3 Month 6:  Materials Science case study (flagship paper run)
Post-graduation:  Climate domain case study (Paper #5)
Post-graduation:  Agriculture domain case study (India journal target)
```

**Materials Science tools to add in Phase 3:**
```python
# tools/science/materials_tools.py
from mp_api.client import MPRester   # Materials Project API

def query_material_properties(formula: str) -> dict:
    """Fetch band gap, formation energy, stability for a material."""

def search_similar_materials(properties: dict) -> list:
    """Find materials with similar properties to a target."""

def run_hypothetical_substitution(base_material: str, element: str) -> dict:
    """Simulate element substitution and predict new properties."""
```

---

### Q10. Redis Message Bus — Option A: Pub/Sub Per Agent (Decided)

**Decision: Option A — channel-per-agent Pub/Sub.**

**Reasoning:**
```
Option B (task queues) is better for:
  → High-throughput production systems (1000s of agents)
  → When you need guaranteed delivery + persistence
  → Large teams where queue monitoring is needed

Option A (Pub/Sub) is better for ARK OS because:
  → 20 agents, not 1000 — latency matters more than throughput
  → Direct agent-to-agent communication matches ARKMessage design
  → Simpler to debug (each agent has a named channel)
  → Fits LangGraph's event-driven model naturally
  → Dashboard can subscribe to all channels for monitoring
```

**Channel topology:**
```
agent:supervisor          ← Supervisor receives all results here
agent:planner             ← Planner receives routing decisions
agent:memory_agent        ← Memory receives store/retrieve requests
agent:coding_agent        ← Coding receives CODE_GENERATION tasks
agent:research_agent      ← Research receives RESEARCH tasks
... (one channel per agent)

ark:broadcast             ← System-wide messages (shutdown, pause, etc.)
ark:heartbeat             ← All agents publish health pings here
ark:audit_log             ← All ARKMessages logged here for debugging
```

**Implementation:**
```python
import redis.asyncio as redis

class ARKMessageBus:
    def __init__(self):
        self.redis = redis.Redis(host="localhost", port=6379)

    async def publish(self, agent_name: str, message: ARKMessage):
        channel = f"agent:{agent_name}"
        await self.redis.publish(channel, json.dumps(message))

    async def subscribe(self, agent_name: str):
        pubsub = self.redis.pubsub()
        await pubsub.subscribe(f"agent:{agent_name}")
        async for message in pubsub.listen():
            if message["type"] == "message":
                yield json.loads(message["data"])
```

---

### Q11. Procedural Memory Skill Schema — Proposed Schema Confirmed (Decided)

**Decision: Use proposed schema exactly. No changes needed.**

```python
class Skill(TypedDict):
    skill_id:     str    # uuid4 — unique ID
    intent:       str    # intent type this handles — e.g. "CODE_GENERATION"
    description:  str    # one-line summary — used for FAISS semantic search
    steps:        list   # ordered steps or code snippets
    success_rate: float  # % of runs where quality_score >= ACCEPT_THRESHOLD
    use_count:    int    # retrieval frequency — higher = more trusted
    last_used:    str    # ISO 8601 — for TTL-based eviction
    source:       str    # "reflection_agent" | "manual" | "self_improvement"
```

**How skills are retrieved:**
```python
# FAISS index built on description embeddings
# When an agent needs a skill:
# 1. Encode incoming task description → embedding
# 2. FAISS nearest-neighbor search in skill descriptions
# 3. Return top-3 skills by similarity × success_rate
# 4. Agent uses top skill's steps as context in its prompt
```

---

### Q12. Self-Improvement Agent — Phased Approach A → C → B (Decided)

**Decision: All three options, implemented in sequence across phases.**

```
Month 6 (Phase 3):    Option A — Prompt versioning
                      Fastest to build. Immediate measurable improvement.
                      A/B test agent prompts on ARKBench tasks.
                      Better prompt replaces worse prompt. Logged and versioned.

Post-graduation:      Option C — Retrieval strategy tuning
                      Adjust top_k, similarity thresholds, skill weights.
                      No model changes — safe, auditable.
                      Adds 5-15% quality improvement on top of Option A.

Masters research:     Option B — LoRA adapter swaps
                      Most powerful. Requires GPU time + dataset curation.
                      Each agent gets its own LoRA adapter, retrained monthly.
                      This is a full research contribution on its own.
```

**Month 6 implementation (Option A):**
```python
class SelfImprovementAgent(BaseAgent):
    def run(self, message: ARKMessage) -> ARKMessage:
        # 1. Read last 50 Reflection logs for target agent
        reflection_logs = self.memory.retrieve(
            query=f"reflection logs {message.payload['target_agent']}",
            memory_type="episodic", top_k=50
        )

        # 2. Identify patterns in failures (quality_score < 0.65)
        failure_patterns = self.analyze_failures(reflection_logs)

        # 3. Generate improved system prompt using Claude Sonnet
        new_prompt = self.llm.invoke(
            f"Current prompt: {current_prompt}\n"
            f"Failure patterns: {failure_patterns}\n"
            f"Generate an improved system prompt that avoids these failures."
        )

        # 4. A/B test: run both prompts on 10 ARKBench tasks
        score_old = self.benchmark(current_prompt, tasks=10)
        score_new = self.benchmark(new_prompt, tasks=10)

        # 5. Keep better prompt, log result
        if score_new > score_old + 0.05:  # 5% improvement threshold
            self.update_agent_prompt(target_agent, new_prompt)
            # Log: which agent, what changed, score delta
```

---

### Q13. Conflict Types — All Four Types, Prioritized Build Order (Decided)

**Decision: All four conflict types, built in priority order.**

```
Build order (easiest → hardest):

Priority 1 — Type 3: Priority conflict          ← Build in Phase 1 (Week 5)
  Two equal-priority tasks arrive simultaneously
  Resolution: timestamp-based FIFO + domain round-robin
  Why first: happens immediately in any multi-agent run

Priority 2 — Type 1: Resource conflict          ← Build in Phase 2 (Month 3)
  Two agents compete for same tool (browser, terminal)
  Resolution: tool semaphore — first-come-first-served; second agent queued
  Why second: only relevant once Browser + Terminal agents exist

Priority 3 — Type 4: Memory conflict            ← Build in Phase 2 (Month 4)
  Two agents write conflicting skills to procedural memory
  Resolution: version-aware merge — keep both versions, tag with agent source
              Reflection Agent reconciles on next run
  Why third: only relevant once Reflection Agent exists

Priority 4 — Type 2: Output conflict            ← Build in Phase 3 (Month 5)
  Research Agent says X is true; Coding Agent's test proves X is false
  Resolution: Supervisor calls a "conflict session" —
              both agents re-run with each other's output as context
              If still conflicting → human escalation flag
  Why last: hardest to detect, needs Vision + Data Analysis agents to compare outputs

All four types = Paper #2 content (AAMAS 2027).
Each type is a subsection: definition, frequency analysis, resolution protocol, evaluation.
```

---

### Q14. Voice Interface — Deferred to Phase 3, Model TBD

**Decision: Include voice in Phase 3 scope. Model decided during Phase 2.**

```
Current candidates (decide in Month 4):

Model               Size    Speed       Quality    Local?
──────────────────────────────────────────────────────────
Whisper-large-v3    1.5GB   Moderate    Best       ✅ Yes
Whisper-medium      780MB   Fast        Good       ✅ Yes
Whisper-small       244MB   Very fast   Decent     ✅ Yes
faster-whisper      *       2-4x faster Same       ✅ Yes (optimized)

Recommendation (likely): faster-whisper + whisper-medium
  → 780MB — fits in RAM alongside Ollama
  → faster-whisper gives 2-4x realtime speed on CPU
  → Good quality for English technical vocabulary
  → Whisper-large-v3 only if quality is insufficient

VRAM impact: Whisper runs on CPU via faster-whisper
→ No VRAM cost → safe for 8GB RTX 5060

Decision point: Month 4, after Phase 2 agents are stable.
```

---

### Q15. Neo4j — YES, Include in Stack

**Decision: Include Neo4j. Restricted to Research Agent + SAGE Mode only.**

**Why it justifies the complexity:**
```
Qdrant (vector search) answers: "What is semantically similar to X?"
Neo4j (graph traversal) answers: "What cites X? What does X cite? 
                                   What connects A to B through the literature?"

For SAGE Mode specifically:
  → Citation graph: which papers are connected? how far apart?
  → Concept graph: which scientific concepts co-occur?
  → Gap detection: which concept pairs have no connecting paper?
    (THIS is where novel hypotheses come from)

Without Neo4j: SAGE generates hypotheses from text similarity alone
With Neo4j:    SAGE generates hypotheses from structural gaps in the knowledge graph
               → This is the publishable claim in Paper #3
```

**Docker service:**
```yaml
# docker-compose.yml addition
neo4j:
  image: neo4j:5.15-community
  ports:
    - "7474:7474"   # HTTP browser UI
    - "7687:7687"   # Bolt protocol (Python driver)
  environment:
    NEO4J_AUTH: neo4j/ark_os_password
  volumes:
    - neo4j_data:/data
```

**What goes in Neo4j vs Qdrant:**
```
Qdrant:  Paper embeddings, code embeddings, skill embeddings
         → "Find papers similar to this one"

Neo4j:   Paper nodes, citation edges, concept nodes, concept-paper edges
         → "Find the gap between concept A and concept B in literature"
         → "Which papers form a cluster around hypothesis X?"
```

---

### Q16. Pavitra P — Set Aside for Now

No decisions made. Will be addressed when relevant.

---

## FINAL ANSWER SHEET

```
Q1   GPU:               RTX 5060 base — 8GB VRAM
                        → Coder-14B replaced by Coder-7B locally
                        → Hot-swap model strategy enforced

Q2   OS:                Both Ubuntu (primary) + Windows WSL2 (secondary)
                        → Docker Compose works on both
                        → OS-agnostic path handling required

Q3   Groq:              Yes — Groq API + Anthropic API + Claude Code all available
                        → Priority: Claude Sonnet → Groq Qwen72B → local fallback

Q4   Python:            3.11 (decided — not negotiable)
                        → Reason: unsloth + bitsandbytes compatibility

Q5   BaseAgent:         All 5 methods enforced
                        → run() + get_capabilities() = abstract (must implement)
                        → register() + health_check() + validate_message() + on_error() = defaults provided

Q6   Registry fields:   All 6 proposed + load_count + avg_quality_score
                        → load_count: enables load balancing
                        → avg_quality_score: triggers self-improvement

Q7   Eval rubric:       All 4 proposed confirmed + Data Analysis added (5th)
                        → Retry threshold: 0.65 | Accept threshold: 0.80

Q8   Embedding model:   BAAI/bge-large-en-v1.5 (1024-dim)
                        → Quality over speed — research RAG demands it

Q9   SAGE domain:       All domains (domain-agnostic architecture)
                        → Flagship paper: Materials Science (Materials Project API)
                        → Case studies: Climate + Agriculture (post-grad)

Q10  Redis topology:    Option A — Pub/Sub per agent
                        → channel: "agent:{agent_name}" per agent
                        → ark:broadcast + ark:heartbeat + ark:audit_log system channels

Q11  Skill schema:      Proposed schema confirmed — no changes
                        → Retrieved by FAISS on description embedding × success_rate

Q12  Self-Improvement:  Phased — A (Month 6) → C (post-grad) → B (Masters)
                        → Month 6: prompt versioning with A/B testing on ARKBench

Q13  Conflict types:    All four — built in order: 3 → 1 → 4 → 2
                        → All four = Paper #2 (AAMAS 2027) content

Q14  Voice:             Included in Phase 3 scope — model TBD in Month 4
                        → Likely: faster-whisper + whisper-medium (CPU, no VRAM cost)

Q15  Neo4j:             YES — included
                        → Used by Research Agent + SAGE Mode only
                        → Enables citation graph traversal and knowledge gap detection
                        → Core claim of Paper #3 (hypothesis generation)

Q16  Pavitra:           Set aside
```

---

## UPDATED WEEK 1 CHECKLIST (Post-Answers)

```
□ Install Python 3.11 (not 3.12, not 3.13)
□ Create virtualenv: python3.11 -m venv ark-env
□ Pull Ollama models:
    ollama pull gemma4:26b-instruct-qat
    ollama pull qwen2.5-coder:7b
    ollama pull qwen2-vl:7b
□ Start Docker services:
    docker-compose up -d qdrant redis neo4j mlflow
□ Set environment variables:
    ANTHROPIC_API_KEY=...
    GROQ_API_KEY=...
□ Freeze ARKMessage schema (AgentSpec v1.0)
□ Write BaseAgent with all 5 methods
□ Start Memory Agent (before everything else)
```

---

*ARK OS Pre-Build Clarifications — FINALIZED*
*All 16 questions answered. Ready to scaffold.*
