# ARK OS — Day-by-Day Build Plan
**Version 2.0 — Updated with all recommendations**
**6 Months · 20 Agents · Every Single Day Planned**

---

## What's New in v2.0

| Addition | Where Added | Why |
|---|---|---|
| Hardware auto-detection | Day 1–2 | Anyone can install, any GPU |
| llama.cpp inference layer | Day 60 | 10–30% faster than Ollama alone |
| Agent versioning | Day 30 | Scientific rigor for self-improvement claim |
| Privacy mode flag | Day 33 | Enterprise/institutional users |
| Checkpoint system | Day 81 | SAGE loop survives power cuts |
| Task scheduling (cron) | Day 82 | True autonomous operation |
| ARK-VLM custom model | Days 86–90 | Paper #5, custom SigLIP + Qwen2.5 |
| MCP server | Day 109 | Distribution via Claude Code / Cursor |
| Obsidian/Notion export | Day 117 | Researchers actually use the output |
| 24/7 deployment guide | Day 118 | Raspberry Pi + cloud options |
| **STATE.md** | Day 16 | Human-readable live system snapshot |
| **--until done conditions** | Day 37 | Loop runs until objective test passes, not "feels done" |

---

## Rules (Read These First)

```
Mon–Sat:  Build days (active coding)
Sunday:   Review, document, plan next week only — no new features

Each day has ONE primary deliverable.
Never start Day N+1 if Day N is incomplete. Carry it forward.
Write tests alongside code — never after.
Commit to git every single day.
Commit format: "[Day NN] Component: what was built"
ARKMessage schema is FROZEN after Day 4. Never change it.
Memory Agent is sacred. Break it → fix it before touching anything else.
ARKBench scores recorded every month — non-negotiable.
```

---

# MONTH 1 — FOUNDATION
## Goal: Environment + Hardware Detection + Memory Agent + Kernel + MVP Loop

---

### WEEK 1 — Environment + Schema + BaseAgent

---

**Day 1 — Monday**
**Task: Environment Setup + Hardware Auto-Detection**

```
PART A — Install everything

  1. Install Python 3.11
       sudo apt install python3.11 python3.11-venv python3.11-dev

  2. Create virtualenv
       python3.11 -m venv ark-env
       source ark-env/bin/activate

  3. Install Ollama
       curl -fsSL https://ollama.com/install.sh | sh

  4. Pull all three local models (start early — takes time)
       ollama pull gemma4:26b-instruct-qat
       ollama pull qwen2.5-coder:7b
       ollama pull qwen2-vl:7b

  5. Install Docker + Docker Compose
       sudo apt install docker.io docker-compose-plugin

  6. Install llama-cpp-python (CUDA build for RTX 5060)
       pip install llama-cpp-python \
         --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cu121

  7. Set environment variables in ~/.bashrc
       export ANTHROPIC_API_KEY=your_key_here
       export GROQ_API_KEY=your_key_here

  8. Test everything works
       ollama run gemma4:26b-instruct-qat "say hello"
       python -c "from llama_cpp import Llama; print('llama.cpp OK')"
       docker run hello-world

PART B — Hardware Detection Script

  File: scripts/select-model.py

  What it does:
    Detects GPU VRAM + system RAM
    Assigns hardware tier (T1–T6)
    Writes config/active-models.json with selected models

  Tier table:
    T1  VRAM < 6GB    Qwen2.5-3B + Phi-3-mini (very light)
    T2  VRAM 6–10GB   Gemma4-26B-A4B + Coder-7B + VL-7B  ← yours
    T3  VRAM 14–20GB  Qwen2.5-14B + Coder-14B + VL-7B
    T4  VRAM 22GB+    Qwen2.5-32B + Coder-32B + VL-72B
    T5  CPU only      Phi-3-mini Q4 (slow but works)
    T6  Apple M-chip  MLX models (faster on unified memory)

  Code to write:

    import subprocess, json, platform, psutil

    def detect_gpu_vram() -> float:
        try:
            r = subprocess.run(
                ["nvidia-smi","--query-gpu=memory.total",
                 "--format=csv,noheader,nounits"],
                capture_output=True, text=True)
            if r.returncode == 0:
                return round(int(r.stdout.strip().split('\n')[0]) / 1024, 1)
        except FileNotFoundError:
            pass
        if platform.system()=="Darwin" and platform.machine()=="arm64":
            return round(psutil.virtual_memory().total / (1024**3), 1)
        return 0.0

    def assign_tier(vram_gb):
        if vram_gb == 0:  return "T5"
        if vram_gb < 6:   return "T1"
        if vram_gb < 14:  return "T2"
        if vram_gb < 22:  return "T3"
        return "T4"

    vram  = detect_gpu_vram()
    ram   = round(psutil.virtual_memory().total / (1024**3), 1)
    tier  = assign_tier(vram)

    with open("config/model-library.json") as f:
        library = json.load(f)
    models = library["tiers"][tier]["models"]

    with open("config/active-models.json","w") as f:
        json.dump({"tier":tier,"vram_gb":vram,"ram_gb":ram,"models":models},f,indent=2)

    print(f"Detected: {vram}GB VRAM, {ram}GB RAM → Tier {tier}")

Done when: Hardware tier detected. active-models.json written.
           All models respond. Docker runs. Python 3.11 active.
```

---

**Day 2 — Tuesday**
**Task: Repository Scaffold + model-library.json**

```
PART A — Full folder structure

  mkdir -p ark-os && cd ark-os && git init
  mkdir -p core memory kernel workers/phase1 workers/phase2 workers/phase3
  mkdir -p sage tools/science api dashboard cli
  mkdir -p benchmarks/arkbench benchmarks/coding_self_repair
  mkdir -p config scripts installers/windows/lib
  mkdir -p tests/memory tests/kernel tests/workers tests/tools tests/integration
  mkdir -p docs research/conflict_data

PART B — config/model-library.json

  Write the complete model library that select-model.py reads:

  {
    "tiers": {
      "T1": {
        "vram_min_gb": 3, "vram_max_gb": 5,
        "models": {
          "general":  "qwen2.5:3b",
          "code":     "qwen2.5-coder:3b",
          "vision":   null,
          "orchestration": "groq_required"
        }
      },
      "T2": {
        "vram_min_gb": 6, "vram_max_gb": 10,
        "models": {
          "general":  "gemma4:26b-instruct-qat",
          "code":     "qwen2.5-coder:7b",
          "vision":   "qwen2-vl:7b",
          "orchestration": "groq_preferred"
        }
      },
      "T3": {
        "vram_min_gb": 14, "vram_max_gb": 20,
        "models": {
          "general":  "qwen2.5:14b",
          "code":     "qwen2.5-coder:14b",
          "vision":   "qwen2-vl:7b",
          "orchestration": "local_capable"
        }
      },
      "T4": {
        "vram_min_gb": 22, "vram_max_gb": 9999,
        "models": {
          "general":  "qwen2.5:32b",
          "code":     "qwen2.5-coder:32b",
          "vision":   "qwen2-vl:72b",
          "orchestration": "fully_local"
        }
      },
      "T5": {
        "vram_min_gb": 0, "vram_max_gb": 0,
        "models": {
          "general":  "phi3:mini",
          "code":     "phi3:mini",
          "vision":   null,
          "orchestration": "groq_required"
        }
      }
    }
  }

PART C — installers/windows/lib/tier-map.ps1
  Windows PowerShell version of hardware detection:

  $gpu = (Get-WmiObject Win32_VideoController |
          Where-Object {$_.AdapterRAM -gt 0} |
          Select-Object -First 1).AdapterRAM
  $vram_gb = [math]::Round($gpu / 1GB, 1)

  if     ($vram_gb -ge 22) { $tier = "T4" }
  elseif ($vram_gb -ge 14) { $tier = "T3" }
  elseif ($vram_gb -ge 6)  { $tier = "T2" }
  elseif ($vram_gb -ge 3)  { $tier = "T1" }
  else                      { $tier = "T5" }

  Write-Output "Detected ${vram_gb}GB VRAM → Tier $tier"
  $tier | Out-File -FilePath "config\active-tier.txt"

PART D — Create placeholder files for all modules
  touch core/__init__.py core/ark_message.py core/base_agent.py
  touch core/message_bus.py core/agent_registry.py core/event_emitter.py
  touch memory/__init__.py memory/memory_agent.py
  touch memory/working_memory.py memory/episodic_memory.py
  touch memory/semantic_memory.py memory/procedural_memory.py
  touch memory/citation_graph.py
  touch kernel/__init__.py kernel/supervisor_agent.py
  touch kernel/planner_agent.py kernel/evaluation_agent.py
  touch kernel/router.py kernel/heartbeat_monitor.py
  touch kernel/conflict_resolver.py kernel/ark_state.py
  touch workers/phase1/research_agent.py workers/phase1/coding_agent.py
  touch api/main.py api/events.py
  touch cli/ark_cli.py
  touch docker-compose.yml README.md pyproject.toml

PART E — pyproject.toml
  [tool.poetry.dependencies]
  python = ">=3.11,<3.12"

  [tool.poetry.scripts]
  ark = "cli.ark_cli:main"

PART F — First git commit
  git add . && git commit -m "[Day 02] Scaffold: full repo structure + model-library.json"

Done when: All folders exist. model-library.json complete.
           Windows PS1 script written. Git shows clean tree.
```

---

**Day 3 — Wednesday**
**Task: Docker Compose — All Services Up**

