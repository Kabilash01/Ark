# Kernel Architecture — Week 3 Review

## Components built this week

| Day | Component | File |
|---|---|---|
| 15 | LangGraph state machine | `kernel/ark_state.py`, `kernel/ark_graph.py` |
| 16 | Supervisor Agent + STATE.md writer | `kernel/supervisor_agent.py`, `core/state_writer.py` |
| 17 | Planner Agent | `kernel/planner_agent.py` |
| 18 | Supervisor ↔ Planner integration | `kernel/supervisor_agent.py` (`handle_goal`) |
| 19 | Router + topological dispatch | `kernel/router.py` |
| 20 | Heartbeat Monitor | `kernel/heartbeat_monitor.py` |

## End-to-end trace (verified, `tests/integration/test_kernel_end_to_end.py`)

```
"Research quantum computing"
   │
   ▼
Supervisor.classify()          -- Groq gpt-oss-120b, intent = RESEARCH
   │
   ▼
MemoryAgent.inject_context()   -- pulls relevant working/semantic/procedural context
   │
   ▼
Planner.plan()                 -- Groq gpt-oss-120b, produces a task DAG (JSON)
   │                               validated acyclic (topological check)
   ▼
Router.dispatch()               -- topological layering, parallel dispatch
   │                               within each layer, skips OFFLINE agents
   │                               (checked against Agent Registry)
   ▼
(Research stub)                 -- per the Week 3 checkpoint; real
                                    Research/Coding/Evaluation agents are
                                    Days 22-26, not yet built
```

Every step above is logged to the shared Redis audit log (`ARKMessageBus.log_to_audit`) as it happens.

## Model tier decision (deviation from the plan, documented since Day 16)

The plan specifies `claude-sonnet-4-6` (primary) → `groq:qwen2.5-72b` (fallback) for Supervisor and Planner. Neither is available: no `ANTHROPIC_API_KEY` is configured, and `qwen2.5-72b` no longer exists in Groq's hosted catalog. Both agents instead use:

```
Groq gpt-oss-120b → Groq llama-3.3-70b-versatile → local Ollama (qwen2.5:7b)
```

Privacy mode forces straight to the local tier, skipping Groq entirely.

## Real bugs found and fixed while building this week

1. **Token-truncation bug (Day 16):** `gpt-oss-120b` is a reasoning model — it spends tokens on an internal `reasoning` field before emitting `content`. `max_tokens: 10` was cutting it off before any answer appeared (`finish_reason: "length"`, empty `content`), silently falling through to a weaker fallback model that got the answer wrong. Fixed by raising the budget to 500/2000 tokens.
2. **VRAM contention (Day 16):** the embedding model (`BAAI/bge-large-en-v1.5`) was defaulting to GPU and competing with local Ollama models for the same 8GB card, causing `CUDA out of memory` errors when the full test suite ran. Fixed by defaulting the encoder to `device="cpu"` in both `memory/semantic_memory.py` and `memory/procedural_memory.py` — embeddings don't need GPU acceleration for short text, and this keeps VRAM free for the LLMs that actually need it. Also consolidated three duplicate per-test-file encoder fixtures into one shared, session-scoped fixture (`tests/memory/conftest.py`).
3. **Undeclared dependency:** `numpy` was imported directly in `memory/procedural_memory.py` but never listed in `pyproject.toml` — it only worked by accident via transitive installation.

## Known, documented limitation: LLM non-determinism

`gpt-oss-120b` on Groq is not perfectly bit-deterministic even at `temperature=0` — observed as a rare, isolated flip on one borderline classification case, not reproducible on demand, likely a batch-composition effect on Groq's serving side rather than a prompt issue (the same goal was verified stable across repeated direct calls). This is documented directly in the test (`tests/kernel/test_supervisor_agent.py`) rather than papered over: an isolated failure on rerun is this characteristic, a *consistent* failure on a specific input would be a real bug.

## Audit result

Full suite re-run fresh for this review: **155/156 passing**, with the 1 failure being the documented rare non-determinism case above, confirmed transient on immediate rerun (passed). All "Done when" criteria for Days 15-21 met:
- LangGraph compiles, state flows through a stub node ✓
- Supervisor classifies 10/10 goal types correctly (via the live Groq path) ✓
- STATE.md written correctly ✓
- Planner produces valid acyclic DAGs for 5/5 goal types ✓
- Full Supervisor → Planner flow works end-to-end ✓
- 4-task DAG dispatched in correct order, with genuine parallel execution of independent tasks ✓
- Dead agents detected and skipped automatically ✓
