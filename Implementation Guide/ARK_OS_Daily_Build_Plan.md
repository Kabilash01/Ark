# ARK OS — Day-by-Day Build Plan
**6 Months · 20 Agents · Every Single Day Planned**

**Rules:**
- Mon–Sat: Build days (active coding)
- Sunday: Review, document, plan next week
- Each day has ONE primary deliverable
- Never skip the Memory Agent rule: it goes first, always
- If a day runs over → carry the task, do not skip it

---

# MONTH 1 — FOUNDATION
## Goal: Kernel scaffold + Memory Agent + Supervisor + Planner + Research + Coding + Evaluation

---

### WEEK 1 — Environment + Schema + BaseAgent

---

**Day 1 — Monday**
**Task: Environment Setup**

```
Install everything. Verify everything works. Nothing else.

Steps:
  1. Install Python 3.11
       sudo apt install python3.11 python3.11-venv python3.11-dev
  2. Create virtualenv
       python3.11 -m venv ark-env
       source ark-env/bin/activate
  3. Install Ollama
       curl -fsSL https://ollama.com/install.sh | sh
  4. Pull all three local models (will take time — start early)
       ollama pull gemma4:26b-instruct-qat
       ollama pull qwen2.5-coder:7b
       ollama pull qwen2-vl:7b
  5. Install Docker + Docker Compose
       sudo apt install docker.io docker-compose-plugin
  6. Set environment variables in ~/.bashrc
       export ANTHROPIC_API_KEY=your_key_here
       export GROQ_API_KEY=your_key_here
  7. Test Ollama is working
       ollama run gemma4:26b-instruct-qat "say hello"

Done when: All models respond. Docker runs. Python 3.11 is active.
```

---

**Day 2 — Tuesday**
**Task: Repository Scaffold**

```
Create the full folder structure. Initialize git. Write pyproject.toml.

Steps:
  1. Create repo
       mkdir ark-os && cd ark-os
       git init

  2. Create full folder structure
       mkdir -p core memory kernel workers/phase1 workers/phase2 workers/phase3
       mkdir -p sage tools api dashboard benchmarks/arkbench tests docs

  3. Create empty placeholder files
       touch core/__init__.py
       touch core/ark_message.py
       touch core/base_agent.py
       touch core/message_bus.py
       touch core/agent_registry.py
       touch memory/__init__.py
       touch memory/memory_agent.py
       touch memory/working_memory.py
       touch memory/episodic_memory.py
       touch memory/semantic_memory.py
       touch memory/procedural_memory.py
       touch kernel/__init__.py
       touch kernel/supervisor_agent.py
       touch kernel/planner_agent.py
       touch kernel/evaluation_agent.py
       touch workers/phase1/research_agent.py
       touch workers/phase1/coding_agent.py
       touch api/main.py
       touch docker-compose.yml
       touch README.md

  4. Write pyproject.toml with Python 3.11 pin
  5. First git commit: "initial scaffold"

Files created:
  ark-os/ (full tree as above)

Done when: git status shows clean tree. Folder structure matches §24 of v1.1.
```

---

**Day 3 — Wednesday**
**Task: Docker Compose — All Services Up**

```
Write docker-compose.yml. Start all services. Verify they are reachable.

File: docker-compose.yml

Services to configure:
  qdrant:      image: qdrant/qdrant:latest     port: 6333
  redis:       image: redis:7-alpine           port: 6379
  neo4j:       image: neo4j:5.15-community     port: 7474 + 7687
  mlflow:      image: ghcr.io/mlflow/mlflow    port: 5000
  (ark-api and ark-ui come later — leave as stubs)

Steps:
  1. Write docker-compose.yml with all 4 services
  2. docker compose up -d
  3. Test each service:
       curl http://localhost:6333/healthz          # Qdrant
       redis-cli ping                              # Redis → PONG
       curl http://localhost:7474                  # Neo4j browser
       curl http://localhost:5000/health           # MLflow

Done when: All 4 services healthy. docker compose ps shows all running.
```

---

**Day 4 — Thursday**
**Task: ARKMessage Schema — AgentSpec v1.0 FROZEN**

```
Write the ARKMessage TypedDict. This is frozen after today. Never change it.

File: core/ark_message.py

What to write:
  - ARKMessage TypedDict with ALL fields from v1.1 answers
  - validate_ark_message() function
  - create_ark_message() factory function
  - Intent enum (RESEARCH, CODE, SEARCH, EXECUTE, STORE, EVALUATE, REFLECT, DISCOVER)
  - Status enum (PENDING, RUNNING, DONE, FAILED, RETRY)

Fields (from Q4 answers):
  task_id, sender, receiver, parent_task_id,
  intent, payload, memory_context,
  priority, status, retry_count, max_retries,
  output, quality_score, timestamp

Tests to write: tests/test_ark_message.py
  - Create a valid ARKMessage
  - Validate missing required fields fail
  - Factory function produces correct defaults

Done when: All tests pass. Schema is committed and tagged v1.0-agentspec.
```

---

**Day 5 — Friday**
**Task: BaseAgent Abstract Class**

```
Write the BaseAgent class. All 20 agents will inherit this.

File: core/base_agent.py

Methods to implement (from Q5 answers):
  Abstract (must implement in every agent):
    run(message: ARKMessage) -> ARKMessage
    get_capabilities() -> list[str]

  Default implementations (can override):
    register(registry) -> None
    health_check() -> dict
    validate_message(message: ARKMessage) -> bool
    on_error(message, exception) -> ARKMessage

Also write:
  - __init__ that takes agent_name and model_name
  - _run_with_error_handling() wrapper that calls validate → run → on_error
  - structured logging on every run (task_id, agent, timestamp, score)

Tests: tests/test_base_agent.py
  - Create a dummy TestAgent that extends BaseAgent
  - Test all 5 methods
  - Test that abstract methods cannot be skipped

Done when: All tests pass. BaseAgent is clean, documented, importable.
```

---

**Day 6 — Saturday**
**Task: Agent Registry**

```
Write the Agent Registry service. Supervisor uses this to route tasks.

File: core/agent_registry.py

AgentRecord fields (from Q6 answers):
  name, capabilities, model, status, current_task,
  last_heartbeat, load_count, avg_quality_score

Methods:
  register(agent_record)             → add agent to registry
  deregister(agent_name)             → remove agent
  get_agent(agent_name)              → return record
  find_agents_for_intent(intent)     → list of capable agents
  get_least_loaded(agents)           → agent with lowest load_count
  update_status(agent_name, status)  → update IDLE/BUSY/ERROR
  update_quality(agent_name, score)  → rolling average of quality_score
  heartbeat(agent_name)              → update last_heartbeat timestamp
  get_all()                          → full registry dump

Storage: Redis hash (agent:{name} → JSON record)
         So registry survives process restart

Tests: tests/test_agent_registry.py

Done when: All tests pass. Registry stores and retrieves from Redis correctly.
```

---

**Day 7 — Sunday**
**Task: Week 1 Review**

```
No new code. Review everything built this week.

Checklist:
  □ All Docker services still healthy?
  □ ARKMessage schema reviewed — is it complete?
  □ BaseAgent reviewed — is it clean?
  □ Agent Registry reviewed — does it use Redis correctly?
  □ All tests passing?
  □ README.md updated with Week 1 progress?

Write in your notebook (not code):
  - What was harder than expected?
  - What would you change in the design?
  - Is there anything that feels wrong about the schema?

This is the LAST chance to change ARKMessage. After Week 2 begins,
AgentSpec v1.0 is permanently frozen.
```

---

### WEEK 2 — Memory Agent (Most Important Week)

---

**Day 8 — Monday**
**Task: Redis Working Memory Tier**

```
Build the first memory tier. Working memory = current task state.

File: memory/working_memory.py

What it stores:
  Current task context, active variables, temp results
  Duration: only for the life of one task (TTL-based)

Methods:
  set(task_id, key, value, ttl=3600)  → store with expiry
  get(task_id, key)                   → retrieve
  get_all(task_id)                    → all context for a task
  delete(task_id, key)                → remove one key
  clear_task(task_id)                 → remove all keys for a task
  exists(task_id, key)                → check existence

Implementation:
  Redis key pattern: "working:{task_id}:{key}"
  All ops use async redis (redis.asyncio)

Tests: tests/memory/test_working_memory.py
  - Store and retrieve across types (str, dict, list)
  - TTL expiry works correctly
  - clear_task removes all keys for that task only

Done when: All tests pass. Redis stores and retrieves correctly.
```

---

**Day 9 — Tuesday**
**Task: SQLite Episodic Memory Tier**

```
Build the episodic memory tier. Permanent task history log.

File: memory/episodic_memory.py

What it stores:
  What happened in past runs — timestamped task records

Schema (SQLite table: episodes):
  id           INTEGER PRIMARY KEY
  task_id      TEXT
  agent_name   TEXT
  intent       TEXT
  summary      TEXT    (what happened, in plain text)
  quality_score REAL
  timestamp    TEXT    (ISO 8601)
  metadata     TEXT    (JSON blob for extras)

Methods:
  log(task_id, agent_name, intent, summary, quality_score, metadata)
  query_by_agent(agent_name, limit=10)
  query_by_intent(intent, limit=10)
  query_by_timerange(start_iso, end_iso)
  query_recent(limit=20)
  search(keyword)     → LIKE search on summary field

Tests: tests/memory/test_episodic_memory.py
  - Log 5 episodes, retrieve by agent, intent, time
  - Keyword search returns correct rows
  - Persists across connection close/reopen (true persistence test)

Done when: All tests pass. SQLite file written to disk and readable.
```

---

**Day 10 — Wednesday**
**Task: Qdrant Semantic Memory Tier**