```
File: docker-compose.yml

Services:
  qdrant:
    image: qdrant/qdrant:latest
    ports: ["6333:6333"]
    volumes: ["qdrant_data:/qdrant/storage"]

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]
    volumes: ["redis_data:/data"]
    command: redis-server --appendonly yes

  neo4j:
    image: neo4j:5.15-community
    ports: ["7474:7474", "7687:7687"]
    environment:
      NEO4J_AUTH: neo4j/ark_os_password
    volumes: ["neo4j_data:/data"]

  mlflow:
    image: ghcr.io/mlflow/mlflow:latest
    ports: ["5000:5000"]
    command: mlflow server --host 0.0.0.0
    volumes: ["mlflow_data:/mlflow"]

  ark-sandbox:
    build: ./docker/sandbox/
    # isolated Python environment for Coding Agent
    # pre-installs: numpy pandas matplotlib torch scikit-learn

  # ark-api and ark-ui added in Month 2

volumes:
  qdrant_data: redis_data: neo4j_data: mlflow_data:

Also write: docker/sandbox/Dockerfile
  FROM python:3.11-slim
  RUN pip install numpy pandas matplotlib scikit-learn torch requests
  WORKDIR /sandbox
  CMD ["python"]

Commands:
  docker compose up -d
  curl http://localhost:6333/healthz   # Qdrant ✓
  redis-cli ping                       # PONG ✓
  curl http://localhost:7474           # Neo4j ✓
  curl http://localhost:5000/health    # MLflow ✓

Done when: All 5 services healthy. docker compose ps all green.
```

---

**Day 4 — Thursday**
**Task: ARKMessage Schema — AgentSpec v1.0 FROZEN**

```
This is frozen after today. Never change it.

File: core/ark_message.py

from typing import TypedDict
from enum import Enum

class Intent(str, Enum):
    RESEARCH   = "RESEARCH"
    CODE       = "CODE"
    SEARCH     = "SEARCH"
    EXECUTE    = "EXECUTE"
    STORE      = "STORE"
    EVALUATE   = "EVALUATE"
    REFLECT    = "REFLECT"
    DISCOVER   = "DISCOVER"
    SUPERVISE  = "SUPERVISE"
    PLAN       = "PLAN"
    BROWSE     = "BROWSE"
    SECURITY   = "SECURITY"
    VISION     = "VISION"

class Status(str, Enum):
    PENDING  = "PENDING"
    RUNNING  = "RUNNING"
    DONE     = "DONE"
    FAILED   = "FAILED"
    RETRY    = "RETRY"

class ARKMessage(TypedDict):
    # Routing
    task_id:        str
    sender:         str
    receiver:       str
    parent_task_id: str

    # Intent and content
    intent:         str
    payload:        dict
    memory_context: list

    # Control
    priority:       int
    status:         str
    retry_count:    int
    max_retries:    int

    # Output (populated on completion)
    output:         dict
    quality_score:  float   # -1.0 = not yet scored
    timestamp:      str     # ISO 8601

    # NEW — Agent versioning (added in v2.0)
    agent_version:  str     # e.g. "coding_agent@1.2.0"

def create_ark_message(sender, receiver, intent, payload,
                       priority=5, parent_task_id="") -> ARKMessage:
    from datetime import datetime, timezone
    return ARKMessage(
        task_id=f"ark_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}",
        sender=sender, receiver=receiver,
        parent_task_id=parent_task_id,
        intent=intent, payload=payload,
        memory_context=[],
        priority=priority,
        status=Status.PENDING,
        retry_count=0, max_retries=3,
        output={}, quality_score=-1.0,
        timestamp=datetime.now(timezone.utc).isoformat(),
        agent_version=""
    )

Tests: tests/test_ark_message.py
  - Factory creates valid message
  - Missing required fields raise error
  - All Intent + Status enum values accessible

Done when: Tests pass. Tagged: git tag v1.0-agentspec
```

---

**Day 5 — Friday**
**Task: BaseAgent Abstract Class**

```
File: core/base_agent.py

from abc import ABC, abstractmethod
from ark_os.core.ark_message import ARKMessage

class BaseAgent(ABC):
    def __init__(self, agent_name: str, model_name: str, version: str = "1.0.0"):
        self.agent_name  = agent_name
        self.model_name  = model_name
        self.version     = version        # e.g. "1.0.0" → agent versioning

    # ── Abstract — must implement ───────────────────────────────────
    @abstractmethod
    def run(self, message: ARKMessage) -> ARKMessage:
        pass

    @abstractmethod
    def get_capabilities(self) -> list[str]:
        pass

    # ── Provided defaults — override if needed ──────────────────────
    def register(self, registry) -> None:
        registry.register(
            name=self.agent_name,
            capabilities=self.get_capabilities(),
            model=self.model_name,
            version=self.version,       # version tracked in registry
            status="IDLE"
        )

    def health_check(self) -> dict:
        from datetime import datetime, timezone
        return {
            "status": "IDLE",
            "current_task": None,
            "version": self.version,
            "last_heartbeat": datetime.now(timezone.utc).isoformat()
        }

    def validate_message(self, message: ARKMessage) -> bool:
        required = ["task_id","sender","receiver","intent",
                    "payload","status","timestamp"]
        return (
            all(f in message for f in required)
            and message["status"] == "PENDING"
            and message["receiver"] == self.agent_name
        )

    def on_error(self, message: ARKMessage, exception: Exception) -> ARKMessage:
        message["status"]       = "FAILED"
        message["output"]       = {
            "error_type":    type(exception).__name__,
            "error_message": str(exception),
            "agent":         self.agent_name,
            "agent_version": self.version
        }
        message["quality_score"] = 0.0
        return message

    def _run_with_error_handling(self, message: ARKMessage) -> ARKMessage:
        message["agent_version"] = f"{self.agent_name}@{self.version}"
        if not self.validate_message(message):
            return self.on_error(message, ValueError("Invalid ARKMessage"))
        try:
            return self.run(message)
        except Exception as e:
            return self.on_error(message, e)

Tests: tests/test_base_agent.py
Done when: All tests pass. BaseAgent importable. Abstract enforcement works.
```

---

**Day 6 — Saturday**
**Task: Agent Registry + Versioning Support**

```
File: core/agent_registry.py

AgentRecord fields (8 original + version added):
  name              str    unique agent ID
  capabilities      list   intent types handled
  model             str    LLM used
  version           str    semantic version e.g. "1.2.0"  ← NEW
  status            str    IDLE | BUSY | ERROR | OFFLINE
  current_task      str    task_id or ""
  last_heartbeat    str    ISO 8601
  load_count        int    total tasks handled (for load balancing)
  avg_quality_score float  rolling average (triggers self-improvement)

Methods:
  register(record)
  deregister(name)
  get_agent(name) -> AgentRecord
  find_agents_for_intent(intent) -> list
  get_least_loaded(agents) -> AgentRecord
  update_status(name, status)
  update_version(name, new_version)     ← NEW: Self-Improvement updates this
  update_quality(name, score)           rolling average
  heartbeat(name)
  get_all() -> list[AgentRecord]

Storage: Redis hash per agent → "registry:{agent_name}"

Version history: Redis list → "registry:version_history:{agent_name}"
  Each entry: {version, timestamp, benchmark_score, changed_by}
  Self-Improvement Agent appends here when it upgrades an agent

Tests: tests/test_agent_registry.py
Done when: Registry stores 8-field records in Redis. Version history works.
```

---

**Day 7 — Sunday**
**Task: Week 1 Review**

```
No new code today.

Checklist:
  □ Hardware detected correctly (run scripts/select-model.py)
  □ model-library.json has all 5 tiers
  □ Docker services all healthy
  □ ARKMessage schema complete — last chance to change anything
  □ BaseAgent with versioning support
  □ Agent Registry with version history

Write in notebook:
  - Does the ARKMessage schema feel complete?
  - Is there anything missing from model-library.json?
  - Which tier did your hardware land on?

FINAL DECISION POINT: AgentSpec v1.0 is permanently frozen after this.
```

---

### WEEK 2 — Memory Agent (Most Critical Week)

---

**Day 8 — Monday: Redis Working Memory**
```
File: memory/working_memory.py
Methods: set(task_id, key, value, ttl=3600), get, get_all, delete, clear_task, exists
Key pattern: "working:{task_id}:{key}"
Tests: TTL expiry, multi-type storage, clear_task isolation
Done when: All tests pass.
```

**Day 9 — Tuesday: SQLite Episodic Memory**
```
File: memory/episodic_memory.py
Table: episodes (id, task_id, agent_name, agent_version, intent,
                  summary, quality_score, timestamp, metadata)
agent_version column added → tracks which version of agent ran
Methods: log, query_by_agent, query_by_intent, query_by_timerange,
         query_recent, search, query_by_version
Tests: persist across restart, keyword search, version filter
Done when: All tests pass. SQLite written to disk.
```

**Day 10 — Wednesday: Qdrant Semantic Memory**
```
File: memory/semantic_memory.py
Embedding model: BAAI/bge-large-en-v1.5 (1024-dim)
Collection: ark_semantic_memory
Methods: store, retrieve, retrieve_by_metadata, delete, count
Tests: semantic similarity works, metadata filter works
Done when: Vectors stored, cosine similarity retrieval correct.
```

**Day 11 — Thursday: Procedural Memory (JSON + FAISS)**
```
File: memory/procedural_memory.py
Skill schema: skill_id, intent, description, steps,
              success_rate, use_count, last_used, source
Storage: skills.json + skills.faiss
Retrieval: FAISS nearest neighbor × success_rate weighted
Done when: Skills saved, FAISS index queryable.
```

**Day 12 — Friday: Memory Agent Unified Interface**
```
File: memory/memory_agent.py
API: store, retrieve, forget, inject_context
CRITICAL TEST: memory survives process kill + restart
Done when: All 4 tiers accessible. Persistence test passes.
```

**Day 13 — Saturday: Redis Message Bus**
```
File: core/message_bus.py
Channels: agent:{name}, ark:broadcast, ark:heartbeat, ark:audit_log
Class: ARKMessageBus — publish, subscribe, broadcast,
       publish_heartbeat, log_to_audit, get_audit_log
Done when: Two agents communicate through Redis pub/sub.
```

**Day 14 — Sunday: Week 2 Review — Memory Audit**
```
Full audit of all 4 memory tiers. Must all pass before moving on.
Write: docs/memory_architecture.md
Only proceed to Week 3 if every persistence test passes.
```

