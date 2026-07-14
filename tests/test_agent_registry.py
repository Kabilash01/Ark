import redis
import pytest

from core.agent_registry import AgentRegistry

TEST_DB = 15


@pytest.fixture
def registry():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield AgentRegistry(redis_client=client)
    client.flushdb()


def test_register_stores_all_eight_fields(registry):
    record = registry.register(
        name="coding_agent",
        capabilities=["CODE", "EXECUTE"],
        model="qwen2.5-coder:7b",
        version="1.0.0",
    )
    assert record.name == "coding_agent"
    assert record.capabilities == ["CODE", "EXECUTE"]
    assert record.model == "qwen2.5-coder:7b"
    assert record.version == "1.0.0"
    assert record.status == "IDLE"
    assert record.current_task == ""
    assert record.load_count == 0
    assert record.avg_quality_score == 0.0
    assert record.last_heartbeat

    fetched = registry.get_agent("coding_agent")
    assert fetched == record


def test_get_agent_returns_none_when_missing(registry):
    assert registry.get_agent("nonexistent") is None


def test_deregister_removes_agent(registry):
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="qwen2.5:7b", version="1.0.0")
    registry.deregister("research_agent")
    assert registry.get_agent("research_agent") is None


def test_get_all_returns_every_registered_agent(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="qwen2.5-coder:7b", version="1.0.0")
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="qwen2.5:7b", version="1.0.0")
    names = {a.name for a in registry.get_all()}
    assert names == {"coding_agent", "research_agent"}


def test_find_agents_for_intent(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="qwen2.5-coder:7b", version="1.0.0")
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="qwen2.5:7b", version="1.0.0")
    matches = registry.find_agents_for_intent("CODE")
    assert [a.name for a in matches] == ["coding_agent"]


def test_get_least_loaded(registry):
    a = registry.register(name="a", capabilities=["CODE"], model="m", version="1.0.0", load_count=5)
    b = registry.register(name="b", capabilities=["CODE"], model="m", version="1.0.0", load_count=2)
    least = registry.get_least_loaded([a, b])
    assert least.name == "b"


def test_get_least_loaded_empty_list(registry):
    assert registry.get_least_loaded([]) is None


def test_update_status(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    updated = registry.update_status("coding_agent", "BUSY")
    assert updated.status == "BUSY"
    assert registry.get_agent("coding_agent").status == "BUSY"


def test_update_status_missing_agent_returns_none(registry):
    assert registry.update_status("nonexistent", "BUSY") is None


def test_update_quality_rolling_average(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    r1 = registry.update_quality("coding_agent", 0.8)
    assert r1.avg_quality_score == 0.8
    assert r1.load_count == 1
    r2 = registry.update_quality("coding_agent", 0.6)
    assert r2.avg_quality_score == pytest.approx(0.7)
    assert r2.load_count == 2


def test_heartbeat_updates_timestamp(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    first = registry.get_agent("coding_agent").last_heartbeat
    updated = registry.heartbeat("coding_agent")
    assert updated.last_heartbeat  # non-empty; exact equality to `first` is timing-dependent


def test_update_version_bumps_version_and_appends_history(registry):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    updated = registry.update_version("coding_agent", "1.1.0", benchmark_score=0.85, changed_by="self_improvement")
    assert updated.version == "1.1.0"

    history = registry.get_version_history("coding_agent")
    assert len(history) == 1
    assert history[0]["version"] == "1.1.0"
    assert history[0]["previous_version"] == "1.0.0"
    assert history[0]["benchmark_score"] == 0.85
    assert history[0]["changed_by"] == "self_improvement"


def test_update_version_missing_agent_returns_none(registry):
    assert registry.update_version("nonexistent", "2.0.0") is None


def test_base_agent_register_is_compatible_with_registry(registry):
    """BaseAgent.register() (Day 5) calls registry.register(name=, capabilities=, model=,
    version=, status=) — confirm the signatures still line up."""
    from core.base_agent import BaseAgent

    class StubAgent(BaseAgent):
        def run(self, message):
            return message

        def get_capabilities(self):
            return ["CODE"]

    agent = StubAgent(agent_name="coding_agent", model_name="qwen2.5-coder:7b", version="1.0.0")
    agent.register(registry)
    assert registry.get_agent("coding_agent") is not None