```
Build the semantic memory tier. Vector knowledge base.

File: memory/semantic_memory.py

Embedding model: BAAI/bge-large-en-v1.5 (1024-dim) — from Q8 answer
Qdrant collection: ark_semantic_memory

Methods:
  store(text, metadata)           → embed text, upsert to Qdrant
  retrieve(query, top_k=5)        → embed query, return top-k chunks
  retrieve_by_metadata(filters)   → filter by source, agent, date
  delete(point_id)                → remove a specific vector
  count()                         → how many vectors stored

Also write:
  _embed(text) → np.ndarray    using sentence-transformers
  EmbeddingModel singleton      load once at startup, reuse

Tests: tests/memory/test_semantic_memory.py
  - Store 5 text chunks about different topics
  - Query returns most similar chunk correctly
  - Metadata filtering works

Note: First run will download BAAI model (~670MB). This is fine — runs once.

Done when: All tests pass. Qdrant has vectors. Similarity retrieval works.
```

---

**Day 11 — Thursday**
**Task: Procedural Memory Tier (JSON + FAISS)**

```
Build the procedural memory tier. How-to skill library.

File: memory/procedural_memory.py

Skill schema (from Q11 answer):
  skill_id, intent, description, steps, success_rate, use_count, last_used, source

Storage:
  skills.json          → list of all Skill records (human readable)
  skills.faiss         → FAISS index on description embeddings

Methods:
  add_skill(skill)             → add to JSON + rebuild FAISS index
  retrieve_skills(intent, query, top_k=3)
                               → FAISS search by description × success_rate
  update_success_rate(skill_id, was_success)
                               → rolling average update
  increment_use_count(skill_id)
  delete_skill(skill_id)
  get_all_skills()

Tests: tests/memory/test_procedural_memory.py
  - Add 3 skills for different intents
  - Retrieve skills by intent + query, get correct top-1
  - Success rate updates correctly

Done when: All tests pass. Skills saved to disk, FAISS index queryable.
```

---

**Day 12 — Friday**
**Task: Memory Agent — Unified Interface**

```
Build the Memory Agent that wraps all 4 tiers into one interface.
All other agents will ONLY talk to this — never to the tiers directly.

File: memory/memory_agent.py

Extends: BaseAgent

Methods (the Memory Agent API):
  store(memory_type, key, value, metadata)
    → routes to correct tier based on memory_type
    → memory_type: "working" | "episodic" | "semantic" | "procedural"

  retrieve(query, memory_type, top_k=5)
    → retrieves from correct tier
    → for semantic/procedural: vector search
    → for episodic: keyword + time search
    → for working: exact key lookup

  forget(memory_type, key_or_policy)
    → deletes from correct tier
    → policy: "ttl_expired" | "low_relevance" | specific key

  inject_context(task: ARKMessage) -> ARKMessage
    → given a task, find all relevant memories across all tiers
    → inject them into task["memory_context"]
    → returns enriched ARKMessage

  run(message: ARKMessage) -> ARKMessage
    → handles STORE / RETRIEVE / FORGET / INJECT intents

Tests: tests/memory/test_memory_agent.py
  - Store to each tier via Memory Agent
  - Retrieve from each tier via Memory Agent
  - inject_context populates memory_context correctly
  - Persists across process restart (critical test)

Done when: ALL persistence tests pass.
           Memory survives: store → kill process → restart → retrieve ✓
```

---

**Day 13 — Saturday**
**Task: Redis Message Bus**

```
Build the message bus. Agents talk to each other through this.

File: core/message_bus.py

Channel topology (from Q10 answer):
  "agent:{agent_name}"    → each agent's inbox
  "ark:broadcast"         → system-wide messages
  "ark:heartbeat"         → health pings
  "ark:audit_log"         → all messages logged here

Class: ARKMessageBus

Methods:
  publish(receiver_name, message: ARKMessage)   → send to agent's channel
  subscribe(agent_name) → AsyncGenerator        → listen on own channel
  broadcast(message)                            → send to ark:broadcast
  publish_heartbeat(agent_name, health: dict)   → ping heartbeat channel
  log_to_audit(message: ARKMessage)             → write to audit_log
  get_audit_log(limit=50)                       → read recent messages

Implementation: redis.asyncio pub/sub

Tests: tests/test_message_bus.py
  - Publish message to agent channel, subscribe receives it
  - Audit log captures every message
  - Broadcast reaches all subscribers

Done when: Two agents can communicate through Redis pub/sub correctly.
```

---

**Day 14 — Sunday**
**Task: Week 2 Review — Memory System Audit**

```
This is the most important review of the entire project.
Memory is the foundation. It must be perfect before anything else is built.

Full audit checklist:
  □ Working memory: stores and retrieves with TTL — tested
  □ Episodic memory: persists to SQLite, queryable by time/agent/intent — tested
  □ Semantic memory: BAAI embeddings in Qdrant, similarity search works — tested
  □ Procedural memory: FAISS index, success_rate weighting works — tested
  □ Memory Agent: all 4 tiers accessible via one interface — tested
  □ inject_context: correctly enriches ARKMessages — tested
  □ Persistence: restart test passed for all 4 tiers

Write memory architecture document: docs/memory_architecture.md
  - How each tier works
  - When to use which tier
  - Code examples for every operation

Only proceed to Week 3 if every tier is tested and working.
```

---

### WEEK 3 — Supervisor + Planner

---

**Day 15 — Monday**
**Task: LangGraph State Machine Setup**

```
Set up LangGraph. Define the ARKState that flows between agents.

File: kernel/ark_state.py

ARKState TypedDict:
  task_id:       str
  original_goal: str
  current_plan:  list[dict]       # task DAG from Planner
  results:       dict             # accumulated results by task_id
  active_agents: list[str]        # who is currently running
  memory_context: list           # injected by Memory Agent
  iteration:     int             # retry counter
  status:        str             # RUNNING | DONE | FAILED

Also write: kernel/ark_graph.py
  - StateGraph(ARKState) initialization
  - Add all agent nodes (stubs for now)
  - Set entry point: supervisor_agent

Tests: tests/kernel/test_ark_state.py
  - ARKState initializes correctly
  - StateGraph compiles without errors

Done when: LangGraph graph compiles. State flows through a stub node.
```

---

**Day 16 — Tuesday**
**Task: Supervisor Agent — Intent Classification**

```
Build the Supervisor. This is the CEO of ARK OS.

File: kernel/supervisor_agent.py

Extends: BaseAgent
Model: claude-sonnet-4-6 (primary) → groq:qwen2.5-72b (fallback)
Capabilities: ["SUPERVISE", "ROUTE", "ARBITRATE"]

What Supervisor does:
  1. Receives user goal as plain text
  2. Classifies intent (RESEARCH / CODE / SEARCH / SECURITY / SAGE / MIXED)
  3. Sets priority (1-10) based on keywords
  4. Checks Agent Registry for available agents
  5. Routes to Planner with enriched ARKMessage
  6. Handles escalations when agents fail max_retries

System prompt:
  You are the Supervisor of ARK OS. You receive user goals and:
  1. Classify the intent type
  2. Assess priority 1-10
  3. Identify which domains are needed
  4. Route to Planner with complete context
  Output ONLY valid JSON matching ARKMessage schema.

Tests: tests/kernel/test_supervisor.py
  - "Research X" → intent = RESEARCH
  - "Write code for Y" → intent = CODE
  - "Research X and implement it" → intent = MIXED
  - Priority scoring is consistent

Done when: Supervisor correctly classifies 10 different goal types.
```

---

**Day 17 — Wednesday**
**Task: Planner Agent — Task DAG Generation**

```
Build the Planner. This is the Project Manager of ARK OS.

File: kernel/planner_agent.py

Extends: BaseAgent
Model: claude-sonnet-4-6 (primary) → groq:qwen2.5-72b (fallback)
Capabilities: ["PLAN", "DECOMPOSE", "SCHEDULE"]

What Planner does:
  1. Receives enriched ARKMessage from Supervisor
  2. Decomposes goal into subtasks
  3. Identifies dependencies between subtasks (what must run before what)
  4. Identifies which subtasks can run in parallel
  5. Produces a task DAG as JSON
  6. Assigns each subtask to an agent capability type

System prompt:
  You are the Planner of ARK OS. Given a goal:
  1. Break it into atomic subtasks (each takes 1-10 minutes)
  2. Identify dependencies: task B requires output of task A
  3. Mark parallel tasks: these can run simultaneously
  4. Assign each task to: RESEARCH | CODE | SEARCH | SECURITY | MEMORY
  Output ONLY valid JSON task DAG.

Task DAG format:
  {
    "tasks": [
      {
        "task_id": "t001",
        "intent": "RESEARCH",
        "instruction": "Find 10 papers on X",
        "depends_on": [],
        "parallel_with": ["t002"]
      },
      ...
    ]
  }

Tests: tests/kernel/test_planner.py
  - Simple goal → 2-step sequential DAG
  - Complex goal → parallel tasks correctly identified
  - Dependencies are acyclic (no cycles)

Done when: Planner produces valid DAGs for 5 different goal types.
```

---

**Day 18 — Thursday**
**Task: Supervisor ↔ Planner Integration**

```
Wire Supervisor and Planner together through the message bus.
First real inter-agent communication.

File: kernel/ark_graph.py (update)

Steps:
  1. Supervisor publishes to message bus → Planner receives
  2. Planner processes → publishes task DAG back to Supervisor
  3. Supervisor stores task DAG in ARKState
  4. Memory Agent's inject_context called before every routing step

Integration test: tests/integration/test_supervisor_planner.py
  Input:  "Research transformer attention and implement a demo"
  Expected flow:
    Supervisor: classifies MIXED intent, priority 7
    Memory: injects prior context (empty at first)
    Planner: produces DAG with research → coding dependency
  Output: valid task DAG in ARKState

Done when: Full Supervisor → Planner flow runs end-to-end.
           Task DAG is stored correctly in Redis.
```

---

**Day 19 — Friday**
**Task: Planner → Worker Routing Logic**