---

### WEEK 3 — Supervisor + Planner + Router

---

**Day 15 — Monday: LangGraph State Machine**
```
File: kernel/ark_state.py + kernel/ark_graph.py
ARKState fields: task_id, original_goal, current_plan, results,
                 active_agents, memory_context, iteration, status,
                 privacy_mode (bool) ← NEW: privacy flag flows through state
Done when: LangGraph graph compiles. State flows through stub node.
```

**Day 16 — Tuesday: Supervisor Agent + STATE.md**
```
File: kernel/supervisor_agent.py
Model: claude-sonnet-4-6 (primary) → groq:qwen2.5-72b (fallback)
Privacy mode handling:
  if state.privacy_mode == True:
    → force all calls to local models only
    → set flag on every ARKMessage: {"privacy": true}
    → Planner must also use local fallback

STATE.md — Auto-updated by Supervisor after every task:
  The 4-tier memory (Redis/SQLite/Qdrant/FAISS) is machine-readable.
  STATE.md is what YOU read to instantly understand where ARK OS is.
  Located at project root. Supervisor writes it after every task completion.

  Format:
    # ARK OS STATE
    Last updated: 2026-07-10 02:14 UTC
    ─────────────────────────────────────
    Current goal:    Solar cell materials discovery
    Active loop:     SAGE iteration 3/5
    Done condition:  novelty_score > 0.7 AND feasibility > 0.6
    Condition met:   ✗ (novelty=0.61, feasibility=0.71)
    ─────────────────────────────────────
    Last task:       coding_agent → implement_band_gap_predictor
    Last score:      0.84 (coding_agent@1.3.1)
    Next scheduled:  arkbench daily at 07:00
    Skills learned:  3 today / 47 total
    ─────────────────────────────────────
    Agents online:   17/20
    Agents offline:  vision_agent, ml_engineer_agent, devops_agent
    Memory usage:    Redis 142MB / Qdrant 4,821 vectors / Episodes 1,204

  File: core/state_writer.py
    class StateWriter:
        STATE_PATH = "STATE.md"

        def update(self, ark_state: ARKState, last_result: dict):
            lines = [
                "# ARK OS STATE",
                f"Last updated: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}",
                "─" * 37,
                f"Current goal:    {ark_state.original_goal}",
                f"Active loop:     {ark_state.current_loop or 'None'}",
                f"Done condition:  {ark_state.done_condition or 'Not set'}",
                f"Condition met:   {self._check_condition(ark_state)}",
                "─" * 37,
                f"Last task:       {last_result.get('agent')} → {last_result.get('task')}",
                f"Last score:      {last_result.get('score')} ({last_result.get('version')})",
                f"Next scheduled:  {self._next_schedule()}",
                f"Skills learned:  {self._skills_today()} today / {self._skills_total()} total",
                "─" * 37,
                f"Agents online:   {self._agents_online()}",
            ]
            with open(self.STATE_PATH, "w") as f:
                f.write("\n".join(lines))

  Supervisor calls StateWriter.update() after every task — no manual step.

Done when: Supervisor classifies 10 goal types correctly.
           STATE.md written at project root after each task.
```

**Day 17 — Wednesday: Planner Agent**
```
File: kernel/planner_agent.py
Model: claude-sonnet-4-6 (primary) → groq:qwen2.5-72b (fallback)
Output: task DAG JSON with depends_on + parallel_with fields
Done when: Planner produces valid acyclic DAGs for 5 goal types.
```

**Day 18 — Thursday: Supervisor ↔ Planner Integration**
```
Wire through Redis message bus.
Memory Agent inject_context called before every routing step.
Integration test: "Research transformers and implement a demo"
Done when: Full Supervisor → Planner flow end-to-end.
```

**Day 19 — Friday: Router + Topological Dispatch**
```
File: kernel/router.py
Topological sort of task DAG.
Parallel dispatch for independent tasks.
Dependency tracking: task B not dispatched until task A DONE.
Done when: 4-task DAG dispatched in correct order.
```

**Day 20 — Saturday: Heartbeat Monitor**
```
File: kernel/heartbeat_monitor.py
Every 30 seconds: agents ping ark:heartbeat
3 missed pings → mark OFFLINE
Routing skips OFFLINE agents
Done when: Dead agents detected and skipped automatically.
```

**Day 21 — Sunday: Week 3 Review**
```
End-to-end trace: "Research quantum computing"
→ Supervisor → Planner → DAG → Router → (Research stub)
Write: docs/kernel_architecture.md
```

---

### WEEK 4 — Research + Coding + Evaluation

---

**Day 22 — Monday: arXiv + Semantic Scholar Tools**
```
File: tools/research_tools.py
Functions: search_arxiv, fetch_arxiv_paper,
           search_semantic_scholar, get_paper_citations
Done when: arXiv search returns correct results.
```

**Day 23 — Tuesday: Research Agent**
```
File: workers/phase1/research_agent.py
Model: gemma4:26b-instruct-qat (local, from active-models.json)
RAG pipeline: search → fetch → embed → store in Qdrant → synthesize
Done when: Returns structured summary with arxiv citations.
```

**Day 24 — Wednesday: Code Execution Sandbox**
```
File: tools/code_tools.py
All code runs in ark-sandbox Docker container — never on host.
Functions: run_python_code, install_package, write_file,
           read_file, run_tests
Done when: Timeout kills runaway code. Host filesystem inaccessible.
```

**Day 25 — Thursday: Coding Agent + Self-Repair Loop**
```
File: workers/phase1/coding_agent.py
Model: qwen2.5-coder:7b (local, from active-models.json)
Self-repair: write → test → fail → diagnose → fix → test (max 3)
Done when: Agent self-repairs 1 injected bug correctly.
```

**Day 26 — Friday: Evaluation Agent**
```
File: kernel/evaluation_agent.py
Model: claude-haiku-4-5
Rubrics: Research(40/30/30) Code(50/20/15/15)
         Security(50/30/20) Hypothesis(50/30/20) Data(50/25/25)
Thresholds: RETRY=0.65, ACCEPT=0.80
Done when: Correctly scores 6 different output types.
```

**Day 27 — Saturday: Full MVP Loop Integration Test**
```
Goal: "Research Fibonacci algorithms, implement the most efficient one"
Full flow: User → Supervisor → Planner → Research → Coding → Evaluation → Memory
Done when: Loop completes autonomously. Score >= 0.70.
```

**Day 28 — Sunday: Month 1 Review**
```
Full audit of all 15 components built.
Write: docs/month1_results.md
Start Paper #1 introduction draft.
```

---

# MONTH 2 — COMPLETE MVP + VERSIONING + PRIVACY + DASHBOARD
## Goal: Harden loop + Agent versioning + Privacy mode + Dashboard + ARKBench v0.1

---

### WEEK 5 — Harden Core Loop + New Features

---

**Day 29 — Monday: Retry Logic + Escalation**
```
File: kernel/supervisor_agent.py (update)
3-attempt retry loop with escalation handler.
EscalationHandler: logs to episodic, notifies dashboard via WebSocket.
Done when: 3-retry loop works. Escalation report generated.
```

---

**Day 30 — Tuesday: Agent Versioning System**

```
This is the scientific rigour layer. Every agent change is tracked.

File: core/agent_versioner.py

AgentVersion:
  agent_name:       str
  version:          str    # semver: major.minor.patch
  prompt_hash:      str    # sha256 of system prompt
  model:            str
  benchmark_score:  float  # ARKBench score at this version
  changed_by:       str    # "self_improvement" | "manual" | "initial"
  created_at:       str    # ISO timestamp
  change_summary:   str    # what changed and why

Class AgentVersioner:
  bump_version(agent_name, change_type, new_prompt, score, changed_by)
    → increments patch (bug fix), minor (improvement), major (rewrite)
    → saves to version history in Redis
    → updates agent_registry with new version string

  get_version_history(agent_name) -> list[AgentVersion]
  rollback(agent_name, target_version)
    → retrieves old prompt from history
    → updates agent to use that prompt
    → logs rollback event

  compare_versions(agent_name, version_a, version_b)
    → returns score delta between two versions

Integration into BaseAgent:
  Every agent constructor takes version="1.0.0"
  Every ARKMessage carries agent_version field
  Every episodic log records which version ran

Why this matters for papers:
  ARKBench results tagged with agent version
  "coding_agent@1.0.0 scored 0.71 in Month 1"
  "coding_agent@1.3.2 scored 0.84 in Month 6"
  Reviewers cannot question: "which version are you comparing?"

Tests: tests/test_agent_versioner.py
  - Version bumps correctly (patch → minor → major)
  - Rollback restores old prompt
  - compare_versions returns correct delta

Done when: All agents carry version strings. History stored in Redis.
```

---

**Day 31 — Wednesday: Reflection Agent**
```
File: workers/phase2/reflection_agent.py
Model: groq:qwen2.5-72b
After every task: analyze logs → extract patterns → update procedural memory
Output: {what_worked, what_failed, pattern_learned, skill_update,
         flag_for_self_improvement, agent_version_recommendation}
flag_for_self_improvement → queued for Self-Improvement Agent in Month 6
Done when: Reflection runs automatically after every task.
```

---

**Day 32 — Thursday: Conflict Resolution — Type 3 (Priority)**
```
File: kernel/conflict_resolver.py
Resolution rules: higher priority wins → earlier timestamp → round-robin
ConflictResolver: detect_priority_conflict, resolve_priority, log_conflict
Done when: Priority conflicts resolved correctly in all test cases.
```

---

**Day 33 — Friday: Privacy Mode Flag**

