import redis
import pytest
from qdrant_client import QdrantClient

from memory.episodic_memory import EpisodicMemory
from memory.memory_agent import MemoryAgent
from memory.procedural_memory import ProceduralMemory
from memory.semantic_memory import SemanticMemory
from memory.working_memory import WorkingMemory

TEST_DB = 15
TEST_COLLECTION = "test_ark_memory_agent"


@pytest.fixture
def qdrant_client():
    client = QdrantClient(host="localhost", port=6333)
    if client.collection_exists(TEST_COLLECTION):
        client.delete_collection(TEST_COLLECTION)
    yield client
    client.delete_collection(TEST_COLLECTION)


def make_agent(tmp_path, encoder, qdrant_client, db_suffix=""):
    redis_client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    return MemoryAgent(
        working=WorkingMemory(redis_client=redis_client),
        episodic=EpisodicMemory(db_path=tmp_path / f"episodic{db_suffix}.db"),
        semantic=SemanticMemory(client=qdrant_client, encoder=encoder, collection_name=TEST_COLLECTION),
        procedural=ProceduralMemory(storage_dir=tmp_path / f"procedural{db_suffix}", encoder=encoder),
    )


@pytest.fixture
def agent(tmp_path, encoder, qdrant_client):
    redis_client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    redis_client.flushdb()
    yield make_agent(tmp_path, encoder, qdrant_client)
    redis_client.flushdb()


def test_all_four_tiers_accessible(agent):
    assert agent.working is not None
    assert agent.episodic is not None
    assert agent.semantic is not None
    assert agent.procedural is not None


def test_store_and_retrieve_working(agent):
    agent.store("working", task_id="task_1", key="goal", value="research transformers")
    assert agent.retrieve("working", task_id="task_1", key="goal") == "research transformers"
    assert agent.retrieve("working", task_id="task_1") == {"goal": "research transformers"}


def test_store_and_retrieve_episodic(agent):
    agent.store(
        "episodic", task_id="task_1", agent_name="coding_agent", agent_version="1.0.0",
        intent="CODE", summary="Wrote a function", quality_score=0.9,
    )
    recent = agent.retrieve("episodic", mode="recent")
    assert len(recent) == 1
    assert recent[0]["summary"] == "Wrote a function"

    by_agent = agent.retrieve("episodic", mode="by_agent", agent_name="coding_agent")
    assert len(by_agent) == 1

    searched = agent.retrieve("episodic", mode="search", keyword="function")
    assert len(searched) == 1


def test_store_and_retrieve_semantic(agent):
    agent.store("semantic", text="Transformers use self-attention.", metadata={"source": "test"})
    results = agent.retrieve("semantic", query="How does self-attention work?", top_k=1)
    assert len(results) == 1
    assert results[0]["text"] == "Transformers use self-attention."


def test_store_and_retrieve_procedural(agent):
    agent.store("procedural", intent="CODE", description="Write a pytest fixture", steps=["a", "b"])
    results = agent.retrieve("procedural", query="how to write a test fixture", top_k=1)
    assert len(results) == 1
    assert results[0]["intent"] == "CODE"


def test_forget_working_key(agent):
    agent.store("working", task_id="task_1", key="goal", value="x")
    agent.forget("working", task_id="task_1", key="goal")
    assert agent.retrieve("working", task_id="task_1", key="goal") is None


def test_forget_working_clears_whole_task(agent):
    agent.store("working", task_id="task_1", key="a", value=1)
    agent.store("working", task_id="task_1", key="b", value=2)
    agent.forget("working", task_id="task_1")
    assert agent.retrieve("working", task_id="task_1") == {}


def test_forget_semantic_point(agent):
    point_id = agent.store("semantic", text="Delete me")
    agent.forget("semantic", point_id=point_id)
    assert agent.semantic.count() == 0


def test_forget_episodic_not_supported(agent):
    with pytest.raises(ValueError, match="does not support forget"):
        agent.forget("episodic", task_id="task_1")


def test_forget_procedural_not_supported(agent):
    with pytest.raises(ValueError, match="does not support forget"):
        agent.forget("procedural", skill_id="x")


def test_inject_context_combines_tiers(agent):
    agent.store("working", task_id="task_1", key="goal", value="research transformers")
    agent.store("semantic", text="Transformers rely on self-attention mechanisms.")
    agent.store("procedural", intent="RESEARCH", description="How to research transformer architectures", steps=["a"])

    context = agent.inject_context("task_1", query="transformer self-attention research")
    tiers = {entry["tier"] for entry in context}
    assert tiers == {"working", "semantic", "procedural"}


def test_inject_context_omits_empty_tiers(agent):
    context = agent.inject_context("task_with_nothing", query="irrelevant query with no matches")
    assert context == []


def test_unknown_memory_type_raises(agent):
    with pytest.raises(ValueError, match="unknown memory_type"):
        agent.store("nonexistent_tier", x=1)
    with pytest.raises(ValueError, match="unknown memory_type"):
        agent.retrieve("nonexistent_tier")
    with pytest.raises(ValueError, match="unknown memory_type"):
        agent.forget("nonexistent_tier")


def test_memory_survives_process_restart(tmp_path, encoder, qdrant_client):
    """CRITICAL TEST (per the plan): create a MemoryAgent, write to all four
    tiers, then build a brand new MemoryAgent pointed at the same backing
    stores (simulating a fresh process) and confirm every tier still has
    the data -- no in-memory-only state anywhere in the stack."""
    redis_client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    redis_client.flushdb()
    try:
        agent1 = make_agent(tmp_path, encoder, qdrant_client, db_suffix="_restart")
        agent1.store("working", task_id="task_1", key="goal", value="restart-safe")
        agent1.store(
            "episodic", task_id="task_1", agent_name="coding_agent", agent_version="1.0.0",
            intent="CODE", summary="restart-safe episode",
        )
        agent1.store("semantic", text="Restart-safe semantic fact about transformers.")
        skill_id = agent1.store("procedural", intent="CODE", description="Restart-safe skill", steps=["a"])

        agent2 = make_agent(tmp_path, encoder, qdrant_client, db_suffix="_restart")

        assert agent2.retrieve("working", task_id="task_1", key="goal") == "restart-safe"
        assert agent2.retrieve("episodic", mode="recent")[0]["summary"] == "restart-safe episode"
        assert agent2.retrieve("semantic", query="transformers", top_k=1)[0]["text"] == \
            "Restart-safe semantic fact about transformers."
        assert agent2.procedural.get_skill(skill_id)["description"] == "Restart-safe skill"
    finally:
        redis_client.flushdb()