```
Build the routing logic that sends subtasks to the right agents.

File: kernel/router.py

What it does:
  1. Takes task DAG from Planner
  2. For each task in DAG:
     - Find capable agents in Agent Registry
     - Pick least-loaded agent (load_count)
     - Publish task to that agent's Redis channel
  3. Handles parallel dispatch (multiple tasks at once)
  4. Tracks which tasks are complete, which are pending
  5. When all dependencies of task T are done → dispatch T

Key functions:
  dispatch_dag(dag, state: ARKState)
  dispatch_task(task, agent_name)
  on_task_complete(task_id, result)    → check if dependencies unblocked
  get_execution_order(dag)             → topological sort

Tests: tests/kernel/test_router.py
  - Simple 3-task DAG dispatched in correct order
  - Parallel tasks dispatched simultaneously
  - Dependency respected: task B not dispatched until task A done

Done when: Router correctly topologically sorts and dispatches a 4-task DAG.
```

---

**Day 20 — Saturday**
**Task: Heartbeat Monitor + Agent Health**

```
Build the heartbeat system. Supervisor needs to know if agents are alive.

File: kernel/heartbeat_monitor.py

What it does:
  - Every 30 seconds, all agents publish to ark:heartbeat channel
  - Monitor listens and updates Agent Registry
  - If agent misses 3 heartbeats → mark OFFLINE
  - Supervisor skips OFFLINE agents when routing

HeartbeatMonitor:
  start()                    → begin listening to ark:heartbeat
  on_heartbeat(agent, data)  → update registry last_heartbeat
  check_dead_agents()        → find agents with old heartbeat
  revive_attempt(agent_name) → try to restart dead agent

Tests: tests/kernel/test_heartbeat.py
  - Agent publishes heartbeat → registry updated
  - Missed heartbeats → agent marked OFFLINE
  - Routing skips OFFLINE agents

Done when: Dead agents are automatically detected and skipped.
```

---

**Day 21 — Sunday**
**Task: Week 3 Review**

```
Review all kernel components.

Checklist:
  □ LangGraph state machine compiles
  □ Supervisor classifies intents correctly
  □ Planner produces valid DAGs
  □ Supervisor ↔ Planner communication through Redis
  □ Router dispatches tasks in correct dependency order
  □ Heartbeat monitor detects dead agents

End-to-end trace:
  Run this manually and verify every step in logs:
  "Research quantum computing"
  → Supervisor → Planner → DAG → Router → (Research stub)

Write: docs/kernel_architecture.md
```

---

### WEEK 4 — Research Agent + Coding Agent

---

**Day 22 — Monday**
**Task: arXiv API Tool**

```
Build the arXiv search tool. Research Agent uses this.

File: tools/research_tools.py

Functions:
  search_arxiv(query, max_results=10, date_from=None, date_to=None)
    → returns list of {title, authors, abstract, arxiv_id, url, published}

  fetch_arxiv_paper(arxiv_id)
    → returns full paper text (abstract + sections if available)

  search_semantic_scholar(query, limit=10)
    → uses Semantic Scholar API (free, no key needed)

  get_paper_citations(arxiv_id)
    → returns list of papers that cite this one (via Semantic Scholar)

Tests: tests/tools/test_research_tools.py
  - Search returns correct number of results
  - Fetch returns paper content
  - Results have all required fields

Done when: Can search arXiv and get full paper details programmatically.
```

---

**Day 23 — Tuesday**
**Task: Research Agent — Core**

```
Build the Research Agent. This reads papers and builds knowledge.

File: workers/phase1/research_agent.py

Extends: BaseAgent
Model: gemma4:26b-instruct-qat (local)
Capabilities: ["RESEARCH", "LITERATURE_REVIEW", "HYPOTHESIS_SEARCH"]

What it does:
  1. Receives RESEARCH task with topic/query
  2. Searches arXiv + Semantic Scholar
  3. Reads top papers (abstract + key sections)
  4. Embeds each paper → stores in Qdrant via Memory Agent
  5. Runs RAG over stored papers
  6. Produces structured research summary

System prompt:
  You are the ARK OS Research Agent. Your job is to:
  1. Find the most relevant papers on the given topic
  2. Extract key findings, methods, and gaps
  3. Synthesize into a structured summary
  4. Identify what is NOT yet known (research gaps)
  Always cite papers with arxiv IDs.

RAG pipeline inside Research Agent:
  query → search_arxiv → fetch papers → embed → store in Qdrant
        → retrieve top-k from Qdrant → synthesize with LLM → output

Tests: tests/workers/test_research_agent.py
  - Given "transformer attention efficiency":
    → finds at least 5 papers
    → stores them in semantic memory
    → produces structured summary with citations
    → identifies at least 1 research gap

Done when: Research Agent produces a quality summary with citations.
```

---

**Day 24 — Wednesday**
**Task: Code Execution Tool + Sandbox**

```
Build safe code execution. Critical for Coding Agent.

File: tools/code_tools.py

IMPORTANT: All code runs inside Docker container — never on host directly.

Functions:
  run_python_code(code: str, timeout=30) -> dict
    → {stdout, stderr, exit_code, execution_time}
    → runs in isolated Docker container (ark-sandbox)
    → kills container after timeout

  install_package(package_name: str) -> dict
    → pip install inside sandbox container
    → returns {success, output, error}

  write_file(path: str, content: str) -> bool
    → write to sandboxed filesystem only

  read_file(path: str) -> str
    → read from sandboxed filesystem

  run_tests(test_file_path: str) -> dict
    → {passed, failed, errors, coverage}

Docker sandbox image: ark-sandbox
  FROM python:3.11-slim
  RUN pip install numpy pandas matplotlib scikit-learn torch
  (pre-installed common packages)

Tests: tests/tools/test_code_tools.py
  - Run simple Python code → get output
  - Run code with error → get stderr cleanly
  - Timeout kills runaway code
  - Cannot access host filesystem

Done when: Code runs in Docker, timeouts work, output captured cleanly.
```

---

**Day 25 — Thursday**
**Task: Coding Agent — Core**

```
Build the Coding Agent. This writes, tests, and fixes code.

File: workers/phase1/coding_agent.py

Extends: BaseAgent
Model: qwen2.5-coder:7b (local)
Capabilities: ["CODE_GENERATION", "DEBUG", "CODE_REVIEW", "TEST_WRITING"]

What it does:
  1. Receives CODE_GENERATION task with specification
  2. Checks Memory Agent for relevant prior code patterns (procedural)
  3. Writes implementation code
  4. Writes unit tests
  5. Runs tests via code_tools.run_python_code()
  6. If tests fail → reads error → fixes → reruns (self-repair loop)
  7. Max 3 self-repair attempts before returning FAILED

Self-repair loop (the most important part):
  write code
    → run tests
    → if pass: return DONE
    → if fail:
        read error
        diagnose with LLM
        write fix
        run tests again
        (max 3 attempts)
    → if still failing: return FAILED with diagnosis

Tests: tests/workers/test_coding_agent.py
  - "Write a function to calculate Fibonacci numbers"
    → produces working code
    → tests pass
  - Inject a syntax error in prompt
    → agent detects and fixes
  - "Calculate RSI from price data" (finance task from memory docs)
    → working RSI implementation

Done when: Coding Agent self-repairs at least 1 injected bug correctly.
```

---

**Day 26 — Friday**
**Task: Evaluation Agent**

```
Build the Evaluation Agent. Scores everything 0.0–1.0.

File: kernel/evaluation_agent.py

Extends: BaseAgent
Model: claude-haiku-4-5 (fastest, cheapest for scoring)
Capabilities: ["EVALUATE", "SCORE", "QUALITY_CHECK"]

Rubrics (from Q7 answers):
  Research:      citation accuracy(40) + coverage(30) + clarity(30)
  Code:          tests pass(50) + no syntax errors(20) + style(15) + security(15)
  Security:      CVE recall(50) + false positive rate(30) + completeness(20)
  Hypothesis:    novelty(50) + feasibility(30) + clarity(20)
  Data analysis: statistical validity(50) + visualization(25) + interpretation(25)

Thresholds:
  RETRY_THRESHOLD  = 0.65   (below → send back for retry)
  ACCEPT_THRESHOLD = 0.80   (above → accepted)
  WARN_RANGE = 0.65–0.80    (accept with warning to Reflection Agent)

System prompt per rubric type:
  You are the ARK OS Evaluation Agent. Score this {output_type} output
  on the following criteria: {rubric_criteria}
  Return ONLY a JSON object: {"score": float, "breakdown": dict, "feedback": str}

run() method:
  1. Detect output type from message.intent
  2. Select correct rubric
  3. Call Claude Haiku with rubric prompt
  4. Parse score
  5. Set message.quality_score
  6. If score < RETRY_THRESHOLD → set status = RETRY
  7. If score >= ACCEPT_THRESHOLD → set status = DONE

Tests: tests/kernel/test_evaluation_agent.py
  - Good research output → score >= 0.80
  - Bad research output → score < 0.65
  - Working code → score >= 0.80
  - Broken code → score < 0.65

Done when: Evaluation correctly scores 6 different output types.
```

---

**Day 27 — Saturday**
**Task: Full MVP Loop Integration Test**

```
Wire everything together. Run the first complete end-to-end task.

Test: tests/integration/test_mvp_loop.py

Goal: "Research Fibonacci algorithms and implement the most efficient one"

Expected full flow:
  User → Supervisor (classify: MIXED, priority 6)
       → Memory Agent (inject_context: empty on first run)
       → Planner (DAG: research → coding)
       → Router (dispatch research task)
       → Research Agent (finds papers, stores in Qdrant)
       → Router (research done, dispatch coding task)
       → Coding Agent (implements, tests pass)
       → Evaluation Agent (scores code: expect >= 0.75)
       → Memory Agent (stores outcome to episodic + semantic)
  Output: working Fibonacci implementation + research summary

Debug every step. Log every message. Fix every failure.

Done when: Full loop completes without manual intervention.
           Evaluation score >= 0.70 on first run.
```

---

**Day 28 — Sunday**
**Task: Month 1 Review + Architecture Write-Up**

