import redis
import pytest
from qdrant_client import QdrantClient
from sentence_transformers import SentenceTransformer

from core.ark_message import Intent
from core.message_bus import ARKMessageBus
from kernel.supervisor_agent import SupervisorAgent
from memory.episodic_memory import EpisodicMemory
from memory.memory_agent import MemoryAgent
from memory.procedural_memory import ProceduralMemory
from memory.semantic_memory import SemanticMemory
from memory.working_memory import WorkingMemory

TEST_DB = 15
TEST_COLLECTION = "test_ark_supervisor_planner_flow"


@pytest.fixture(scope="module")
def encoder():
    return SentenceTransformer("BAAI/bge-large-en-v1.5", device="cpu")


@pytest.fixture
def redis_client():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield client
    client.flushdb()


@pytest.fixture
def qdrant_client():
    client = QdrantClient(host="localhost", port=6333)
    if client.collection_exists(TEST_COLLECTION):
        client.delete_collection(TEST_COLLECTION)
    yield client
    client.delete_collection(TEST_COLLECTION)


@pytest.fixture
def supervisor(tmp_path, redis_client, qdrant_client, encoder):
    memory_agent = MemoryAgent(
        working=WorkingMemory(redis_client=redis_client),
        episodic=EpisodicMemory(db_path=tmp_path / "episodic.db"),
        semantic=SemanticMemory(client=qdrant_client, encoder=encoder, collection_name=TEST_COLLECTION),
        procedural=ProceduralMemory(storage_dir=tmp_path / "procedural", encoder=encoder),
    )
    message_bus = ARKMessageBus(redis_client=redis_client)
    return SupervisorAgent(memory_agent=memory_agent, message_bus=message_bus)


@pytest.mark.live_llm
def test_full_supervisor_planner_flow_end_to_end(supervisor):
    """Done-when criterion from the plan: 'Full Supervisor -> Planner flow
    end-to-end.' Real Groq classification + planning, real Redis audit
    log, real Memory Agent (all 4 tiers) for inject_context."""
    result = supervisor.handle_goal(
        task_id="integration_1", goal="Research transformers and implement a demo"
    )

    assert result["task_id"] == "integration_1"
    assert result["intent"] in {intent.value for intent in Intent}
    assert len(result["dag"]["tasks"]) >= 1
    for task in result["dag"]["tasks"]:
        assert task["agent"] in {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}

    audit_log = supervisor._message_bus.get_audit_log()
    events = [entry["event"] for entry in audit_log]
    assert events == [
        "supervisor_classify_start",
        "supervisor_classified",
        "planner_start",
        "planner_done",
    ]
    assert all(entry.get("task_id") == "integration_1" for entry in audit_log)


@pytest.mark.live_llm
def test_inject_context_is_called_before_planning(supervisor):
    """Store something in semantic memory first, then confirm handle_goal's
    memory_context actually surfaces it -- proves inject_context runs for
    real before the Planner is invoked, not just that the method exists."""
    supervisor._memory_agent.semantic.store(
        "The vanilla Transformer uses multi-head self-attention and positional encodings.",
        metadata={"source": "test"},
    )

    result = supervisor.handle_goal(task_id="integration_2", goal="Research transformer architectures")

    semantic_hits = next((c["data"] for c in result["memory_context"] if c["tier"] == "semantic"), [])
    assert any("multi-head self-attention" in hit["text"] for hit in semantic_hits)
