from datetime import datetime, timedelta, timezone

import redis
import pytest

from core.agent_registry import AgentRegistry
from kernel.heartbeat_monitor import HeartbeatMonitor, OFFLINE_AFTER_SECONDS

TEST_DB = 15


@pytest.fixture
def registry():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield AgentRegistry(redis_client=client)
    client.flushdb()


@pytest.fixture
def monitor(registry):
    return HeartbeatMonitor(registry)


def _set_last_heartbeat(registry, name, seconds_ago):
    timestamp = (datetime.now(timezone.utc) - timedelta(seconds=seconds_ago)).isoformat()
    registry._redis.hset(registry._key(name), "last_heartbeat", timestamp)


def test_marks_stale_agent_offline(registry, monitor):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    _set_last_heartbeat(registry, "coding_agent", seconds_ago=OFFLINE_AFTER_SECONDS + 10)

    newly_offline = monitor.check_all()

    assert newly_offline == ["coding_agent"]
    assert registry.get_agent("coding_agent").status == "OFFLINE"


def test_leaves_fresh_agent_online(registry, monitor):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    _set_last_heartbeat(registry, "coding_agent", seconds_ago=5)

    newly_offline = monitor.check_all()

    assert newly_offline == []
    assert registry.get_agent("coding_agent").status == "IDLE"


def test_does_not_re_report_already_offline_agent(registry, monitor):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    _set_last_heartbeat(registry, "coding_agent", seconds_ago=OFFLINE_AFTER_SECONDS + 10)
    registry.update_status("coding_agent", "OFFLINE")

    newly_offline = monitor.check_all()

    assert newly_offline == []


def test_get_online_agents_excludes_offline(registry, monitor):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="m", version="1.0.0")
    _set_last_heartbeat(registry, "research_agent", seconds_ago=OFFLINE_AFTER_SECONDS + 10)

    monitor.check_all()
    online = {record.name for record in monitor.get_online_agents()}

    assert online == {"coding_agent"}


def test_mixed_batch_only_marks_the_stale_ones(registry, monitor):
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="m", version="1.0.0")
    registry.register(name="security_agent", capabilities=["SECURITY"], model="m", version="1.0.0")
    _set_last_heartbeat(registry, "coding_agent", seconds_ago=5)
    _set_last_heartbeat(registry, "research_agent", seconds_ago=OFFLINE_AFTER_SECONDS + 10)
    _set_last_heartbeat(registry, "security_agent", seconds_ago=OFFLINE_AFTER_SECONDS + 100)

    newly_offline = monitor.check_all()

    assert set(newly_offline) == {"research_agent", "security_agent"}
    assert registry.get_agent("coding_agent").status == "IDLE"