```
Month 1 is complete. Full audit and documentation.

Code review checklist:
  □ ARKMessage schema: all fields present, TypedDict correct
  □ BaseAgent: all 5 methods, error handling clean
  □ Agent Registry: 8 fields, Redis persistence working
  □ Working memory: TTL, key patterns correct
  □ Episodic memory: SQLite persists across restarts
  □ Semantic memory: BAAI embeddings in Qdrant
  □ Procedural memory: FAISS index working
  □ Memory Agent: all tiers accessible via one interface
  □ Message Bus: Redis Pub/Sub routing works
  □ Supervisor: intent classification correct
  □ Planner: DAG generation valid and acyclic
  □ Router: topological dispatch working
  □ Research Agent: finds papers, stores in Qdrant
  □ Coding Agent: self-repair loop working
  □ Evaluation Agent: scores with correct rubric

Write: docs/month1_results.md
  - What was built
  - Test pass rates
  - Performance numbers (latency per agent)
  - What was harder than expected
  - What to improve in Month 2

Start draft of Paper #1 introduction.
```

---

# MONTH 2 — COMPLETE THE MVP LOOP
## Goal: Harden the 6-agent loop + Web Search + File System + Dashboard + ARKBench v0.1

---

### WEEK 5 — Harden Core Loop

---

**Day 29 — Monday**
**Task: Retry Logic + Escalation**

```
Build proper retry handling in the Supervisor.

File: kernel/supervisor_agent.py (update)

Retry flow:
  Evaluation returns RETRY
    → Supervisor increments retry_count
    → if retry_count < max_retries (3):
        re-dispatch to same agent with feedback
    → if retry_count >= max_retries:
        escalate (mark task FAILED, notify user)

Also build: EscalationHandler
  - Logs escalation to episodic memory
  - Notifies dashboard via WebSocket
  - Creates detailed failure report

Tests:
  - Inject a task that always scores < 0.65
  - Verify: retried 3 times, then marked FAILED
  - Escalation report generated

Done when: 3-retry loop works correctly with escalation.
```

---

**Day 30 — Tuesday**
**Task: Conflict Resolution — Type 3 (Priority)**

```
Build priority conflict resolution (first and simplest type).

File: kernel/conflict_resolver.py

Type 3: Priority conflict
  When 2 tasks arrive simultaneously with equal priority:
  Resolution: FIFO by timestamp, then round-robin by domain

ConflictResolver class:
  detect_priority_conflict(task_queue) -> bool
  resolve_priority(task_a, task_b) -> task    (which goes first)
  log_conflict(conflict_type, resolution)     (to episodic memory)

Resolution rules:
  1. Higher priority wins
  2. If equal priority: earlier timestamp wins
  3. If same timestamp: round-robin (RESEARCH → CODE → SECURITY → repeat)

Tests:
  - Two RESEARCH tasks, different priorities → higher wins
  - Two RESEARCH tasks, same priority, different timestamps → earlier wins
  - Domain round-robin is correct

Done when: Priority conflicts resolved correctly in all test cases.
```

---

**Day 31 — Wednesday**
**Task: Reflection Agent**

```
Build the Reflection Agent. Learns from every completed task.

File: workers/phase2/reflection_agent.py

Extends: BaseAgent
Model: groq:qwen2.5-72b (needs strong reasoning for analysis)
Capabilities: ["REFLECT", "ANALYZE", "STRATEGY_UPDATE"]

What it does:
  After every task completes (pass OR fail):
  1. Reads task history from episodic memory
  2. Analyzes: what worked? what failed? why?
  3. Extracts patterns: "When X happens, doing Y leads to score > 0.80"
  4. Updates procedural memory with new/updated skills
  5. Flags patterns for Self-Improvement Agent (Month 6)

Reflection output format:
  {
    "task_id": "...",
    "what_worked": [...],
    "what_failed": [...],
    "pattern_learned": "...",
    "skill_update": {skill record},
    "flag_for_self_improvement": bool
  }

System prompt:
  You are the ARK OS Reflection Agent. After each task, analyze the
  execution log and extract learnable patterns. Be specific and actionable.

Tests:
  - After a successful coding task: identifies what made it succeed
  - After a failed research task: identifies root cause
  - Skill is updated in procedural memory

Done when: Reflection runs automatically after every task completion.
```

---

**Day 32 — Thursday**
**Task: Web Search Agent**

```
Build the Web Search Agent. Grounds knowledge in current web content.

File: workers/phase2/web_search_agent.py

Extends: BaseAgent
Model: gemma4:26b-instruct-qat (local)
Capabilities: ["WEB_SEARCH", "CURRENT_EVENTS", "URL_FETCH"]

Tools to build: tools/web_tools.py
  search_web(query, num_results=5)
    → Use DuckDuckGo API (free, no key) or Tavily (key needed)
    → Returns: [{title, url, snippet, date}]

  fetch_url(url)
    → Download and parse web page text (BeautifulSoup)
    → Returns cleaned text content

  search_news(query, days_back=7)
    → Filter results to recent news only

Agent behavior:
  1. Receives SEARCH task
  2. Formulates search query from task instruction
  3. Searches web, fetches top 3 results fully
  4. Synthesizes findings
  5. Stores in semantic memory for future retrieval
  6. Returns structured summary

Tests:
  - Search "latest LangGraph updates" → gets recent results
  - Fetch a specific URL → returns text content
  - Results stored in semantic memory

Done when: Web Search returns grounded, current information.
```

---

**Day 33 — Friday**
**Task: File System Agent**

```
Build the File System Agent. Reads, writes, and watches files.

File: workers/phase2/file_system_agent.py

Extends: BaseAgent
Model: gemma4:26b-instruct-qat (local)
Capabilities: ["FILE_READ", "FILE_WRITE", "FILE_SEARCH", "DIRECTORY_INDEX"]

Tools to build: tools/file_tools.py
  read_file(path)                          → file content as string
  write_file(path, content)                → write text file
  append_file(path, content)               → append to file
  list_directory(path, recursive=False)    → directory listing
  search_files(directory, pattern)         → find files matching glob
  watch_file(path, callback)               → notify on change
  read_pdf(path)                           → extract text from PDF
  read_docx(path)                          → extract text from DOCX

Safety rules:
  - Only read/write within allowed directories (configurable whitelist)
  - Never touch system files (/etc, /sys, /boot)
  - Log every write operation to episodic memory

Tests:
  - Read a text file → returns content
  - Write a file → content saved to disk
  - Read PDF → text extracted correctly
  - Blocked path rejected → raises PermissionError

Done when: File operations work safely within allowed directories.
```

---

**Day 34 — Saturday**
**Task: FastAPI Backend — Core Routes**

```
Build the FastAPI backend. Both terminal and web connect to this.

File: api/main.py

Routes to implement:

  POST /task
    Body: {"goal": "..."}
    → Creates ARKMessage, sends to Supervisor
    → Returns: {task_id, status: "submitted"}

  GET /task/{task_id}
    → Returns current status + output of a task

  GET /tasks
    → Returns list of recent tasks (from episodic memory)

  DELETE /task/{task_id}
    → Cancels a running task

  GET /agents
    → Returns Agent Registry dump (all agents + status)

  GET /memory/{tier}
    → tier: working|episodic|semantic|procedural
    → Returns memory contents for that tier

  POST /memory/search
    Body: {"query": "...", "tier": "semantic"}
    → Returns similar memories

  WebSocket /ws
    → Client connects → receives all event stream updates

Start FastAPI:
  uvicorn api.main:app --host 0.0.0.0 --port 8000 --reload

Tests:
  - POST /task → task_id returned
  - GET /task/{id} → status updated correctly
  - WebSocket receives events as task runs

Done when: All REST routes work. WebSocket streams events.
```

---

**Day 35 — Sunday**
**Task: Week 5 Review**

```
Review and test everything built this week.

Integration test:
  Run full end-to-end task via API:
  curl -X POST http://localhost:8000/task \
    -d '{"goal": "Search for recent papers on RAG and summarize them"}'

  → Supervisor → Planner → Web Search + Research → Evaluation → Reflection
  → All logged to episodic memory
  → Result retrievable via GET /task/{id}

Check:
  □ Retry logic fires correctly on low scores
  □ Reflection Agent produces useful output
  □ Web Search returns current results
  □ File System reads and writes correctly
  □ FastAPI endpoints all working

Update README with full setup instructions.
```

---

### WEEK 6 — Dashboard + ARKBench v0.1

---

**Day 36 — Monday**
**Task: WebSocket Event System**

```
Build the event emission system. Every agent action broadcasts events.

File: api/events.py

Event types to emit (every agent sends these):
  agent_started      {agent, task_id, timestamp}
  agent_thought      {agent, task_id, thought_text}
  tool_called        {agent, task_id, tool_name, tool_input}
  tool_result        {agent, task_id, tool_name, result_summary}
  agent_done         {agent, task_id, quality_score, duration}
  memory_written     {tier, key, summary}
  task_completed     {task_id, final_score, output_path}
  task_failed        {task_id, error, retry_count}
  conflict_detected  {conflict_type, agents_involved}

Every BaseAgent.run() call emits:
  agent_started → (run) → agent_thought (for each thought) → agent_done

File: core/event_emitter.py
  emit(event_type, data)    → publish to WebSocket + Redis audit_log

Update BaseAgent to inject event_emitter.

Done when: Running any task streams events to WebSocket client.
```

---

**Day 37 — Tuesday**
**Task: Terminal CLI — ark command**

```
Build the terminal interface. Users type: ark "their goal"

File: cli/ark_cli.py

Commands:
  ark "goal text"                  → submit task, stream output live
  ark status <task_id>             → show task status
  ark list                         → show recent tasks
  ark stop <task_id>               → cancel task
  ark agents                       → show all agent statuses
  ark memory search "query"        → search semantic memory
  ark memory show <tier>           → show memory contents
  ark logs <task_id>               → full execution log
  ark                              → interactive mode (REPL)

Terminal output format:
  ARK OS ──────────────────────────────────────────
  Task: ark_001 | Priority: 7 | Goal: "..."
  [10:31:02] Supervisor  → intent: RESEARCH
  [10:31:03] Planner     → 3 subtasks created
  [10:31:04] Research    ⟳ searching arXiv...
  [10:31:18] Research    ✓ 8 papers found
  [10:31:19] Evaluation  ✓ score: 0.87
  Done in 1m 12s | Output: /ark/outputs/ark_001/
  ──────────────────────────────────────────────────

Uses WebSocket internally to receive and format events.

Entry point in pyproject.toml:
  [tool.poetry.scripts]
  ark = "cli.ark_cli:main"

Done when: ark "research quantum computing" runs and streams correctly.
```