```
One-day build. Massive impact for institutional/enterprise users.

Files to update:
  cli/ark_cli.py
  api/main.py
  kernel/supervisor_agent.py
  kernel/planner_agent.py

CLI usage:
  ark --privacy "research confidential topic"
  ark --privacy sage "materials science discovery"

API usage:
  POST /task
  Body: {"goal": "...", "privacy": true}

What privacy mode does:
  1. Sets ARKState.privacy_mode = True
  2. Supervisor: skips Claude API → uses local Qwen2.5-14B via Ollama
  3. Planner: skips Groq → uses local Qwen2.5-14B via Ollama
  4. Evaluation: skips Claude Haiku → uses local Gemma4
  5. ALL agents: log only to local SQLite/Redis, never to cloud
  6. Network: no outbound API calls except arXiv/Semantic Scholar
              (configurable — can disable those too with --airgap flag)

Config: config/privacy_config.json
  {
    "privacy_mode": false,
    "airgap_mode": false,           # even stricter: no internet at all
    "allowed_external": [           # whitelist when privacy=true
      "api.semanticscholar.org",
      "export.arxiv.org"
    ]
  }

Quality impact (documented honestly):
  ~15-20% lower routing quality vs Claude Sonnet
  Acceptable for sensitive use cases

Target users:
  - Corporate researchers (confidential IP)
  - Government/defence labs
  - Healthcare research (HIPAA considerations)
  - Anyone in regions with data sovereignty laws

Tests: tests/test_privacy_mode.py
  - Privacy mode: zero API calls to Anthropic/Groq verified
  - Airgap mode: zero outbound connections verified
  - Quality score still > 0.60 on basic tasks in privacy mode

Done when: Privacy mode verifiably makes zero external API calls.
```

---

**Day 34 — Saturday: Web Search + File System Agents**
```
File: workers/phase2/web_search_agent.py
  Model: reads from active-models.json (general tier)
  Tools: DuckDuckGo (no key) or Tavily (key)
  Privacy mode: skip web search entirely if --airgap

File: workers/phase2/file_system_agent.py
  Allowed directory whitelist in config/
  Logs every write to episodic memory

Done when: Both agents working. Privacy mode respected.
```

---

**Day 35 — Sunday: Week 5 Review**
```
Check:
  □ Retry + escalation working
  □ Agent versioning system complete
  □ Reflection Agent producing useful output
  □ Privacy mode: zero API calls verified
  □ Web Search + File System agents working
```

---

### WEEK 6 — FastAPI + Dashboard + ARKBench

---

**Day 36 — Monday: FastAPI Backend + Privacy Routes**
```
File: api/main.py

Routes:
  POST /task              → submit task (supports privacy: bool in body)
  GET  /task/{task_id}    → task status + output
  GET  /tasks             → recent task list
  DELETE /task/{task_id}  → cancel task
  GET  /agents            → agent registry dump
  GET  /agents/{name}/versions  → version history  ← NEW
  GET  /memory/{tier}     → memory contents
  POST /memory/search     → semantic search
  POST /config/privacy    → toggle privacy mode    ← NEW
  WebSocket /ws           → event stream

Done when: All routes work. Privacy toggle functional.
```

**Day 37 — Tuesday: WebSocket Event System + Terminal CLI + Done Conditions**
```
File: api/events.py + core/event_emitter.py
All agent actions emit events: agent_started, agent_thought,
tool_called, tool_result, agent_done, memory_written,
task_completed, task_failed, conflict_detected,
version_updated ← NEW event type

File: cli/ark_cli.py
Commands: ark "goal", ark --privacy "goal", ark status, ark list,
          ark stop, ark agents, ark memory search/show,
          ark logs, ark versions <agent_name>  ← NEW

--until Done Conditions (Loop Engineering concept):
  Instead of running "until the model thinks it's done",
  the loop runs until an OBJECTIVE, TESTABLE condition is met.
  The Evaluation Agent checks the condition after every iteration.

  Examples:
    ark --until "all_tests_pass AND coverage > 80" "implement auth system"
    ark --until "novelty_score > 0.7" sage "solar cell materials"
    ark --until "security_issues == 0" "scan and fix codebase"
    ark --until "quality_score > 0.85" "write research summary on BERT"

  How it works:
    1. User defines the done condition as a string expression
    2. Supervisor stores it in ARKState.done_condition
    3. After each iteration, Evaluation Agent parses + checks the condition
    4. STATE.md shows: Done condition: [expression] | Met: ✗ (current=0.61)
    5. Loop continues until condition is True OR max_iterations hit
    6. Max iterations default: 10 (override: --max-iter 20)

  File: core/done_condition.py
    class DoneConditionEvaluator:
        ALLOWED_VARS = {
            "quality_score", "novelty_score", "feasibility_score",
            "all_tests_pass", "coverage", "security_issues",
            "self_repair_rate", "citation_count"
        }

        def evaluate(self, condition_str: str, metrics: dict) -> bool:
            # Safe eval — only allowed variable names
            safe_vars = {k: metrics.get(k, 0) for k in self.ALLOWED_VARS}
            return bool(eval(condition_str, {"__builtins__": {}}, safe_vars))

        def check(self, condition_str: str, ark_state: ARKState) -> tuple[bool, dict]:
            metrics  = self._collect_metrics(ark_state)
            met      = self.evaluate(condition_str, metrics)
            return met, metrics

  API support:
    POST /task
    Body: {"goal": "...", "until": "quality_score > 0.85", "max_iter": 10}

Done when: ark --until command runs loop until condition met.
           Loop stops immediately when condition is True.
           STATE.md shows condition + current values after each iteration.
```

**Day 38 — Wednesday: Web Dashboard — Agent Grid + Live Log**
```
Directory: dashboard/ (React + Vite)
npm install socket.io-client recharts lucide-react reactflow

Components:
  AgentGrid.jsx — 20 agent cards, live status colors
                  Shows version string on each card  ← NEW
  TaskQueue.jsx — active + recent tasks
  LiveLog.jsx   — scrolling event stream

Done when: Dashboard shows live agent status with version numbers.
```

**Day 39 — Thursday: Dashboard — Task Detail + Memory Browser + Version History**
```
Components:
  TaskDetail.jsx      — DAG, agent log, output, download button
  MemoryBrowser.jsx   — 4-tier browser with search
  VersionHistory.jsx  ← NEW: click any agent → see full version history
                        Shows: version, date, score delta, what changed
                        Rollback button per version

Done when: Can browse memory, view version history, rollback an agent.
```

**Day 40 — Friday: ARKBench v0.1 — First 10 Tasks**
```
File: benchmarks/arkbench/benchmark_v0.1.json
5 research tasks + 5 coding tasks (as originally planned)
Each task records: agent_version field alongside score  ← NEW
Done when: 10 tasks defined. Benchmark runner written.
```

**Day 41 — Saturday: Run ARKBench v0.1 + Record Baseline**
```
python benchmarks/arkbench/run_benchmark.py
Record: benchmarks/arkbench/results/month2_baseline.json
Each result: {task_id, score, duration, agent_version, notes}
This is Month 2 baseline with version stamps.
Done when: All 10 scored. Baseline saved with version info.
```

**Day 42 — Sunday: Month 2 Review**
```
Checklist:
  □ Agent versioning: version history in Redis
  □ Privacy mode: zero external calls verified
  □ FastAPI + WebSocket: all routes working
  □ Terminal CLI: ark command functional
  □ Dashboard: live grid, memory browser, version history
  □ ARKBench v0.1: 10 tasks, baseline scored with versions

Write: docs/month2_results.md
Paper #1: Architecture section draft started
```

---

# MONTH 3 — PHASE 2 WORKERS + LLAMA.CPP INFERENCE
## Goal: Browser + Terminal + GitHub + llama.cpp + Self-repair evaluation

---

### WEEK 7 — Browser + Terminal + GitHub

---

**Day 43 — Monday: Playwright Browser Tool**
```
File: tools/browser_tools.py
pip install playwright && playwright install chromium
Functions: open_browser, navigate, get_page_text, get_page_html,
           click, type_text, screenshot, fill_form, extract_links,
           extract_table, wait_for_element, close_browser
Context manager: async with ARKBrowser() as b: ...
Done when: Navigate, read, screenshot any website.
```

**Day 44 — Tuesday: Browser Agent**
```
File: workers/phase2/browser_agent.py
Model: reads from active-models.json (general tier)
ReAct loop: thought → action → observe → repeat
Actions: navigate, click, type, extract, screenshot
Done when: Navigates arXiv, extracts paper titles autonomously.
```

**Day 45 — Wednesday: Terminal Agent**
```
File: workers/phase2/terminal_agent.py + tools/terminal_tools.py
ALL commands run in Docker sandbox — never on host.
run_command(cmd, timeout=30, sandbox=True)
Parses ModuleNotFoundError → auto-suggests pip install
Done when: bash commands run safely, output parsed intelligently.
```

**Day 46 — Thursday: Conflict Resolution — Type 1 (Resource)**
```
File: kernel/conflict_resolver.py (update)
ToolSemaphore: Browser max 1, Terminal max 2, arXiv API max 3
acquire(tool_name, agent_name) → queued if busy
release triggers next queued agent
Done when: No two agents use same exclusive tool simultaneously.
```

**Day 47 — Friday: GitHub Agent**
```
File: workers/phase2/github_agent.py + tools/github_tools.py
pip install gitpython PyGithub
Functions: clone_repo, get_open_issues, create_branch,
           commit_files, create_pull_request, list_changed_files
Done when: Agent commits files, creates branch, opens PR.
```

**Day 48 — Saturday: Coding Agent Self-Repair Evaluation**
```
File: benchmarks/coding_self_repair/run_self_repair_eval.py
10 intentionally broken code tasks (syntax, import, logic, type errors)
Metric: Self-Repair Rate = tasks_fixed / 10
Target: >= 7/10 within 3 attempts
Record: agent version used alongside results
Done when: Evaluation complete. Results saved.
```

**Day 49 — Sunday: Week 7 Review**
```
Write: docs/self_repair_evaluation.md
Include: per-error-type repair rates, agent version used
```

---

### WEEK 8 — Document + Security + llama.cpp Migration

---

