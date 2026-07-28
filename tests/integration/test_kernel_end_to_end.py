import redis
import pytest
from qdrant_client import QdrantClient
from sentence_transformers import SentenceTransformer

from core.agent_registry import AgentRegistry
from core.message_bus import ARKMessageBus
from kernel.router import Router
from kernel.supervisor_agent import SupervisorAgent
from memory.episodic_memory import EpisodicMemory
from memory.memory_agent import MemoryAgent
from memory.procedural_memory import ProceduralMemory
from memory.semantic_memory import SemanticMemory
from memory.working_memory import WorkingMemory

TEST_DB = 15
TEST_COLLECTION = "test_ark_kernel_e2e"


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


@pytest.mark.live_llm
def test_research_quantum_computing_end_to_end(tmp_path, redis_client, qdrant_client, encoder):
    """Week 3 review trace from the plan: 'Research quantum computing'
    -> Supervisor -> Planner -> DAG -> Router -> (Research stub).

    Every component is real except the actual work a dispatched task does
    (Research/Coding/Evaluation agents aren't built until Days 22-26) --
    that's stubbed by dispatch_fn, exactly as the plan's trace says
    "(Research stub)" for this week's checkpoint.
    """
    memory_agent = MemoryAgent(
        working=WorkingMemory(redis_client=redis_client),
        episodic=EpisodicMemory(db_path=tmp_path / "episodic.db"),
        semantic=SemanticMemory(client=qdrant_client, encoder=encoder, collection_name=TEST_COLLECTION),
        procedural=ProceduralMemory(storage_dir=tmp_path / "procedural", encoder=encoder),
    )
    message_bus = ARKMessageBus(redis_client=redis_client)
    registry = AgentRegistry(redis_client=redis_client)
    for name, capabilities in [
        ("research_agent", ["RESEARCH"]),
        ("coding_agent", ["CODE"]),
        ("evaluation_agent", ["EVALUATE"]),
        ("memory_agent", ["STORE"]),
    ]:
        registry.register(name=name, capabilities=capabilities, model="qwen2.5:7b", version="1.0.0")

    supervisor = SupervisorAgent(memory_agent=memory_agent, message_bus=message_bus)

    # 1. Supervisor -> Planner: classify + produce a task DAG
    plan_result = supervisor.handle_goal(task_id="e2e_1", goal="Research quantum computing")
    dag = plan_result["dag"]
    assert len(dag["tasks"]) >= 1

    # 2. Router dispatches the DAG in dependency order, against the live
    #    Agent Registry (skips anything OFFLINE), calling a Research stub
    #    for the actual per-task work.
    dispatched = []

    def research_stub(task: dict) -> dict:
        dispatched.append(task["task_id"])
        return {"status": "DONE", "output": f"stub output for {task['agent']}: {task['instruction'][:40]}"}

    router = Router()
    dispatch_results = router.dispatch(dag, research_stub, registry=registry)

    # Every task in the DAG was actually dispatched (all agents online) and
    # in an order the Router's own topological layering guarantees is valid.
    assert set(dispatched) == {task["task_id"] for task in dag["tasks"]}
    assert all(result["status"] == "DONE" for result in dispatch_results.values())

    # 3. The full trace is visible in the shared audit log.
    audit_events = [entry["event"] for entry in message_bus.get_audit_log()]
    assert "supervisor_classified" in audit_events
    assert "planner_done" in audit_events