---

**Day 38 — Wednesday**
**Task: Web Dashboard — Layout + Live Agent Grid**

```
Build the web dashboard frontend.

Directory: dashboard/ (React + Vite)

Setup:
  cd dashboard && npm create vite@latest . -- --template react
  npm install socket.io-client recharts lucide-react

Components to build today:
  AgentGrid.jsx
    - Shows all 20 agents (or however many exist)
    - Each agent card shows: name, status (IDLE/BUSY/ERROR), model, current task
    - Status color: green=IDLE, yellow=BUSY, red=ERROR, gray=OFFLINE
    - Updates in real-time via WebSocket

  TaskQueue.jsx
    - Shows active + recent tasks
    - Each task shows: goal (truncated), status, time elapsed, score

  LiveLog.jsx
    - Scrolling log of all events
    - Color-coded by event type

Layout:
  Left panel (30%): AgentGrid
  Right panel (70%): TaskQueue on top, LiveLog on bottom

Done when: Dashboard shows live agent status. Colors update as tasks run.
```

---

**Day 39 — Thursday**
**Task: Web Dashboard — Task Detail + Memory Browser**

```
Add task detail view and memory browser to dashboard.

Components to build:

  TaskDetail.jsx
    - Click any task → full detail view
    - Shows: goal, DAG visualization (simple list), agent log, output
    - Download output button

  MemoryBrowser.jsx
    - Tab for each memory tier: Working | Episodic | Semantic | Procedural
    - Working: key-value list
    - Episodic: timeline of past tasks
    - Semantic: searchable list of stored knowledge
    - Procedural: skill library with success rates

  SearchBar.jsx (in MemoryBrowser)
    - Type query → calls POST /memory/search → shows results
    - Results ranked by similarity

Done when: Can browse all 4 memory tiers from browser. Search works.
```

---

**Day 40 — Friday**
**Task: ARKBench v0.1 — First 10 Tasks**

```
Build the evaluation benchmark. This proves ARK OS works.

File: benchmarks/arkbench/benchmark_v0.1.json

Write 10 benchmark tasks (5 research + 5 coding):

Research tasks:
  1. "Summarize the key contributions of the original Transformer paper"
     Expected: mentions attention mechanism, parallelization, BLEU score
  2. "Find 3 papers from 2024 on retrieval-augmented generation"
     Expected: 3 real papers with correct arxiv IDs
  3. "What are the main differences between dense and sparse retrieval?"
     Expected: covers BM25 vs embeddings, accuracy vs speed tradeoffs
  4. "Explain how LoRA reduces VRAM requirements for fine-tuning"
     Expected: covers rank decomposition, frozen base model, adapter size
  5. "What research gaps exist in multi-agent AI systems as of 2025?"
     Expected: mentions memory, conflict resolution, evaluation benchmarks

Coding tasks:
  6. "Write a Python function to calculate RSI given a list of closing prices"
     Expected: correct RSI formula, handles edge cases
  7. "Write a binary search implementation with unit tests"
     Expected: O(log n), works for edge cases (empty, single, not found)
  8. "Write a FastAPI endpoint that accepts a JSON body and returns it reversed"
     Expected: working FastAPI route, correct Pydantic model
  9. "Write a function to detect if a string is a palindrome"
     Expected: handles spaces, capitalization, special chars
  10. "Write a SQLite wrapper class with connect, execute, fetchall methods"
      Expected: context manager, parameterized queries, error handling

File: benchmarks/arkbench/run_benchmark.py
  Runs all 10 tasks sequentially, collects scores, generates report

Done when: Benchmark runs all 10 tasks. Baseline scores recorded.
```

---

**Day 41 — Saturday**
**Task: Run ARKBench v0.1 + Record Baseline**

```
Run the benchmark. Record scores. These are your Month 1 baseline.

Steps:
  1. python benchmarks/arkbench/run_benchmark.py
  2. Collect quality_score for each task
  3. Record in: benchmarks/arkbench/results/month1_baseline.json

Expected baseline scores (rough targets):
  Research tasks: 0.65–0.75 (research quality should be decent)
  Coding tasks:   0.70–0.85 (coding agent + self-repair helps)

If any task scores below 0.50:
  → Debug that specific task
  → Fix the agent's prompt or tool
  → Re-run that task until score > 0.60

This baseline is IMPORTANT — Month 6 must beat these scores to prove M5 (self-improvement).

File: benchmarks/arkbench/results/month1_baseline.json
  {task_id, score, duration, agent_path, notes}

Done when: All 10 tasks have scores. Baseline saved.
```

---

**Day 42 — Sunday**
**Task: Month 2 Review**

```
Month 2 complete. Full audit.

Checklist:
  □ Retry logic + 3-attempt escalation works
  □ Reflection Agent produces useful patterns
  □ Web Search Agent returns current results
  □ File System Agent reads/writes safely
  □ FastAPI backend — all routes working
  □ WebSocket streams events in real-time
  □ Terminal CLI: ark command works
  □ Web Dashboard: live agent grid, task list, memory browser
  □ ARKBench v0.1: 10 tasks, baseline scores recorded

Month 2 deliverable: Write docs/month2_results.md
  Include: ARKBench v0.1 scores table

Paper #1 status: Introduction + Architecture section draft
```

---

# MONTH 3 — PHASE 2 WORKERS PART 1
## Goal: Browser, Terminal, GitHub agents + Coding self-repair evaluation

---

### WEEK 7 — Browser + Terminal Agents

---

**Day 43 — Monday**
**Task: Playwright Browser Tool**

```
Build browser automation tools. Browser Agent uses these.

File: tools/browser_tools.py

Install: pip install playwright && playwright install chromium

Functions:
  open_browser()                           → launch headless Chromium
  navigate(url)                            → go to URL
  get_page_text()                          → extract all visible text
  get_page_html()                          → raw HTML
  click(selector)                          → click element by CSS selector
  type_text(selector, text)               → type in input field
  screenshot(path)                         → save screenshot as PNG
  fill_form(field_map: dict)              → fill multiple fields at once
  extract_links()                          → all links on page
  extract_table(selector)                 → parse HTML table to list of dicts
  wait_for_element(selector, timeout=10)  → wait until element appears
  close_browser()

Context manager:
  async with ARKBrowser() as browser:
      await browser.navigate("https://arxiv.org")
      text = await browser.get_page_text()

Tests:
  - Navigate to a known URL, get text
  - Screenshot saves correctly
  - Extract links returns list of URLs

Done when: Can navigate, read, and screenshot any website.
```

---

**Day 44 — Tuesday**
**Task: Browser Agent**

```
Build the Browser Agent that uses Playwright tools autonomously.

File: workers/phase2/browser_agent.py

Extends: BaseAgent
Model: gemma4:26b-instruct-qat (local)
Capabilities: ["WEB_BROWSE", "WEB_SCRAPE", "FORM_FILL", "SCREENSHOT"]

What it does:
  1. Receives a task with URL + instruction
  2. Opens browser, navigates to URL
  3. Decides what actions to take (read, click, fill, extract)
  4. Executes actions step by step (ReAct loop)
  5. Returns structured extracted content
  6. Always takes screenshot as evidence

System prompt:
  You are the ARK OS Browser Agent. You control a web browser.
  For each step, output:
  {"thought": "...", "action": "navigate|click|type|extract|screenshot", "params": {...}}
  Continue until task is complete.

Done when: Browser Agent can navigate to arXiv, search for a topic,
           extract paper titles, return them as structured output.
```

---

**Day 45 — Wednesday**
**Task: Terminal Agent**

```
Build the Terminal Agent. Runs bash commands safely.

File: workers/phase2/terminal_agent.py
File: tools/terminal_tools.py

SAFETY RULE: All commands run in Docker sandbox — never on host directly.

tools/terminal_tools.py:
  run_command(cmd: str, timeout=30, sandbox=True) -> dict
    → {stdout, stderr, exit_code, duration}
    → sandbox=True: runs in Docker container
    → sandbox=False: runs on host (requires explicit permission flag)

  run_script(script_path: str, args=None) -> dict
  list_processes() -> list
  kill_process(pid) -> bool
  check_command_exists(cmd) -> bool
  get_environment() -> dict

Terminal Agent:
  Capabilities: ["TERMINAL_EXECUTE", "PROCESS_MANAGE", "INSTALL_PACKAGE"]

  Understands natural language commands:
    "Install the requests library" → pip install requests (sandboxed)
    "Check if Python 3.11 is installed" → python3.11 --version
    "List all running processes" → ps aux

  Parses stdout/stderr intelligently:
    If exit_code != 0 → analyze stderr, suggest fix
    If "ModuleNotFoundError" → auto-suggest pip install

Done when: Terminal Agent runs bash commands safely and interprets output.
```

---

**Day 46 — Thursday**
**Task: Conflict Resolution — Type 1 (Resource)**

```
Build resource conflict resolution. When two agents want the same tool.

File: kernel/conflict_resolver.py (update)

Type 1: Resource conflict
  Scenario: Browser Agent + Web Search Agent both try to use Playwright

ToolSemaphore:
  acquire(tool_name, agent_name, task_id) → bool (got it or queued)
  release(tool_name, agent_name)
  get_queue(tool_name) → list of waiting agents

Semaphore rules:
  - Browser: max 1 concurrent user
  - Terminal sandbox: max 2 concurrent (2 Docker containers)
  - arXiv API: max 3 concurrent (rate limit friendly)
  - Qdrant: unlimited (vector DB handles concurrent reads fine)

Router update:
  Before dispatching tool-using task → acquire semaphore
  After task completes → release semaphore
  Queued agents get notified when slot opens

Tests:
  - Two Browser tasks → second queued until first releases
  - Three Terminal tasks → two run, one queues
  - Release triggers queued agent

Done when: No two agents use the same exclusive tool simultaneously.
```