**Day 50 — Monday: Document Agent**
```
File: workers/phase2/document_agent.py + tools/document_tools.py
pip install pypdf python-docx openpyxl reportlab
Functions: read_pdf, read_docx, read_xlsx, write_docx, write_pdf, write_xlsx
Done when: Reads real PDF paper. Writes formatted DOCX report.
```

**Day 51 — Tuesday: Security Agent**
```
File: workers/phase2/security_agent.py + tools/security_tools.py
pip install semgrep bandit python-nmap
CRITICAL: Never sends user code to external APIs.
Functions: run_semgrep, run_bandit, check_dependencies,
           check_owasp_top10, generate_audit_report
Done when: Finds SQL injection + hardcoded credentials in test files.
```

**Day 52 — Wednesday: Neo4j Citation Graph**
```
File: memory/citation_graph.py
pip install neo4j
Nodes: Paper, Concept
Edges: CITES, MENTIONS, RELATED
Functions: add_paper, add_citation, find_concept_gaps,
           find_bridge_concepts
Done when: 5 papers produce a queryable citation graph in Neo4j.
```

**Day 53 — Thursday: Conflict Resolution — Type 4 (Memory)**
```
File: kernel/conflict_resolver.py (update)
When two agents write conflicting procedural skills:
  Keep both versions tagged with source + success_rate
  Retrieval uses higher success_rate version
  Reflection Agent notified for human review
Done when: Memory conflicts versioned, best version retrieved.
```

---

**Day 54 — Friday: llama.cpp Inference Layer**

```
Replace Ollama inference with llama.cpp for 10–30% speed improvement.
Ollama still used for model management only (pull, list, delete).

File: core/llm_backend.py

from llama_cpp import Llama
import json

class ARKInferenceBackend:
    """
    Replaces direct Ollama API calls with llama.cpp for all local models.
    Ollama is still used to manage (download, list) models.
    llama.cpp is used for actual token generation.
    """
    def __init__(self):
        # Load active models from hardware detection
        with open("config/active-models.json") as f:
            self.active = json.load(f)
        self._models = {}   # cache loaded models

    def _get_model_path(self, role: str) -> str:
        """Find GGUF path for the given role (general, code, vision)"""
        model_name = self.active["models"][role]
        # Ollama stores models here:
        base = os.path.expanduser("~/.ollama/models/blobs/")
        # Map model name to GGUF file
        # (Ollama pull already downloaded it — we just point llama.cpp at it)
        return self._find_gguf(base, model_name)

    def load(self, role: str) -> Llama:
        """Load model into memory. Cache after first load."""
        if role not in self._models:
            path = self._get_model_path(role)
            self._models[role] = Llama(
                model_path=path,
                n_gpu_layers=-1,    # push ALL layers to GPU (RTX 5060)
                n_ctx=8192,
                n_batch=512,
                verbose=False
            )
        return self._models[role]

    def unload(self, role: str):
        """Free VRAM — important for 8GB GPU hot-swapping."""
        if role in self._models:
            del self._models[role]
            import gc; gc.collect()

    def generate(self, role: str, messages: list, max_tokens=2048,
                 temperature=0.7) -> str:
        llm = self.load(role)
        response = llm.create_chat_completion(
            messages=messages,
            max_tokens=max_tokens,
            temperature=temperature,
            stream=False
        )
        return response["choices"][0]["message"]["content"]

    def generate_stream(self, role: str, messages: list, **kwargs):
        """Streaming version — used by CLI for live output."""
        llm = self.load(role)
        for chunk in llm.create_chat_completion(messages=messages,
                                                 stream=True, **kwargs):
            delta = chunk["choices"][0]["delta"]
            if "content" in delta:
                yield delta["content"]

# Hot-swap policy for 8GB VRAM:
VRAM_SWAP_POLICY = {
    "supervisor":   "api",       # Claude/Groq — always API
    "planner":      "api",
    "evaluation":   "api",
    "research":     "general",   # local Gemma4
    "coding":       "code",      # local Qwen-Coder (swap from general)
    "vision":       "vision",    # local Qwen2-VL (swap from general)
    "default":      "general"
}

Update BaseAgent to use ARKInferenceBackend instead of direct Ollama calls.

Before: response = requests.post("http://localhost:11434/api/chat", ...)
After:  response = self.backend.generate(role, messages)

Performance test:
  Run 10 Coding Agent tasks with Ollama
  Run same 10 tasks with llama.cpp
  Record: tokens/second, latency per task
  Expected improvement: 10–30% faster generation

File: benchmarks/inference/llama_vs_ollama.py

Done when: llama.cpp generates tokens correctly for all 3 local model roles.
           Performance test shows improvement. Hot-swap between roles works.
```

---

**Day 55 — Saturday: 14-Agent Integration Test**
```
Goal: "Find CVE papers, implement vulnerable Flask app, scan it, write report"
Agents: Supervisor, Planner, Memory, Research, Web Search, Coding,
        Security, Document, Reflection
Expected outputs: research_summary.md, vulnerable_app.py,
                  security_audit.html, reflection_log.json
Done when: All agents activate. Pipeline completes end-to-end.
```

**Day 56 — Sunday: Month 3 Review**
```
Checklist:
  □ Browser Agent working
  □ Terminal Agent sandboxed
  □ GitHub Agent commits + PRs
  □ Document Agent reads PDF, writes DOCX
  □ Security Agent finds injected vulnerabilities
  □ llama.cpp: performance improvement measured and documented
  □ 14-agent integration test passed

Write: docs/month3_results.md (include llama.cpp benchmark)
Paper #1: Methods section — inference architecture section added
```

---

# MONTH 4 — HARDENING + ARKBENCH 50 + PAPER #2 DATA

---

### WEEKS 9–10 (Days 57–70)

---

**Day 57 — Conflict Type 2 (Output Conflict)**
```
Hardest conflict type. Research says X, Code proves not-X.
Resolution: both agents re-run with each other's output as context.
2 rounds still conflicting → human escalation flag.
Collect 5 real examples for Paper #2.
```

**Day 58 — Security Red-Team Simulation**
```
Run OWASP WebGoat locally (pre-built vulnerable app).
Security Agent scans it → finds known vulnerabilities.
Target recall: >= 0.75 on OWASP WebGoat.
```

**Day 59 — Memory TTL + Eviction Policies**
```
Working: auto-expire after task (Redis TTL)
Episodic: keep last 10,000 episodes, archive older
Semantic: evict vectors with 0 retrieval in 30 days
Procedural: evict skills with success_rate < 0.30
Test: run 100 tasks, verify memory stays bounded.
```

**Day 60 — Performance Optimization**
```
Profile with cProfile + py-spy.
Target latencies:
  Memory retrieval: < 100ms
  Research Agent:   < 2 min for 5 papers
  Coding Agent:     < 3 min for medium task
  Evaluation:       < 30 sec
Fix top 3 bottlenecks.
```

**Day 61 — Graceful Degradation**
```
Groq down      → local Qwen2.5-14B via Ollama
Redis down     → in-memory dict (lose working memory only)
Ollama down    → FAILED with clear message
Qdrant down    → FAISS-only retrieval (degraded)
Test all failure modes.
```

**Day 62 — Dashboard: DAG + Version Tracker Visualization**
```
Add ReactFlow DAG visualization (nodes pulse while running).
Add Version Tracker panel: per-agent score timeline graph
showing ARKBench scores across months for each agent version.
This visualization goes directly into Paper #1 figures.
```

**Day 63 — Sunday: Week 9 Review**

**Day 64 — ARKBench v0.5: Add 20 Tasks**
```
+10 security tasks (CVE-based)
+10 browser/document/terminal tasks
Run all 30. Month 4 intermediate scores saved with agent versions.
```

**Day 65 — Reflection Quality Analysis**
```
After 100+ tasks: do Reflection patterns actually improve behavior?
Write: docs/reflection_quality_analysis.md
```

**Day 66 — Load Balancing Test**
```
Spawn 2 Research Agent instances.
Verify load_count routing distributes evenly.
avg_quality_score threshold deprioritizes underperforming agent.
```

**Day 67 — Paper #2 Data Collection**
```
Log all conflict events from past month.
Classify: Type 1/2/3/4, frequency, resolution time, outcome.
File: research/conflict_data/conflict_log.json
This is the empirical basis for Paper #2 (AAMAS 2027).
```

**Day 68 — Stress Test: 20 Simultaneous Tasks**
```
No race conditions in memory writes.
Semaphores hold under load.
No deadlocks.
Scores consistent with single-task runs.
```

**Day 69 — ARKBench Final 50 Tasks**
```
+20 tasks: 10 agent coordination + 10 mixed domain
All 50 tasks run. Month 4 final scores saved.
Target: 5–10% improvement over Month 2 baseline (early learning effect).
All scores tagged with agent versions.
```

**Day 70 — Month 4 Review**
```
14 agents complete. ARKBench 50 tasks. All conflicts handled.
Write: docs/month4_results.md
Paper #1: Architecture section complete
Paper #2: Conflict data collected, analysis started
```

---

# MONTH 5 — INTELLIGENCE LAYER + SAGE + CHECKPOINTS + SCHEDULING + ARK-VLM
## Goal: 20 agents complete + SAGE loop + fault tolerance + custom VLM

---

### WEEK 11 — Phase 3 Agents

---

**Day 71 — Vision Agent**
```
File: workers/phase3/vision_agent.py
Model: qwen2-vl:7b (via llama.cpp vision pipeline)
Capabilities: SCREENSHOT_ANALYSIS, IMAGE_UNDERSTANDING, OCR, FIGURE_READING
Test: analyze matplotlib figure, extract chart values.
```

**Day 72 — Vision Agent: Scientific Figures**
```
Specialize for scientific output: line plots, bar charts, confusion matrices.
For SAGE Mode: read experiment result figures autonomously.
Test: analyze SAGE-generated plot, write caption.
```

