import json

import redis
import pytest
from fastapi.testclient import TestClient

from api.main import PRIVACY_CONFIG_PATH, _InMemoryTaskStore, app
from core.agent_registry import AgentRegistry

TEST_DB = 15


@pytest.fixture
def client():
    original_privacy_config = PRIVACY_CONFIG_PATH.read_text()

    test_registry_client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    test_registry_client.flushdb()
    app.state.agent_registry = AgentRegistry(redis_client=test_registry_client)
    app.state.task_store = _InMemoryTaskStore()

    with TestClient(app) as test_client:
        yield test_client

    PRIVACY_CONFIG_PATH.write_text(original_privacy_config)
    test_registry_client.flushdb()


def test_submit_and_get_task(client):
    response = client.post("/task", json={"goal": "research transformers"})
    assert response.status_code == 200
    task = response.json()
    assert task["payload"] == {"goal": "research transformers", "privacy": False}
    assert task["status"] == "PENDING"
    assert task["intent"] == "SUPERVISE"

    fetched = client.get(f"/task/{task['task_id']}")
    assert fetched.status_code == 200
    assert fetched.json() == task


def test_submit_task_with_privacy_flag(client):
    response = client.post("/task", json={"goal": "confidential research", "privacy": True})
    assert response.json()["payload"]["privacy"] is True


def test_get_task_404(client):
    assert client.get("/task/nonexistent").status_code == 404


def test_list_tasks_returns_most_recent_first(client):
    first = client.post("/task", json={"goal": "task one"}).json()
    second = client.post("/task", json={"goal": "task two"}).json()
    listed = client.get("/tasks").json()
    assert [t["task_id"] for t in listed] == [second["task_id"], first["task_id"]]


def test_cancel_pending_task(client):
    task = client.post("/task", json={"goal": "cancel me"}).json()
    response = client.delete(f"/task/{task['task_id']}")
    assert response.status_code == 200
    cancelled = response.json()
    assert cancelled["status"] == "FAILED"
    assert cancelled["output"] == {"cancelled": True, "reason": "cancelled by user"}


def test_cancel_missing_task_404(client):
    assert client.delete("/task/nonexistent").status_code == 404


def test_cancel_already_terminal_task_409(client):
    task = client.post("/task", json={"goal": "cancel twice"}).json()
    client.delete(f"/task/{task['task_id']}")
    assert client.delete(f"/task/{task['task_id']}").status_code == 409


def test_list_agents_empty_when_none_registered(client):
    assert client.get("/agents").json() == []


def test_list_agents_returns_registered_agents(client):
    app.state.agent_registry.register(
        name="coding_agent", capabilities=["CODE"], model="qwen2.5-coder:7b", version="1.0.0"
    )
    agents = client.get("/agents").json()
    assert len(agents) == 1
    assert agents[0]["name"] == "coding_agent"


def test_agent_versions_404_for_unknown_agent(client):
    assert client.get("/agents/nonexistent/versions").status_code == 404


def test_agent_versions_returns_history(client):
    app.state.agent_registry.register(
        name="coding_agent", capabilities=["CODE"], model="qwen2.5-coder:7b", version="1.0.0"
    )
    app.state.agent_registry.update_version("coding_agent", "1.1.0", benchmark_score=0.9)
    versions = client.get("/agents/coding_agent/versions").json()
    assert len(versions) == 1
    assert versions[0]["version"] == "1.1.0"


@pytest.mark.parametrize("tier", ["working", "episodic", "semantic", "procedural"])
def test_memory_tiers_return_empty_stub(client, tier):
    response = client.get(f"/memory/{tier}")
    assert response.status_code == 200
    assert response.json()["items"] == []


def test_memory_invalid_tier_400(client):
    assert client.get("/memory/nonexistent").status_code == 400


def test_memory_search_stub(client):
    response = client.post("/memory/search", json={"query": "transformers", "top_k": 3})
    assert response.status_code == 200
    body = response.json()
    assert body["results"] == []
    assert body["query"] == "transformers"


def test_privacy_toggle_persists_to_config_file(client):
    initial = client.get("/config/privacy").json()
    assert initial["privacy_mode"] is False

    response = client.post("/config/privacy", json={"privacy_mode": True})
    assert response.status_code == 200
    assert response.json()["privacy_mode"] is True

    # Confirm it's actually persisted to disk, not just in-memory.
    assert json.loads(PRIVACY_CONFIG_PATH.read_text())["privacy_mode"] is True

    reread = client.get("/config/privacy")
    assert reread.json()["privacy_mode"] is True


def test_privacy_toggle_airgap_optional(client):
    response = client.post("/config/privacy", json={"privacy_mode": True, "airgap_mode": True})
    assert response.json()["airgap_mode"] is True


def test_websocket_receives_task_created_event(client):
    with client.websocket_connect("/ws") as websocket:
        task = client.post("/task", json={"goal": "watch this over websocket"}).json()
        event = websocket.receive_json()
        assert event == {"event": "task_created", "task_id": task["task_id"], "timestamp": task["timestamp"]}