---

**Day 47 — Friday**
**Task: GitHub Agent**

```
Build the GitHub Agent. Manages repos, PRs, and issues.

File: workers/phase2/github_agent.py
File: tools/github_tools.py

Install: pip install gitpython PyGithub

tools/github_tools.py:
  clone_repo(url, local_path)
  get_repo_info(repo_path)            → {name, branches, last_commit}
  get_open_issues(repo_url)           → list of issues
  create_branch(repo_path, name)
  commit_files(repo_path, message, files)
  create_pull_request(repo, title, body, branch)
  get_file_content(repo_path, file_path)
  list_changed_files(repo_path)       → files changed vs main

GitHub Agent:
  Capabilities: ["GIT_COMMIT", "PR_CREATE", "ISSUE_TRIAGE", "REPO_ANALYSIS"]

  Tasks it handles:
    "Commit the changes in /ark/outputs/coding/ to feature/rsi-agent branch"
    "Create a PR for the new research agent implementation"
    "List all open issues in this repository and triage them"

Done when: GitHub Agent can commit files, create a branch, open a PR.
```

---

**Day 48 — Saturday**
**Task: Coding Agent Self-Repair Evaluation**

```
Formally evaluate the Coding Agent's self-repair capability.
This is the Month 3 deliverable.

File: benchmarks/coding_self_repair/run_self_repair_eval.py

Build 10 intentionally broken code tasks:
  1. Python function with syntax error (missing colon)
  2. Import of non-existent package
  3. Off-by-one error in list indexing
  4. Wrong variable name (undefined variable)
  5. Division by zero (no guard)
  6. Infinite loop (no termination condition)
  7. Type mismatch (str + int)
  8. Missing return statement
  9. Incorrect API call (wrong parameter name)
  10. Logic error (wrong comparison operator)

For each:
  1. Give broken code to Coding Agent
  2. Count how many repair attempts needed
  3. Record: fixed (True/False), attempts_needed, final_score

Metric: Self-Repair Rate = tasks_fixed / 10

Target: >= 7/10 tasks self-repaired within 3 attempts

File: benchmarks/coding_self_repair/results.json
  {task, error_type, fixed, attempts, final_score}

Done when: Self-repair evaluation run. Results saved. Report written.
```

---

**Day 49 — Sunday**
**Task: Week 7 Review**

```
Checklist:
  □ Browser Agent navigates, extracts, screenshots
  □ Terminal Agent runs commands in sandbox
  □ GitHub Agent commits, branches, PRs
  □ Resource conflicts resolved via semaphores
  □ Self-repair evaluation: results recorded

Write: docs/self_repair_evaluation.md
  Include: per-error-type repair rates, common failure patterns
```

---

### WEEK 8 — Document + Security + Hardening

---

**Day 50 — Monday**
**Task: Document Agent**

```
Build the Document Agent. Reads and writes PDFs, DOCX, XLSX.

File: workers/phase2/document_agent.py
File: tools/document_tools.py

Install: pip install pypdf python-docx openpyxl

tools/document_tools.py:
  read_pdf(path)              → text content
  read_docx(path)             → text content
  read_xlsx(path)             → dict of {sheet_name: [[rows]]}
  write_docx(path, content)   → create Word document
  write_pdf(path, content)    → create PDF (via reportlab or weasyprint)
  write_xlsx(path, data_dict) → create Excel file

Document Agent:
  Capabilities: ["READ_PDF", "READ_DOCX", "WRITE_REPORT", "PARSE_PAPER"]

  Primary use: Read academic papers (PDF) and write output reports (DOCX/PDF)

Done when: Can read a real PDF research paper and extract clean text.
           Can write a formatted DOCX report from ARK OS output.
```

---

**Day 51 — Tuesday**
**Task: Security Agent — CVE + Static Analysis**

```
Build the Security Agent. Scans for vulnerabilities. Fully local.

File: workers/phase2/security_agent.py
File: tools/security_tools.py

Install: pip install semgrep bandit python-nmap

tools/security_tools.py:
  run_semgrep(target_dir, rules="auto")       → list of findings
  run_bandit(target_file)                     → list of vulnerabilities
  check_dependencies(requirements_file)       → CVE hits from NVD API
  check_owasp_top10(target_dir)               → OWASP coverage report
  generate_audit_report(findings, output_path) → structured HTML report

Security Agent:
  Capabilities: ["SECURITY_SCAN", "CVE_CHECK", "AUDIT_REPORT"]

  CRITICAL RULE: Never calls external APIs with user code.
                 All analysis runs locally only.

  On receiving SECURITY_SCAN task:
    1. Run Semgrep on target directory
    2. Run Bandit on Python files
    3. Check requirements.txt against NVD (CVE lookup only, no code sent)
    4. Generate structured findings report
    5. Classify each finding: CRITICAL | HIGH | MEDIUM | LOW

Tests:
  - Scan a file with a known SQL injection pattern → Semgrep finds it
  - Scan file with hardcoded password → Bandit finds it
  - Audit report generated as HTML

Done when: Security Agent finds injected vulnerabilities in test code.
```

---

**Day 52 — Wednesday**
**Task: Neo4j Citation Graph Integration**

```
Connect Neo4j to the Research Agent. Citation graph for SAGE Mode.

File: memory/citation_graph.py

Install: pip install neo4j

Schema (Neo4j nodes and relationships):
  Node: Paper     {arxiv_id, title, authors, year, abstract_embedding_id}
  Node: Concept   {name, category}
  Edge: CITES     (Paper)-[:CITES]->(Paper)
  Edge: MENTIONS  (Paper)-[:MENTIONS]->(Concept)
  Edge: RELATED   (Concept)-[:RELATED]->(Concept)

Functions:
  add_paper(paper_dict)
  add_citation(citing_id, cited_id)
  add_concept(name, category)
  link_paper_to_concept(arxiv_id, concept_name)
  get_paper_citations(arxiv_id)
  get_citation_network(arxiv_id, depth=2)    → subgraph
  find_concept_gaps()                         → concepts with few connections
  find_bridge_concepts(concept_a, concept_b) → concepts connecting two ideas

Update Research Agent to:
  - After reading each paper → add_paper() + add_citations()
  - Extract concepts from paper using LLM → add to Neo4j

Done when: After reading 5 papers, Neo4j has a citation graph.
           find_concept_gaps() returns concepts with sparse coverage.
```

---

**Day 53 — Thursday**
**Task: Conflict Resolution — Type 4 (Memory)**

```
Build memory conflict resolution. When agents write conflicting skills.

File: kernel/conflict_resolver.py (update)

Type 4: Memory conflict
  Two agents write different approaches to the same skill_id

MemoryConflictResolver:
  detect_skill_conflict(skill_id_a, skill_id_b) -> bool
  resolve_skill_conflict(skill_a, skill_b) -> Skill
    Resolution strategy:
      Keep BOTH versions tagged with source agent
      Tag: {version: "A", source: "research_agent", success_rate: 0.82}
           {version: "B", source: "coding_agent",   success_rate: 0.71}
    On retrieval: return version with higher success_rate
    Flag for Reflection Agent: "Conflict detected in skill X, please review"

  merge_skills(skill_a, skill_b) -> Skill
    → if steps are compatible: merge into unified skill
    → if incompatible: keep both versions (version_a, version_b)

Tests:
  - Two agents write same skill_id with different steps
  - Higher success_rate version used on retrieval
  - Reflection Agent notified of conflict

Done when: Memory conflicts detected, versioned, and resolved correctly.
```

---

**Day 54 — Friday**
**Task: 14-Agent Integration Test**

```
Run a complex multi-domain task that uses 14 agents.

Goal:
  "Find recent papers on code security vulnerabilities, 
   implement a simple vulnerable web app to demonstrate one,
   then scan it with the Security Agent and generate a report"

Agents that should activate:
  Supervisor, Planner, Memory (always)
  Research Agent (find CVE papers)
  Web Search Agent (find recent CVEs)
  Coding Agent (write vulnerable Flask app)
  Security Agent (scan it)
  Document Agent (generate HTML report)
  Reflection Agent (log what worked)

Expected outputs:
  /outputs/{task_id}/
    research_summary.md
    vulnerable_app.py
    security_audit.html
    reflection_log.json

Run test, debug every failure, fix issues.

Done when: Full pipeline runs end-to-end. All 7 agents activate correctly.
```

---

**Day 55 — Saturday**
**Task: ARKBench v0.5 — Add 20 More Tasks**

```
Expand ARKBench to 30 tasks total.

Add 10 security tasks:
  1. "Scan this Python file for SQL injection vulnerabilities"    (inject one)
  2. "Check if this requirements.txt has any known CVEs"
  3. "Find hardcoded credentials in this codebase"
  4. "Generate an OWASP Top 10 compliance report for this project"
  5. "Detect path traversal vulnerability in this Flask route"
  ... (5 more security scenarios)

Add 10 browser/document/terminal tasks:
  6. "Navigate to arxiv.org and find today's top papers in cs.AI"
  7. "Read this PDF and extract all figure captions"
  8. "Create a DOCX report from this JSON data"
  9. "Clone this GitHub repo and list all Python files"
  10. "Run this shell script and capture the output"
  ... (5 more browser/doc/terminal tasks)

Run all 30 tasks. Record scores.
Compare to Month 1 baseline (should be higher now — more agents available).

Done when: 30 ARKBench tasks run. Results saved as month3_results.json
```

---

**Day 56 — Sunday**
**Task: Month 3 Review**

