import json
import time

import redis
import pytest

from core.message_bus import ARKMessageBus

TEST_DB = 15


@pytest.fixture
def redis_client():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield client
    client.flushdb()


@pytest.fixture
def bus(redis_client):
    return ARKMessageBus(redis_client=redis_client)


def _next_real_message(pubsub, timeout=2.0):
    """Skip subscribe/unsubscribe confirmations and return the next actual message."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        event = pubsub.get_message(timeout=0.2)
        if event is not None and event["type"] == "message":
            return json.loads(event["data"])
    return None


def test_two_agents_communicate_via_pubsub(bus):
    """Agent A publishes directly to Agent B's channel; Agent B receives it."""
    receiver_pubsub = bus.subscribe("coding_agent")
    _next_real_message(receiver_pubsub, timeout=0.5)  # drain subscribe confirmations

    bus.publish("coding_agent", {"from": "planner_agent", "instruction": "write a function"})

    received = _next_real_message(receiver_pubsub)
    assert received == {"from": "planner_agent", "instruction": "write a function"}
    receiver_pubsub.close()


def test_broadcast_reaches_all_subscribed_agents(bus):
    pubsub_a = bus.subscribe("agent_a")
    pubsub_b = bus.subscribe("agent_b")
    _next_real_message(pubsub_a, timeout=0.5)
    _next_real_message(pubsub_b, timeout=0.5)

    bus.broadcast({"event": "system_shutdown"})

    assert _next_real_message(pubsub_a) == {"event": "system_shutdown"}
    assert _next_real_message(pubsub_b) == {"event": "system_shutdown"}
    pubsub_a.close()
    pubsub_b.close()


def test_agent_channel_isolation(bus):
    """A message published to agent_a's channel should not reach agent_b."""
    pubsub_b = bus.subscribe("agent_b")
    _next_real_message(pubsub_b, timeout=0.5)

    bus.publish("agent_a", {"private": "for agent_a only"})

    assert _next_real_message(pubsub_b, timeout=0.5) is None
    pubsub_b.close()


def test_publish_heartbeat(bus):
    heartbeat_pubsub = bus.subscribe_heartbeat()
    _next_real_message(heartbeat_pubsub, timeout=0.5)

    bus.publish_heartbeat("coding_agent", status="BUSY")

    event = _next_real_message(heartbeat_pubsub)
    assert event["agent_name"] == "coding_agent"
    assert event["status"] == "BUSY"
    assert event["timestamp"]
    heartbeat_pubsub.close()


def test_audit_log_records_and_retrieves_in_order(bus):
    bus.log_to_audit({"event": "task_created", "task_id": "t1"})
    bus.log_to_audit({"event": "task_completed", "task_id": "t1"})

    log = bus.get_audit_log()
    assert [entry["event"] for entry in log] == ["task_created", "task_completed"]
    assert all("timestamp" in entry for entry in log)


def test_audit_log_respects_limit(bus):
    for i in range(5):
        bus.log_to_audit({"event": f"event_{i}"})
    log = bus.get_audit_log(limit=2)
    assert [entry["event"] for entry in log] == ["event_3", "event_4"]
