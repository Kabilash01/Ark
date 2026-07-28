from memory.episodic_memory import EpisodicMemory
from memory.procedural_memory import ProceduralMemory
from memory.semantic_memory import SemanticMemory
from memory.working_memory import WorkingMemory

_EPISODIC_QUERY_MODES = {
    "recent": "query_recent",
    "by_agent": "query_by_agent",
    "by_intent": "query_by_intent",
    "by_timerange": "query_by_timerange",
    "by_version": "query_by_version",
    "search": "search",
}


class MemoryAgent:
    """Unified read/write interface across all four memory tiers. Every
    other agent talks to memory ONLY through this class -- never touches
    Redis/SQLite/Qdrant/FAISS directly."""

    def __init__(
        self,
        working: WorkingMemory | None = None,
        episodic: EpisodicMemory | None = None,
        semantic: SemanticMemory | None = None,
        procedural: ProceduralMemory | None = None,
    ):
        self.working = working or WorkingMemory()
        self.episodic = episodic or EpisodicMemory()
        self.semantic = semantic or SemanticMemory()
        self.procedural = procedural or ProceduralMemory()

    def store(self, memory_type: str, **kwargs):
        if memory_type == "working":
            return self.working.set(**kwargs)  # task_id, key, value, ttl=
        if memory_type == "episodic":
            return self.episodic.log(**kwargs)  # task_id, agent_name, agent_version, intent, summary, ...
        if memory_type == "semantic":
            return self.semantic.store(**kwargs)  # text, metadata=
        if memory_type == "procedural":
            return self.procedural.add_skill(**kwargs)  # intent, description, steps, source=
        raise ValueError(f"unknown memory_type: {memory_type}")

    def retrieve(self, memory_type: str, **kwargs):
        if memory_type == "working":
            if "key" in kwargs:
                return self.working.get(**kwargs)
            return self.working.get_all(**kwargs)  # task_id
        if memory_type == "episodic":
            mode = kwargs.pop("mode", "recent")
            method_name = _EPISODIC_QUERY_MODES.get(mode)
            if method_name is None:
                raise ValueError(f"unknown episodic retrieve mode: {mode}")
            return getattr(self.episodic, method_name)(**kwargs)
        if memory_type == "semantic":
            return self.semantic.retrieve(**kwargs)  # query, top_k=
        if memory_type == "procedural":
            return self.procedural.retrieve(**kwargs)  # query, top_k=
        raise ValueError(f"unknown memory_type: {memory_type}")

    def forget(self, memory_type: str, **kwargs):
        if memory_type == "working":
            if "key" in kwargs:
                return self.working.delete(**kwargs)  # task_id, key
            return self.working.clear_task(**kwargs)  # task_id
        if memory_type == "semantic":
            return self.semantic.delete(**kwargs)  # point_id
        if memory_type in ("episodic", "procedural"):
            raise ValueError(
                f"{memory_type} memory does not support forget() -- "
                "episodic is an append-only history log, and procedural has no removal API yet"
            )
        raise ValueError(f"unknown memory_type: {memory_type}")

    def inject_context(self, task_id: str, query: str, top_k: int = 5) -> list[dict]:
        """Populates ARKMessage.memory_context for an outgoing task: current
        working-memory state for this task, plus relevant long-term knowledge
        and skills."""
        context = []

        working_state = self.working.get_all(task_id)
        if working_state:
            context.append({"tier": "working", "data": working_state})

        semantic_hits = self.semantic.retrieve(query, top_k=top_k)
        if semantic_hits:
            context.append({"tier": "semantic", "data": semantic_hits})

        procedural_hits = self.procedural.retrieve(query, top_k=top_k)
        if procedural_hits:
            context.append({"tier": "procedural", "data": procedural_hits})

        return context