**Day 73 — ML Engineer Agent**
```
File: workers/phase3/ml_engineer_agent.py
Model: qwen2.5-coder:7b (code tier)
MLflow: every experiment auto-logged.
Optuna: Bayesian HPO runs automatically.
Test: 10-trial Optuna study → results in MLflow dashboard.
```

**Day 74 — ML Engineer: Full Experiment Pipeline**
```
Given: {model, dataset, hyperparameter_ranges}
Does: run experiment → log → plot → return best config
Uses Coding + Terminal Agents internally.
Test: sklearn HPO run end-to-end.
```

**Day 75 — DevOps Agent**
```
File: workers/phase3/devops_agent.py
Model: general tier from active-models.json
Capabilities: DOCKER_MANAGE, CI_PIPELINE, DEPLOY
Test: build Docker image from Coding Agent output, run it.
```

**Day 76 — Data Analysis Agent**
```
File: workers/phase3/data_analysis_agent.py
Tools: pandas, matplotlib, scipy stats
Test: given CSV → mean/std/correlation + 3 plots + written summary.
```

**Day 77 — Sunday: Week 11 Review**
```
18 agents operational.
All Phase 3 agents use llama.cpp backend.
Version strings set on all new agents.
```

---

### WEEK 12 — SAGE Components

---

**Day 78 — Hypothesis Engine**
```
File: sage/hypothesis_engine.py (extends Planner)
Input: Qdrant embeddings + Neo4j citation graph
Output: ranked hypotheses by novelty × feasibility score
Novelty: cosine distance from existing paper embeddings
Test: 20 materials science papers → 5 ranked hypotheses.
```

**Day 79 — Experiment Designer**
```
File: sage/experiment_designer.py (extends ML Engineer)
Input: one hypothesis
Output: {hypothesis, dataset, method, baseline, metrics, code_spec}
Coding Agent implements the code_spec.
Test: hypothesis → spec → Coding Agent produces runnable code.
```

**Day 80 — Scientific Report Generator**
```
File: sage/report_generator.py (extends Document Agent)
Input: hypothesis + spec + results + figures
Output: LaTeX paper draft (Abstract, Intro, Related Work, Method, Results)
Citations pulled from Neo4j citation graph automatically.
Test: mock results → valid .tex file.
```

---

**Day 81 — SAGE Checkpoint System**

```
Problem: SAGE loop runs 8 hours, power cuts at hour 7. All lost.
Solution: Checkpoint every completed step.

File: sage/checkpoint_manager.py

CheckpointManager:
  save(loop_id, step_name, data)
    → sage/checkpoints/{loop_id}/{step_name}.json
    → also writes {loop_id}/status.json: {step, status, timestamp}

  load(loop_id, step_name) -> dict | None
    → returns None if checkpoint doesn't exist

  get_resume_point(loop_id) -> str
    → reads status.json, returns last completed step

  list_checkpoints(loop_id) -> list[str]

  clear(loop_id)
    → removes all checkpoints after successful loop completion

SAGE loop update (sage/sage_orchestrator.py):
  Steps: ["literature", "hypothesis", "experiment", "analysis", "report"]

  for step in steps:
    # Check if already completed in a previous interrupted run
    checkpoint = cm.load(loop_id, step)
    if checkpoint:
        result = checkpoint
        log(f"Resumed from checkpoint: {step}")
        continue

    # Run the step
    result = run_step(step)

    # Save checkpoint immediately after completion
    cm.save(loop_id, step, result)

On restart:
  SAGE orchestrator reads status.json
  Skips completed steps
  Resumes from last incomplete step
  User sees: "Resuming SAGE loop ark_sage_001 from step: analysis"

Also add: Checkpoint status to dashboard
  Shows: current SAGE loop + which step + resume-able or not

CLI:
  ark sage resume ark_sage_001    → resume from checkpoint
  ark sage list                   → show all SAGE loops + status
  ark sage clear ark_sage_001     → delete checkpoints after success

Tests: tests/sage/test_checkpoint_manager.py
  - Save step, kill process, restart, resume from saved step
  - All 5 steps checkpointed and resumable

Done when: SAGE loop survives simulated power cut. Resumes correctly.
```

---

**Day 82 — Task Scheduling (Cron-Style)**

```
ARK OS is truly autonomous when it decides WHEN to run tasks.

File: core/task_scheduler.py
pip install apscheduler

Scheduler (wraps APScheduler):
  schedule_cron(cron_expr, goal, privacy=False)
    → "0 2 * * *" = every night at 2 AM

  schedule_interval(interval, goal, privacy=False)
    → "every 6 hours"

  schedule_once(datetime_iso, goal)
    → run exactly once at this time

  schedule_on_file_change(path, goal)
    → watch a file/directory, run on change

  cancel(schedule_id)
  list_schedules() -> list
  pause(schedule_id)
  resume(schedule_id)

Storage: schedules persist in SQLite (survive restarts)
         file: data/schedules.db

CLI:
  ark schedule "0 2 * * *" "sage materials science discovery"
  → Task: ark_sched_001
  → Runs every night at 2 AM while ARK OS is running

  ark schedule "every 6h" "scan codebase for security issues"
  → Runs every 6 hours

  ark schedule "watch /src/" "run tests and fix failures"
  → Runs whenever /src/ directory changes

  ark schedules          → list all active schedules
  ark schedules cancel ark_sched_001

Dashboard additions:
  Schedules panel: list of active scheduled tasks + next run time

Why this matters:
  You submit a schedule, go to class, go to sleep.
  ARK OS runs automatically at the scheduled time.
  No need to keep a terminal open.
  This is the "works while you sleep" feature formalized.

Tests: tests/test_task_scheduler.py
  - Schedule fires at correct time
  - File watcher triggers on directory change
  - Schedules survive process restart

Done when: ark schedule command works. Scheduler survives restart.
```

---

**Day 83 — Sunday: Week 12 Review**
```
SAGE components complete.
Checkpoint system: SAGE is fault-tolerant.
Task scheduling: ARK OS is truly autonomous.
Materials Science API tools: pip install mp-api, add tools/science/materials_tools.py
Test: one full SAGE loop on materials science topic.
```

---

### WEEK 13 — SAGE Debugging + ARK-VLM

---

**Day 84 — First Full SAGE Run**
```
Expect failures. Debug them all.
Goal: "Predict novel solar cell material candidates"
Run all 5 steps. Fix whatever breaks.
```

**Day 85 — Fix Hypothesis Engine Issues**
```
Common issues: hallucinated arxiv IDs, low novelty scores,
too-broad hypotheses. Fix formatting + prompt.
```

**Day 86 — ARK-VLM: Stage 1 Setup**

```
Start building the custom VLM: SigLIP + Qwen2.5 + projection bridge.
This is Paper #5.

File: ark_vlm/model.py

pip install transformers torch sentence-transformers

import torch
import torch.nn as nn
from transformers import SiglipVisionModel, Qwen2ForCausalLM

class ARKVisionModel(nn.Module):
    def __init__(self):
        super().__init__()

        # Vision encoder: SigLIP-400M (FROZEN — Google pretrained)
        self.vision_encoder = SiglipVisionModel.from_pretrained(
            "google/siglip-large-patch16-384"
        )
        for p in self.vision_encoder.parameters():
            p.requires_grad = False   # freeze — never update

        # Language model: Qwen2.5-7B (FROZEN in Stage 1, LoRA in Stage 2)
        self.language_model = Qwen2ForCausalLM.from_pretrained(
            "Qwen/Qwen2.5-7B-Instruct",
            torch_dtype=torch.bfloat16
        )

        # Projection bridge (YOUR TRAINED LAYER — randomly initialized)
        # SigLIP outputs 1152-dim → bridge maps to Qwen2.5's 4096-dim
        self.projection = nn.Sequential(
            nn.Linear(1152, 4096),
            nn.GELU(),
            nn.Linear(4096, 4096),
            nn.LayerNorm(4096)
        )
        # Only this bridge trains in Stage 1

    def encode_image(self, pixel_values):
        vision_out = self.vision_encoder(pixel_values)
        image_features = vision_out.last_hidden_state  # (B, 729, 1152)
        visual_tokens  = self.projection(image_features)# (B, 729, 4096)
        return visual_tokens

    def forward(self, pixel_values, input_ids, attention_mask, labels=None):
        visual_tokens  = self.encode_image(pixel_values)
        text_embeddings = self.language_model.model.embed_tokens(input_ids)
        combined = torch.cat([visual_tokens, text_embeddings], dim=1)
        return self.language_model(inputs_embeds=combined,
                                    attention_mask=attention_mask,
                                    labels=labels)

File: ark_vlm/train_stage1.py
  Data: LLaVA-CC3M-Pretrain-595K (image-caption pairs)
  Trains: ONLY projection bridge (SigLIP + Qwen2.5 frozen)
  Goal: bridge learns visual → language space mapping
  Time: ~3 days on RTX 5060 8GB

Start Stage 1 training today. It runs in background while you build other things.
```

**Day 87 — Fix SAGE Report LaTeX Issues**
```
Citation formatting, figure placement, section structure.
Test with real experiment results from Day 84–86 runs.
```

**Day 88 — ARK-VLM: Monitor Stage 1 Training**
```
Check training loss curve in MLflow.
Should decrease steadily.
If loss plateaus early → increase learning rate slightly.
No code changes — just monitoring.
```

**Day 89 — SAGE Loop: 2nd Hypothesis Run**
```
Run SAGE on a second hypothesis automatically.
Verify the loop repeats without manual intervention.
Checkpoint system saves each step.
```

**Day 90 — SAGE: Vision Agent Reads Experiment Plots**
```
After ML Engineer runs experiment:
  Vision Agent reads the matplotlib output figures
  Writes auto-generated captions for the LaTeX report
Test: Vision Agent describes a confusion matrix correctly.
```

**Day 91 — Sunday: Week 13 Review**
```
SAGE loop working end-to-end on 2 hypotheses.
ARK-VLM Stage 1 training: check loss curve.
If Stage 1 complete → start Stage 2 training.
```

