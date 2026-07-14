# Week 1 Review

## Checklist (per the plan)

- [x] Hardware detected correctly — `scripts/select-model.py` → 8.0GB VRAM, 30.1GB RAM → **Tier T2**
- [x] `model-library.json` has all 5 tiers — T1, T2, T3, T4, T5
- [x] Docker services all healthy — qdrant, redis, neo4j, mlflow up 6h+, `ark-sandbox` builds and runs
- [x] ARKMessage schema complete — `core/ark_message.py`, tagged `v1.0-agentspec`
- [x] BaseAgent with versioning support — `core/base_agent.py`, `version` field flows through `agent_version` on every message
- [x] Agent Registry with version history — `core/agent_registry.py`, Redis-backed, 30/30 tests passing across ARKMessage + BaseAgent + AgentRegistry

## Notes

- **Does the ARKMessage schema feel complete?** Yes, with one bug found and fixed after the fact (not a schema change): `task_id` only had second-granularity, so two tasks created in the same second collided. Fixed by appending a short uuid suffix in `create_ark_message()` — the frozen fields/enums are untouched.
- **Anything missing from `model-library.json`?** No — matches the plan's tier table exactly, except two model tags were corrected against the live Ollama registry (`gemma4:26b-instruct-qat` and `qwen2-vl:7b` don't exist; using `qwen2.5:7b` and `qwen2.5vl:7b` instead for T2, consistent with PRD v1.1 §12).
- **Which tier did the hardware land on?** T2 (RTX 5060 Laptop, 8GB VRAM).

## Detour and correction

Days 1-6 were built in sequence. After Day 6, an instruction to skip ahead to Week 6 (FastAPI/Dashboard/ARKBench) was given, confirmed, and partially executed — Day 36 (FastAPI backend) was built on stubbed-out Weeks 2-5 foundations. This was then reconsidered, and the decision was made to return to proper sequence starting at Day 7. Day 36's code is left in place (`api/main.py`, `api/events.py`, `config/privacy_config.json`) but on hold, not deleted, in case Week 6 is resumed later once the real Memory Agent/Supervisor/Planner exist to back it.

## Final decision point

**AgentSpec v1.0 is permanently frozen as of this review.** No further changes to `ARKMessage`'s fields or the `Intent`/`Status` enum values without a strong, explicit reason.