```
Month 3 complete.

Checklist:
  □ Browser Agent fully working
  □ Terminal Agent sandboxed and working
  □ GitHub Agent commits, PRs working
  □ Document Agent reads PDFs, writes DOCX
  □ Security Agent scans with Semgrep + Bandit
  □ Neo4j citation graph integration working
  □ Type 1 + Type 4 conflict resolution working
  □ 14-agent integration test passed
  □ ARKBench v0.5: 30 tasks scored

Write: docs/month3_results.md
Paper #1: Add Methods section draft
Paper #2: Start conflict resolution data collection
```

---

# MONTH 4 — PHASE 2 COMPLETE + HARDENING
## Goal: Finalize all 14 agents + ARKBench security track + Paper #2 data

---

### WEEKS 9–10 (Days 57–70) — Hardening + Paper Data

---

**Day 57 — Conflict Resolution Type 2 (Output Conflict)**
```
Build the hardest conflict type. Research says X, Code proves not-X.
File: kernel/conflict_resolver.py

Resolution: Both agents re-run with each other's output as context.
If still conflicting after 2 rounds → human escalation flag set.
Collect 5 real conflict examples for Paper #2.
```

**Day 58 — Security Agent Red-Team Simulation**
```
Add red-team simulation capability to Security Agent.
File: workers/phase2/security_agent.py

Run OWASP WebGoat (pre-built vulnerable app) locally.
Security Agent scans it → finds known vulnerabilities.
Recall metric: % of injected vulnerabilities found.
Target: >= 0.75 recall on OWASP WebGoat.
```

**Day 59 — Memory TTL + Eviction Policies**
```
Implement memory eviction so ARK OS doesn't grow unbounded.
File: memory/memory_agent.py

Policies:
  Working memory: auto-expire after task completion (Redis TTL)
  Episodic: keep last 10,000 episodes, archive older ones
  Semantic: evict vectors with zero retrieval in 30 days
  Procedural: evict skills with success_rate < 0.30

Test: run 100 tasks, verify memory stays bounded.
```

**Day 60 — Performance Optimization**
```
Profile every agent. Find the slowest operations. Fix them.

Tools: cProfile, py-spy, asyncio profiling

Target latencies:
  Memory retrieval:     < 100ms
  Research Agent:       < 2 minutes for 5 papers
  Coding Agent:         < 3 minutes for medium task
  Evaluation Agent:     < 30 seconds

Fix top 3 bottlenecks found.
```

**Day 61 — Error Recovery + Graceful Degradation**
```
What happens when Groq is down? When Redis restarts? When Ollama crashes?

Build fallback handlers:
  Groq down → use local Qwen2.5-14B via Ollama
  Redis down → use in-memory dict (lose working memory only)
  Ollama down → return FAILED with clear error message
  Qdrant down → use FAISS-only retrieval (degraded semantic search)

Test all failure modes. Verify ARK OS degrades gracefully.
```

**Day 62 — Dashboard: Task DAG Visualization**
```
Add visual task DAG to web dashboard.
Show nodes (tasks) and edges (dependencies) as a graph.
Use React Flow or simple SVG.
Active task → pulsing animation.
Completed → green. Failed → red. Running → yellow pulse.
```

**Day 63 — Sunday: Week 9 Review**
```
14 agents fully operational.
All conflict types resolved.
Dashboard shows DAG visualization.
```

**Day 64 — Security Benchmark: 10 CVE Tasks**
```
Add 10 CVE-based security tasks to ARKBench.
Use real CVE-2023 and CVE-2024 entries.
Measure: recall, precision, F1 for Security Agent.
```

**Day 65 — Reflection Agent Quality Analysis**
```
After 100+ tasks completed, analyze Reflection Agent output.
Does it produce actionable patterns?
Are procedural memory skills actually improving agent behavior?
Write analysis: docs/reflection_quality_analysis.md
```

**Day 66 — Agent Registry Load Balancing Test**
```
Test load_count-based routing.
Spawn 2 instances of Research Agent.
Verify tasks distributed evenly.
avg_quality_score drops → agent deprioritized.
```

**Day 67 — Paper #2 Data Collection**
```
Log all conflict events for the past month.
Classify by type (1, 2, 3, 4).
Measure: frequency, resolution time, outcome.
This data is the empirical basis for Paper #2.
File: research/conflict_data/conflict_log.json
```

**Day 68 — End-to-End Stress Test**
```
Run 20 tasks simultaneously.
Verify:
  No race conditions in memory writes
  Semaphores prevent tool conflicts
  All 20 tasks complete (no deadlocks)
  Scores are consistent with single-task runs
```

**Day 69 — Complete ARKBench to 50 Tasks**
```
Final 20 tasks added:
  10 agent coordination (multi-agent chains)
  10 mixed domain (research + code + security together)

Run all 50 tasks. Month 4 baseline scores saved.
Target: 5–10% improvement over Month 1 baseline (early self-improvement effect).
```

**Day 70 — Month 4 Review**
```
14 agents complete. ARKBench 50 tasks. All conflict types handled.
Write: docs/month4_results.md
Paper #1: Architecture section complete
Paper #2: Data collected, analysis started
Start preparing SAGE Mode components for Month 5.
```

---

# MONTH 5 — INTELLIGENCE LAYER + SAGE MODE
## Goal: 6 new agents + SAGE pipeline + first discovery run

---

### WEEKS 11–14 (Days 71–112)

---

**Day 71 — Vision Agent Setup**
```
Build the Vision Agent using Qwen2-VL-7B.
File: workers/phase3/vision_agent.py
Capabilities: SCREENSHOT_ANALYSIS, IMAGE_UNDERSTANDING, OCR, FIGURE_READING
Tools: take_screenshot(), analyze_image(), extract_text_from_image()
Test: analyze a scientific figure, extract numbers from a chart.
```

**Day 72 — Vision Agent: Scientific Figure Analysis**
```
Specialize Vision Agent for scientific plots and figures.
Understands: line plots, bar charts, confusion matrices, architecture diagrams.
For SAGE Mode: reads experiment result figures.
Test: analyze a matplotlib output figure and describe what it shows.
```

**Day 73 — ML Engineer Agent**
```
Build the ML Engineer Agent.
File: workers/phase3/ml_engineer_agent.py
Model: qwen2.5-coder:7b (local)
Capabilities: EXPERIMENT_TRACK, HPO, TRAIN_MODEL, EVALUATE_MODEL
MLflow integration: every experiment auto-logged.
Optuna integration: Bayesian HPO runs automatically.
Test: run 10-trial Optuna study on a simple model, log to MLflow.
```

**Day 74 — ML Engineer: Experiment Pipeline**
```
Build full experiment pipeline in ML Engineer Agent.
Given: {model, dataset, hyperparameter_ranges}
Does: run experiment, log metrics, plot results, return best config.
Uses: Coding Agent to write training code, Terminal Agent to run it.
Test: full HPO run on sklearn model, MLflow dashboard shows results.
```

**Day 75 — DevOps Agent**
```
Build the DevOps Agent.
File: workers/phase3/devops_agent.py
Model: gemma4:26b-instruct-qat (local)
Capabilities: DOCKER_MANAGE, CI_PIPELINE, DEPLOY
Tools: docker build/run/stop, write GitHub Actions YAML
Test: build a Docker image from Coding Agent output, run it.
```

**Day 76 — Data Analysis Agent**
```
Build the Data Analysis Agent.
File: workers/phase3/data_analysis_agent.py
Model: gemma4:26b-instruct-qat (local)
Capabilities: DATA_ANALYZE, VISUALIZE, STATISTICAL_TEST, REPORT
Tools: pandas operations, matplotlib plots, scipy stats tests
Test: given a CSV, produce mean/std/correlation + 3 plots + summary.
```

**Day 77 — Sunday: Week 11 Review**
```
18 agents operational.
Missing: Self-Improvement (19) + Capability Discovery (20).
SAGE components start next week.
```

**Day 78 — Hypothesis Engine (SAGE Component 1)**
```
Build the Hypothesis Engine. Extends Planner Agent.
File: sage/hypothesis_engine.py

Takes: 50+ paper embeddings from Qdrant + Neo4j citation graph
Does:
  1. Find concept pairs with no connecting paper (knowledge gaps)
  2. Find contradictions across papers (two papers claim opposite things)
  3. Generate hypotheses from gaps and contradictions
  4. Score each hypothesis: novelty × feasibility
  5. Return ranked list of top-10 hypotheses

Novelty score: cosine distance from existing paper embeddings
Feasibility score: LLM judgment (can this be tested computationally?)

Test: given 20 materials science papers → generates 5 ranked hypotheses.
```

**Day 79 — Experiment Designer (SAGE Component 2)**
```
Build the Experiment Designer. Extends ML Engineer Agent.
File: sage/experiment_designer.py

Takes: one hypothesis from Hypothesis Engine
Outputs: complete experiment specification
  {
    "hypothesis": "...",
    "dataset": "Materials Project API query X",
    "method": "train regression model to predict Y from features Z",
    "baseline": "linear regression",
    "metrics": ["MAE", "R2", "RMSE"],
    "code_spec": "detailed coding instruction for Coding Agent"
  }

Coding Agent then implements this spec.
Test: hypothesis → experiment spec → Coding Agent produces runnable code.
```

**Day 80 — Scientific Report Generator (SAGE Component 3)**
```
Build the report generator. Extends Document Agent.
File: sage/report_generator.py

Takes: hypothesis + experiment spec + results + figures
Outputs: LaTeX paper draft with:
  Abstract, Introduction, Related Work, Method, Results, Discussion

Template sections filled with:
  - Hypothesis Engine output → Introduction/Method
  - Experiment results → Results section
  - Paper citations → Related Work (from Neo4j citation graph)
  - Vision Agent figure analysis → Figure captions

Test: given mock experiment results → produces valid LaTeX file.
```

**Day 81 — SAGE Loop Wiring**
```
Wire all SAGE components into the discovery loop.
File: sage/sage_orchestrator.py

Full loop:
  Literature → Hypothesis → Experiment → Analysis → Report → Memory → Loop

Test: Run one complete SAGE loop on Materials Science topic.
Each step produces output. Loop continues automatically.
```