---

### WEEK 14 — Self-Improvement + Capability Discovery + ARK-VLM Stage 2

---

**Day 92 — Self-Improvement Agent**
```
File: workers/phase3/self_improvement_agent.py
Model: groq:qwen2.5-72b
Month 6 implementation (Option A — prompt versioning):
  Read 50 Reflection logs for target agent
  Find failure patterns (quality_score < 0.65 runs)
  Generate improved system prompt via Claude Sonnet
  A/B test: run both prompts on 10 ARKBench tasks
  If new_score > old_score + 0.05: apply new prompt, bump version
  Use AgentVersioner to record the change
Done when: Self-Improvement correctly versions an agent after improvement.
```

**Day 93 — Capability Discovery Agent**
```
File: workers/phase3/capability_discovery_agent.py
Model: general tier
Reads: Agent Registry + Reflection logs + ARKBench results
Answers: "Can ARK OS do X?" with confidence 0.0–1.0
Updates live capability map: config/capabilities.json
Done when: Answers 10 "can it do X?" questions with correct confidence.
```

**Day 94 — ARK-VLM Stage 2 Setup**
```
Stage 1 projection training should be done by now.
Start Stage 2: train projection + Qwen2.5 LoRA adapters.

File: ark_vlm/train_stage2.py
Data: LLaVA-Instruct-150K + ARK OS scientific figure dataset
      (figures collected from SAGE runs in Months 4–5)

from peft import LoraConfig, get_peft_model
lora = LoraConfig(r=64, lora_alpha=128,
                  target_modules=["q_proj","k_proj","v_proj",
                                  "o_proj","gate_proj","up_proj","down_proj"])
model = get_peft_model(ark_vlm.language_model, lora)

Training time: ~4 days on RTX 5060 8GB
Start training today. Runs in background.
```

**Day 95 — Voice Interface**
```
pip install faster-whisper
File: core/voice_input.py
Pipeline: microphone → faster-whisper → text → ark CLI
Model: whisper-medium (CPU, no VRAM cost)
Test: speak "research quantum computing" → task submitted.
```

**Day 96 — All 20 Agents Integration Test**
```
Run one task touching all 20 agents.
Verify each activates at least once.
Goal: "Research solar cells, implement prediction model,
       scan for security, deploy as API, generate paper"
```

**Day 97 — ARKBench Month 5 Run**
```
All 50 tasks. Record scores with agent versions.
Compare to Month 2 baseline.
Target: measurable improvement in at least 3 categories.
```

**Day 98 — Sunday: Month 5 Review**
```
20 agents complete.
SAGE loop: 2+ complete runs.
Checkpoint system: fault-tolerant.
Task scheduling: autonomous operation verified.
ARK-VLM Stage 2: training in progress.
Voice: works.
Write: docs/month5_results.md
```

---

# MONTH 6 — INTEGRATION + EVALUATION + PAPERS + RELEASE
## Goal: Polish + ARK-VLM + MCP server + ARKBench final + Paper submissions

---

### WEEKS 15–16 (Days 99–112) — Polish + Evidence

---

**Day 99 — Self-Improvement First Full Run**
```
Run Self-Improvement Agent on all agents with 3+ months of data.
Target: at least 3 agents show >= 5% improvement.
All changes tracked via AgentVersioner.
```

**Day 100 — ARK-VLM: Evaluate + Integrate**
```
Stage 2 training should be complete.
Evaluate ARK-VLM on: VQAv2, ScienceQA, ChartQA, TextVQA
Compare to base Qwen2-VL-7B on same benchmarks.
If ARK-VLM wins on scientific figures → Paper #5 confirmed.

Convert to GGUF:
  python llama.cpp/convert_hf_to_gguf.py ark_vlm/ --outtype q4_k_m
  ollama create ark-vlm -f ark_vlm/Modelfile

Replace Vision Agent's Qwen2-VL-7B with ARK-VLM:
  Vision Agent now uses the model trained on ARK OS scientific data.
```

**Day 101 — Dashboard v1.0 Polish**
```
Add: SAGE Mode control panel (start/stop/resume loops)
Add: Self-Improvement tracker (score timeline per agent per version)
Add: ARKBench radar chart (all domain scores in one view)
Add: Scheduling panel (active schedules + next run times)
Add: Hardware tier badge (shows T2 etc from active-models.json)
Screenshot everything for paper figures.
```

**Day 102 — ARKBench Final: All 50 Tasks**
```
Definitive Month 6 run with all 20 agents + ARK-VLM.
Every result tagged with agent version.
Save: benchmarks/arkbench/results/month6_final.json
```

**Day 103 — Month 2 vs Month 6 Analysis**
```
Core claim: ARK OS self-improves over time.

Generate comparison table:
  Category | Month 2 | Month 6 | Delta | %Improvement | Agent versions

Plot: score timeline graph (Months 2,3,4,5,6) per category
      Annotate with agent version bump events

Write: docs/self_improvement_evidence.md
This is the core empirical contribution of Paper #1 and Paper #3.
```

**Day 104 — Complete Integration Test (6 hours)**
```
Goal: "Research solar cell materials, implement property prediction,
       scan for security, deploy as REST API, generate paper"
All 20 agents active. This is the flagship demo.
Record screen. This becomes the demo video.
```

---

### WEEKS 16–17 (Days 105–115) — Papers + MCP + Release

---

**Day 105 — Paper #1: Architecture**
```
Write: Abstract, Introduction, Related Work, Architecture, Evaluation
Include: ARKBench comparison, self-improvement timeline graph,
         version history table, privacy mode evaluation
Target: NeurIPS / ICLR
```

**Day 106 — Paper #1: Method Details**
```
AgentSpec v1.0 formal specification
Four-tier memory architecture formalization
Hardware-adaptive tier system (model-library.json)
llama.cpp inference layer performance comparison
LangGraph state machine diagram
```

**Day 107 — Paper #3: SAGE + Hypothesis Generation**
```
Introduction + Related Work (vs AI Scientist)
Novelty scoring formalization
Neo4j gap detection method
Materials Science case study results
Checkpoint system for reproducibility
```

**Day 108 — Paper #4: ARKBench**
```
Benchmark design, task categories, scoring rubrics
Baseline comparisons: AutoGPT, BabyAGI, MetaGPT on same tasks
Version-tagged results enabling reproducibility
Submit to arXiv as preprint first → immediate citable reference
```

---

**Day 109 — ARK OS as MCP Server**

```
MCP = Model Context Protocol (Anthropic standard)
This connects ARK OS to Claude Code, Cursor, VS Code — millions of users.

File: mcp/ark_mcp_server.py
pip install mcp

from mcp.server import Server
from mcp.server.models import InitializationOptions
import mcp.types as types

app = Server("ark-os")

@app.list_tools()
async def list_tools():
    return [
        types.Tool(
            name="ark_research",
            description="Research a topic using ARK OS Research Agent",
            inputSchema={"type":"object","properties":{
                "topic":{"type":"string"},
                "privacy":{"type":"boolean","default":False}
            },"required":["topic"]}
        ),
        types.Tool(
            name="ark_code",
            description="Write and test code using ARK OS Coding Agent",
            inputSchema={"type":"object","properties":{
                "instruction":{"type":"string"},
                "language":{"type":"string","default":"python"}
            },"required":["instruction"]}
        ),
        types.Tool(
            name="ark_security",
            description="Scan code for security vulnerabilities",
            inputSchema={"type":"object","properties":{
                "target_path":{"type":"string"}
            },"required":["target_path"]}
        ),
        types.Tool(
            name="ark_sage",
            description="Run SAGE scientific discovery mode",
            inputSchema={"type":"object","properties":{
                "domain":{"type":"string"},
                "topic":{"type":"string"}
            },"required":["domain","topic"]}
        ),
        types.Tool(
            name="ark_task",
            description="Submit any goal to ARK OS",
            inputSchema={"type":"object","properties":{
                "goal":{"type":"string"},
                "privacy":{"type":"boolean","default":False}
            },"required":["goal"]}
        )
    ]

@app.call_tool()
async def call_tool(name: str, arguments: dict):
    # Route to ARK OS FastAPI backend
    import httpx
    async with httpx.AsyncClient() as client:
        r = await client.post("http://localhost:8000/task",
                              json={"goal": arguments.get("goal",""),
                                    "intent": name,
                                    "privacy": arguments.get("privacy",False)})
        task_id = r.json()["task_id"]
        # Wait for completion and return result
        result = await poll_task(client, task_id)
    return [types.TextContent(type="text", text=str(result))]

MCP config for Claude Code (~/.claude/settings.json):
  {
    "mcpServers": {
      "ark-os": {
        "command": "python",
        "args": ["/path/to/ark-os/mcp/ark_mcp_server.py"]
      }
    }
  }

Users in Claude Code can now type:
  /ark_research "transformer attention mechanisms"
  /ark_code "write a FastAPI authentication endpoint"
  /ark_security "/home/user/myproject"
  /ark_sage "materials science" "solar cell efficiency"

Why this matters for distribution:
  Claude Code has millions of users.
  Zero installation friction — just add to settings.json.
  ARK OS becomes a tool inside Claude Code, Cursor, VS Code.

Done when: MCP server registered. Tools callable from Claude Code.
```

---

**Day 110 — Sunday: Week 15 Review**

**Day 111 — Public GitHub Release**
```
Make ark-os repository public.
Clean README with: one-command install, demo GIF, benchmark table.
Apache 2.0 LICENSE.
CONTRIBUTING.md + CODE_OF_CONDUCT.md
docker-compose up verified on clean machine.
```

**Day 112 — Paper #1 Submission**
```
Submit ARK OS Architecture paper to target venue.
```

**Day 113 — Demo Video**
```
Record 5-minute demo:
  1. Terminal: ark "research quantum computing, implement circuit simulator"
  2. Web dashboard: live agent grid, DAG animation, memory browser
  3. SAGE mode: hypothesis discovery in materials science
  4. Privacy mode: ark --privacy "confidential research topic"
  5. Month 2 vs Month 6 improvement graph
  6. Claude Code with ARK OS MCP: /ark_code "write auth endpoint"
Upload to YouTube. Link in README.
```

