# Memory Architecture — Week 2 Audit

## The four tiers

| Tier | File | Backing store | Retention | Tested persistence |
|---|---|---|---|---|
| Working | `memory/working_memory.py` | Redis (per-key TTL) | Task duration only | Real TTL expiry (`test_ttl_expiry`, actual sleep past expiry) |
| Episodic | `memory/episodic_memory.py` | SQLite | Persistent | Close + reopen same `.db` file (`test_persists_across_restart`) |
| Semantic | `memory/semantic_memory.py` | Qdrant, `BAAI/bge-large-en-v1.5` embeddings (1024-dim, cosine) | Persistent | Backed by Qdrant's own on-disk storage (Day 3 `qdrant_data` volume) |
| Procedural | `memory/procedural_memory.py` | JSON (`skills.json`) + FAISS (`skills.faiss`) | Persistent | Close + reopen same directory (`test_persistence_across_restart`) |

All four are unified behind `memory/memory_agent.py` (`MemoryAgent`) — `store`, `retrieve`, `forget`, `inject_context`. Every other agent is meant to go through this class, never touch Redis/SQLite/Qdrant/FAISS directly.

`core/message_bus.py` (`ARKMessageBus`) sits alongside memory as the inter-agent transport: per-agent channels (`agent:{name}`), a broadcast channel (`ark:broadcast`), a heartbeat channel (`ark:heartbeat`), and a persistent audit log (`ark:audit_log`, a Redis list).

## Critical test: memory survives a process restart

Per the plan's own requirement for this week, `tests/memory/test_memory_agent.py::test_memory_survives_process_restart` builds one `MemoryAgent`, writes to all four tiers, then builds a **second, independent** `MemoryAgent` instance pointed at the same backing stores (simulating a fresh process) and confirms every tier still returns the data. No tier holds state only in Python-process memory.

## Audit result

- Full suite: **103/103 passing** (rerun fresh for this review, not carried over from earlier runs)
- Docker services (qdrant, redis, neo4j, mlflow): healthy
- `forget()` is intentionally not supported for episodic (append-only history log by design) or procedural (no removal API was specified) — this raises a clear `ValueError` rather than silently no-op'ing.
- `inject_context()` pulls working-memory state for the task plus semantically-relevant semantic + procedural hits; it omits tiers with no matches rather than returning empty placeholders.

**Every persistence test passes. Proceeding to Week 3.**