**Day 82 — Materials Science API Integration**
```
Add Materials Project API tools for SAGE flagship domain.
File: tools/science/materials_tools.py

pip install mp-api

Functions:
  search_materials(formula_or_property)
  get_material_properties(material_id)
  get_similar_materials(properties_dict)
  query_band_gap_range(min_ev, max_ev)
  get_stability_data(material_id)

Test: query Materials Project for all solar cell candidate materials
      with band gap between 1.0–1.7 eV.
```

**Day 83 — Sunday: Week 12 Review**
```
All SAGE components built.
Discovery loop runs.
Materials Science API connected.
Test one complete Materials Science hypothesis run.
```

**Day 84–91 — SAGE Loop Testing + Debugging (Week 13)**
```
Week 13 is dedicated to running and fixing the SAGE loop.

Day 84: First full SAGE run (expect failures, debug them)
Day 85: Fix hypothesis engine output format issues
Day 86: Fix experiment designer code spec quality
Day 87: Fix LaTeX report generation (citation formatting)
Day 88: Re-run SAGE loop end-to-end cleanly
Day 89: Run SAGE on 2nd hypothesis (verify loop repeats)
Day 90: Vision Agent reads SAGE-generated experiment plots
Day 91: Sunday review — SAGE loop producing usable output?
```

**Day 92–98 — Self-Improvement + Capability Discovery (Week 14)**
```
Day 92: Self-Improvement Agent (Agent 19)
        File: workers/phase3/self_improvement_agent.py
        Month 6 version: prompt A/B testing on ARKBench tasks
        Reads Reflection logs → finds patterns → proposes prompt changes
        A/B tests new prompts → if +5% improvement, applies change

Day 93: Capability Discovery Agent (Agent 20)
        File: workers/phase3/capability_discovery_agent.py
        Reads Agent Registry + Reflection logs
        Answers: "Can ARK OS do X?" with confidence score
        Updates capability map dynamically

Day 94: Voice Interface (faster-whisper)
        pip install faster-whisper
        STT pipeline: microphone → text → ARK OS task submission
        Test: speak a goal, ARK OS executes it

Day 95: All 20 agents integration test
        Run a task that touches all 20 agents
        Verify each one activates at least once

Day 96: First SAGE Materials Science paper draft
        Run complete SAGE loop on: "predict novel solar cell materials"
        Output: LaTeX draft + experiment code + results

Day 97: ARKBench month 5 run
        All 50 tasks. Compare to month 1 and month 4.
        Should see measurable improvement.

Day 98: Sunday — month 5 review
```

---

# MONTH 6 — INTEGRATION + EVALUATION + PAPERS
## Goal: Polish + ARKBench final + Month 1 vs Month 6 comparison + Paper submissions

---

### WEEKS 15–16 (Days 99–112) — Polish + Self-Improvement Evidence

---

**Day 99 — Self-Improvement First Full Run**
```
Run Self-Improvement Agent on all agents with 3+ months of data.
For each agent:
  Read 50 Reflection logs
  Identify failure patterns
  Generate improved prompt
  A/B test (10 ARKBench tasks each)
  Record: old score vs new score

Target: at least 3 agents show >= 5% improvement.
```

**Day 100 — Dashboard v1.0 Polish**
```
Production-ready web dashboard.
Add: SAGE Mode control panel (start/stop loop, view current hypothesis)
Add: Self-Improvement tracker (scores over time per agent, graph)
Add: ARKBench results visualization (radar chart across domains)
Add: Memory stats (live vector count, episode count, skill count)
Make it presentable for paper screenshots.
```

**Day 101 — ARKBench Final: All 50 Tasks**
```
This is the definitive Month 6 run.
Run all 50 ARKBench tasks with fully matured 20-agent ARK OS.
Compare to Month 1 baseline.

Expected improvement areas:
  Research tasks: +10–15% (more papers in Qdrant, better RAG)
  Coding tasks:   +8–12%  (Self-Improvement has tuned prompts)
  Security tasks: +5–10%  (procedural memory has accumulated patterns)
  SAGE tasks:     new category (didn't exist in Month 1)

Save: benchmarks/arkbench/results/month6_final.json
Generate: improvement comparison report
```

**Day 102 — Month 1 vs Month 6 Analysis**
```
This is Metric M5. The proof that ARK OS self-improves.

Generate comparison table:
  Task category | Month 1 score | Month 6 score | Delta | % improvement

Plot: line graph of score over months (1, 2, 3, 4, 5, 6) for each category

Write: docs/self_improvement_evidence.md
  This document is a core section of Paper #1 and Paper #3.

If any category did NOT improve:
  Investigate why. Document the finding.
  This is also publishable — honest negative results matter.
```

**Day 103 — Complete Integration Test**
```
The most complex test. 6 hours. All 20 agents.

Goal: "Research emerging solar cell materials, implement a
       property prediction model, scan it for security issues,
       deploy it as a REST API, and generate a research paper"

Expected agent activations:
  Supervisor, Planner, Memory × many times
  Research Agent × 2 runs
  Web Search Agent
  Hypothesis Engine (SAGE)
  Experiment Designer (SAGE)
  Coding Agent × 3 runs
  Security Agent
  Evaluation Agent × 5 runs
  Reflection Agent × 5 runs
  DevOps Agent
  ML Engineer Agent
  Data Analysis Agent
  Vision Agent
  Document Agent
  Scientific Report Generator (SAGE)
  GitHub Agent (commit outputs)

If it runs end-to-end: you have your flagship demo.
```

**Day 104–110 — Paper Writing (Week 16)**
```
Day 104: Paper #1 — Architecture paper
         Write: Abstract, Introduction, Related Work, Architecture, Evaluation
         Include ARKBench comparison tables, self-improvement graph

Day 105: Paper #1 — Method section details
         AgentSpec v1.0 formal specification
         Four-tier memory architecture formalization
         LangGraph state machine diagram

Day 106: Paper #3 — Hypothesis Generation (SAGE)
         Write: Introduction, Related Work (vs AI Scientist)
         Include: Materials Science case study results
         Novelty scoring method formalization

Day 107: ARKBench paper (Paper #4)
         Benchmark design, task categories, baseline comparisons
         vs AutoGPT / BabyAGI / MetaGPT on same tasks
         Submit to arXiv as preprint first

Day 108: Paper #1 final review + co-author review
         Grammar, citations, figure quality
         Submit to NeurIPS / ICLR workshop track first

Day 109: GitHub repository preparation
         Public release: clean README, setup guide, demo GIFs
         Apache 2.0 LICENSE file
         CONTRIBUTING.md, CODE_OF_CONDUCT.md
         Docker Compose one-command setup verified

Day 110: Sunday — Month 6 review
```

---

### WEEKS 17–18 (Days 111–120) — Release + Submission

---

**Day 111 — Public GitHub Release**
```
Make ark-os repository public.
Announce on Twitter/LinkedIn.
Share with SRIHER faculty.
```

**Day 112 — Paper #1 Submission**
```
Submit ARK OS Architecture paper to target venue.
```

**Day 113 — Demo Video**
```
Record a 5-minute demo video:
  1. Terminal: ark "research quantum computing and implement a circuit simulator"
  2. Web dashboard showing agents working in real-time
  3. SAGE mode discovering a materials science hypothesis
  4. Month 1 vs Month 6 score comparison graph

Upload to YouTube. Link in README.
```

**Day 114 — Paper #4 arXiv Preprint**
```
Submit ARKBench paper to arXiv.
This gives you a citable preprint immediately.
```

**Day 115 — `pip install ark-os` Setup**
```
Publish ARK OS to PyPI.
User flow:
  pip install ark-os
  ark init          → guided setup (installs Docker, Ollama, pulls models)
  ark "first task"  → runs immediately

pyproject.toml: package configuration
  Entry point: ark = ark_os.cli.ark_cli:main
```

**Day 116–120 — Buffer Days**
```
These 5 days are buffer for anything that slipped.
Use for:
  - Bug fixes from public GitHub issues
  - Paper revision based on advisor feedback
  - Documentation improvements
  - Additional ARKBench tasks
  - SAGE loop improvements
```

---

# SUMMARY TABLE

```
Month  Weeks  Agents Built   Key Deliverable
──────────────────────────────────────────────────────────────────────
1      1–4    6 (kernel)     Memory Agent + MVP loop + first e2e test
2      5–6    8 (+ web, FS)  ARKBench v0.1 + dashboard + CLI
3      7–8    11 (+browser,  Self-repair evaluation + 14-agent test
               terminal,
               github)
4      9–10   14 (+doc,      ARKBench 50 tasks + conflict resolution
               security,      data for Paper #2
               reflection)
5      11–14  20 (all)       SAGE loop + Materials Science paper draft
6      15–18  20 (polished)  Month 1 vs 6 comparison + paper submissions
                             Public GitHub release + pip package
```

---

# DAILY RULES (Apply Every Day)

```
1. One primary deliverable per day. Do not start Day N+1 deliverable
   if Day N is not complete. Carry it forward.

2. Write tests before or alongside the code. Not after.

3. Every new file must have a corresponding test file.

4. Run the full test suite every Friday before the weekend.

5. Memory Agent is sacred. If you break it, fix it before touching
   anything else.

6. Commit to git every day, even if incomplete.
   Commit message format: "[Day NN] AgentName: what was built"

7. Sunday is review + documentation only. No new features.

8. If a week's work is running 2+ days late, do not try to rush.
   Drop the lowest-priority item for that week and move forward.

9. ARKBench scores must be recorded every month. This is non-negotiable.
   The Month 1 vs Month 6 comparison is a core paper claim.

10. Never change ARKMessage schema after Day 4. AgentSpec v1.0 is frozen.
```

---

*ARK OS — Day-by-Day Build Plan v1.0*
*Kabilash (E0323036) · SRIHER Chennai · 2026*
*6 months · 20 agents · 120 planned build days*