**Day 114 — Paper #4 arXiv Preprint + pip install ark-os**
```
Submit ARKBench preprint to arXiv → immediate citable reference.

Publish to PyPI:
  pip install ark-os

User flow:
  pip install ark-os
  ark init           → detects hardware tier, pulls correct models,
                       starts Docker services, sets API keys
  ark "first task"   → runs immediately

ark init flow:
  1. Run scripts/select-model.py → detect GPU tier
  2. Print: "Detected T2 hardware (8GB VRAM)"
  3. Pull recommended models via Ollama
  4. Start Docker services
  5. Prompt for API keys (optional — can skip for local-only)
  6. Run quick test task
  7. Print: "ARK OS ready. Try: ark 'research quantum computing'"
```

---

### WEEK 18 (Days 115–120) — Extras + Buffer

---

**Day 115 — Paper #5 Draft: ARK-VLM**
```
Write: ARK-VLM architecture, training stages, benchmark results.
"ARK-VLM: A Domain-Specialized Vision-Language Model
 for Scientific Discovery via Combined Encoder Architecture"
Submit as arXiv preprint.
```

**Day 116 — Paper #2: Conflict Resolution**
```
Write Paper #2 using conflict_log.json data.
4 conflict types, frequency analysis, resolution protocol, evaluation.
Target: AAMAS 2027.
```

---

**Day 117 — Obsidian / Notion Export**

```
Researchers keep notes in Obsidian or Notion.
ARK OS produces knowledge. Connect them so output is actually used.

File: tools/export_tools.py

Obsidian export (markdown files):
  export_to_obsidian(output_dir, obsidian_vault_path)
    → After each Research Agent run:
       Creates one .md file per paper in the vault
       [[wiki-links]] connect related papers automatically
       Front matter: arxiv_id, authors, year, tags
       Body: ARK OS summary + key findings

    → After each SAGE hypothesis run:
       Creates hypothesis.md with linked papers
       Experiment results as child notes
       Citation graph visible in Obsidian graph view

Notion export (via Notion API):
  export_to_notion(task_output, notion_db_id)
    → Each SAGE loop creates a Notion database entry
    → Hypothesis, experiment, results, status all in one page
    → Auto-updates as SAGE loop progresses

CLI:
  ark config set export.obsidian_vault ~/Documents/ResearchNotes
  ark config set export.notion_token your_token_here
  ark config set export.notion_db_id your_db_id_here
  → ARK OS auto-exports all research outputs to both targets

Config: config/export_config.json
  {
    "obsidian_vault": "/home/user/Documents/Research",
    "notion_enabled": false,
    "notion_token": "",
    "auto_export": true
  }

Done when: After a Research Agent run, notes appear in Obsidian vault.
           Paper connections shown in Obsidian graph view.
```

---

**Day 118 — 24/7 Deployment Guide**

```
Write the complete guide for running ARK OS when laptop is off.

File: docs/deployment_guide.md

OPTION A — Keep Laptop Running (Simplest)
  Power settings: never sleep when lid closed
  Cost: ~₹8-12/night electricity
  Suitable for: occasional overnight runs

OPTION B — Raspberry Pi as Always-On Brain (Smart, ₹6,000 one-time)
  Pi runs: Supervisor + Planner + Memory + Task Queue + FastAPI + Dashboard
  Heavy compute: dispatched to laptop when on, or cloud GPU on demand
  Power: 5W → ~₹30/month
  Setup guide included in docs/deployment_guide.md

  Pi install script: installers/raspberry_pi/install.sh
    Installs: Python 3.11, Redis, SQLite, Qdrant (ARM), FastAPI
    Skips: Ollama (no GPU on Pi) — uses API for all LLM calls
    Starts: supervisord to keep all services alive

OPTION C — Cloud GPU (Best for 24/7 SAGE, ~₹2,500/month)
  Recommended: Vast.ai RTX 3090 instance
  Docker Compose works identically on cloud as local
  SAGE loop runs continuously without touching laptop
  Access via terminal or web dashboard from anywhere

  Cloud deploy script: installers/cloud/deploy_vastai.sh
    Provisions instance, copies docker-compose.yml, starts services
    Sets up SSH tunnel for dashboard access

OPTION D — Home Mini PC (Best long-term)
  Beelink SER7 (AMD 780M iGPU, 32GB RAM) ~₹35,000 one-time
  Runs ARK OS 24/7, all agents local
  Access remotely via SSH or dashboard

ARK OS 24/7 Stack on any always-on machine:
  Process manager: supervisord (restarts services after crash)
  Log rotation: logrotate
  Monitoring: dashboard accessible from phone browser
  Scheduled SAGE: runs every night at 2 AM via task scheduler

Done when: Deployment guide covers all 4 options with step-by-step commands.
```

---

**Day 119 — Bug Fix + Documentation**
```
Fix any bugs reported from public GitHub.
Improve docstrings across all agents.
Write: ARCHITECTURE.md — one-page visual summary of all components.
Update README with ARK-VLM results + MCP setup instructions.
```

**Day 120 — Final Day**
```
Tag release: git tag v1.0.0
Write CHANGELOG.md covering all 120 days.
Post on Hacker News, Reddit r/MachineLearning, Twitter.
Email SRIHER faculty with GitHub link + demo video.
Submit Paper #3 (SAGE / hypothesis generation).

Done. ARK OS v1.0.0 is shipped.
```

---

# FULL SUMMARY TABLE

```
Month  Days    Agents  Key Deliverables
────────────────────────────────────────────────────────────────────────
1      1–28    6       Hardware auto-detection, Memory Agent,
                       Kernel, MVP loop, ARKBench baseline

2      29–42   8+      Agent versioning, Privacy mode,
                       Dashboard, ARKBench v0.1

3      43–56   11+     Browser, Terminal, GitHub agents,
                       llama.cpp inference (10–30% faster)

4      57–70   14+     All conflicts handled, ARKBench 50 tasks,
                       Paper #2 data collected

5      71–98   20      SAGE + checkpoints + task scheduling,
                       ARK-VLM training (Stage 1+2), Voice

6      99–120  20+VLM  Self-improvement evidence, MCP server,
                       Obsidian export, 24/7 deploy guide,
                       pip install ark-os, 3 papers submitted
```

---

# ALL FEATURES BUILT (Complete List)

```
Core System:
  ✅ 20 specialized agents
  ✅ 4-tier persistent memory (Redis + SQLite + Qdrant + FAISS)
  ✅ AgentSpec v1.0 frozen protocol
  ✅ LangGraph state machine orchestration
  ✅ llama.cpp inference (faster than Ollama alone)
  ✅ Redis Pub/Sub message bus
  ✅ 4 conflict resolution types
  ✅ Self-repair loop (Coding Agent)
  ✅ Evaluation rubrics (5 output types)
  ✅ ARKBench 50-task evaluation suite

New Additions (v2.0):
  ✅ Hardware auto-detection (T1–T6 tier system)
  ✅ model-library.json (all GPU configs)
  ✅ Windows PowerShell installer (tier-map.ps1)
  ✅ Agent versioning (semantic versions + history)
  ✅ Privacy mode (--privacy flag, zero API calls)
  ✅ Airgap mode (--airgap, zero internet)
  ✅ SAGE checkpoint system (fault-tolerant loops)
  ✅ Task scheduling (cron-style, file watchers)
  ✅ ARK-VLM (SigLIP + Qwen2.5 custom model)
  ✅ MCP server (Claude Code / Cursor / VS Code)
  ✅ Obsidian + Notion export
  ✅ 24/7 deployment guide (Pi + cloud + mini PC)
  ✅ pip install ark-os (PyPI package)
  ✅ Voice interface (faster-whisper, CPU-only)

Interfaces:
  ✅ Terminal CLI (ark command)
  ✅ Web dashboard (React, WebSocket)
  ✅ FastAPI REST backend
  ✅ MCP server (Claude Code integration)
  ✅ Voice input (faster-whisper)

Publications:
  📄 Paper #1  ARK OS Architecture         → NeurIPS / ICLR
  📄 Paper #2  Conflict Resolution          → AAMAS 2027
  📄 Paper #3  SAGE Hypothesis Generation   → Nature Machine Intelligence
  📄 Paper #4  ARKBench                     → arXiv preprint + ACL
  📄 Paper #5  ARK-VLM Custom Model         → arXiv preprint
```

---

# DAILY RULES

```
1.  One primary deliverable per day. Carry over if incomplete.
2.  Write tests alongside code — never after.
3.  Every new file needs a test file.
4.  Run full test suite every Friday.
5.  Memory Agent is sacred — break it, fix it before anything else.
6.  Commit to git every day.
    Format: "[Day NN] Component: what was built"
7.  Sunday = review + docs only. No new code.
8.  If 2+ days behind: drop lowest-priority item, move forward.
9.  ARKBench scores recorded every month with agent versions. Non-negotiable.
10. ARKMessage schema frozen after Day 4. Never change it.
11. Hardware tier loaded from active-models.json — never hardcode model names.
12. Privacy mode must be respected by EVERY agent, no exceptions.
13. Every SAGE step saves a checkpoint immediately after completion.
14. Agent versions bump when Self-Improvement makes any prompt change.
15. STATE.md is updated after every task — it must always reflect the current system state.
16. Use --until for any goal where success is measurable. Never let the model self-grade its own completion.
```

---

*ARK OS — Day-by-Day Build Plan v2.0*
*Kabilash (E0323036) · SRIHER Chennai · 2026*
*6 months · 20 agents + ARK-VLM · 120 build days*
*Updated: hardware detection, llama.cpp, versioning, privacy, checkpoints,*
*scheduling, ARK-VLM, MCP server, Obsidian export, 24/7 deploy*
