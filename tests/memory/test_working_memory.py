import time

import redis
import pytest

from memory.working_memory import WorkingMemory

TEST_DB = 15


@pytest.fixture
def wm():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield WorkingMemory(redis_client=client)
    client.flushdb()


def test_set_and_get_round_trip(wm):
    wm.set("task_1", "goal", "research transformers")
    assert wm.get("task_1", "goal") == "research transformers"


def test_get_missing_key_returns_none(wm):
    assert wm.get("task_1", "nonexistent") is None


def test_multi_type_storage(wm):
    wm.set("task_1", "a_string", "hello")
    wm.set("task_1", "an_int", 42)
    wm.set("task_1", "a_float", 3.14)
    wm.set("task_1", "a_bool", True)
    wm.set("task_1", "a_list", [1, 2, 3])
    wm.set("task_1", "a_dict", {"nested": {"value": 1}})

    assert wm.get("task_1", "a_string") == "hello"
    assert wm.get("task_1", "an_int") == 42
    assert wm.get("task_1", "a_float") == 3.14
    assert wm.get("task_1", "a_bool") is True
    assert wm.get("task_1", "a_list") == [1, 2, 3]
    assert wm.get("task_1", "a_dict") == {"nested": {"value": 1}}


def test_get_all_returns_every_key_for_task(wm):
    wm.set("task_1", "goal", "research transformers")
    wm.set("task_1", "step", 2)
    assert wm.get_all("task_1") == {"goal": "research transformers", "step": 2}


def test_get_all_empty_when_no_keys(wm):
    assert wm.get_all("task_1") == {}


def test_delete_removes_key(wm):
    wm.set("task_1", "goal", "x")
    wm.delete("task_1", "goal")
    assert wm.get("task_1", "goal") is None


def test_exists(wm):
    wm.set("task_1", "goal", "x")
    assert wm.exists("task_1", "goal") is True
    assert wm.exists("task_1", "nonexistent") is False


def test_ttl_expiry(wm):
    wm.set("task_1", "goal", "x", ttl=1)
    assert wm.exists("task_1", "goal") is True
    time.sleep(1.5)
    assert wm.exists("task_1", "goal") is False
    assert wm.get("task_1", "goal") is None


def test_clear_task_isolation(wm):
    wm.set("task_1", "goal", "task 1 goal")
    wm.set("task_1", "step", 1)
    wm.set("task_2", "goal", "task 2 goal")

    wm.clear_task("task_1")

    assert wm.get_all("task_1") == {}
    assert wm.get("task_2", "goal") == "task 2 goal"
