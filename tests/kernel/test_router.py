import threading
import time

import redis
import pytest

from core.agent_registry import AgentRegistry
from kernel.router import Router

TEST_DB = 15


def make_task(task_id, depends_on=None, agent="coding_agent"):
    return {"task_id": task_id, "agent": agent, "intent": "CODE", "instruction": "x",
            "depends_on": depends_on or [], "parallel_with": []}


@pytest.fixture
def registry():
    client = redis.Redis(host="localhost", port=6379, db=TEST_DB, decode_responses=True)
    client.flushdb()
    yield AgentRegistry(redis_client=client)
    client.flushdb()


def test_topological_layers_linear_chain():
    dag = {"tasks": [make_task("t1"), make_task("t2", ["t1"]), make_task("t3", ["t2"])]}
    router = Router()
    assert router.topological_layers(dag) == [["t1"], ["t2"], ["t3"]]


def test_topological_layers_diamond():
    dag = {
        "tasks": [
            make_task("t1"),
            make_task("t2", ["t1"]),
            make_task("t3", ["t1"]),
            make_task("t4", ["t2", "t3"]),
        ]
    }
    router = Router()
    layers = router.topological_layers(dag)
    assert layers[0] == ["t1"]
    assert set(layers[1]) == {"t2", "t3"}
    assert layers[2] == ["t4"]


def test_topological_layers_independent_tasks_single_layer():
    dag = {"tasks": [make_task("t1"), make_task("t2"), make_task("t3")]}
    router = Router()
    layers = router.topological_layers(dag)
    assert len(layers) == 1
    assert set(layers[0]) == {"t1", "t2", "t3"}


def test_topological_layers_raises_on_cycle():
    dag = {"tasks": [make_task("t1", ["t2"]), make_task("t2", ["t1"])]}
    router = Router()
    with pytest.raises(ValueError, match="cycle"):
        router.topological_layers(dag)


def test_dispatch_respects_dependency_order_4_task_dag():
    """Done-when criterion from the plan: '4-task DAG dispatched in
    correct order.' Diamond shape: t1 -> (t2, t3) -> t4."""
    dag = {
        "tasks": [
            make_task("t1"),
            make_task("t2", ["t1"]),
            make_task("t3", ["t1"]),
            make_task("t4", ["t2", "t3"]),
        ]
    }
    started_order = []
    lock = threading.Lock()

    def dispatch_fn(task):
        with lock:
            started_order.append(task["task_id"])
        time.sleep(0.05)
        return f"{task['task_id']}_done"

    router = Router()
    results = router.dispatch(dag, dispatch_fn)

    assert results == {"t1": "t1_done", "t2": "t2_done", "t3": "t3_done", "t4": "t4_done"}
    assert started_order[0] == "t1"
    assert started_order[-1] == "t4"
    assert set(started_order[1:3]) == {"t2", "t3"}


def test_dispatch_never_starts_a_task_before_its_dependency_finishes():
    """Dependency tracking: task B not dispatched until task A DONE --
    checked by recording actual start/finish timestamps, not just call order."""
    dag = {"tasks": [make_task("t1"), make_task("t2", ["t1"])]}
    events = []
    lock = threading.Lock()

    def dispatch_fn(task):
        with lock:
            events.append((task["task_id"], "start", time.monotonic()))
        time.sleep(0.1)
        with lock:
            events.append((task["task_id"], "finish", time.monotonic()))

    Router().dispatch(dag, dispatch_fn)

    t1_finish = next(t for tid, kind, t in events if tid == "t1" and kind == "finish")
    t2_start = next(t for tid, kind, t in events if tid == "t2" and kind == "start")
    assert t2_start >= t1_finish


def test_dispatch_runs_independent_tasks_in_parallel():
    """Two independent tasks (no dependency relationship) should overlap
    in execution time, not run back-to-back."""
    dag = {"tasks": [make_task("t1"), make_task("t2")]}
    sleep_seconds = 0.3

    def dispatch_fn(task):
        time.sleep(sleep_seconds)
        return task["task_id"]

    start = time.monotonic()
    Router().dispatch(dag, dispatch_fn)
    elapsed = time.monotonic() - start

    # Serial execution would take ~2x sleep_seconds; parallel should be ~1x.
    assert elapsed < sleep_seconds * 1.7


def test_dispatch_single_task_dag():
    dag = {"tasks": [make_task("t1")]}
    results = Router().dispatch(dag, lambda task: task["task_id"])
    assert results == {"t1": "t1"}


def test_dispatch_skips_offline_agent_without_calling_dispatch_fn(registry):
    """Done-when criterion from the plan: 'Dead agents detected and
    skipped automatically.'"""
    registry.register(name="coding_agent", capabilities=["CODE"], model="m", version="1.0.0")
    registry.register(name="research_agent", capabilities=["RESEARCH"], model="m", version="1.0.0")
    registry.update_status("research_agent", "OFFLINE")

    dag = {"tasks": [make_task("t1", agent="coding_agent"), make_task("t2", agent="research_agent")]}
    calls = []

    def dispatch_fn(task):
        calls.append(task["task_id"])
        return "ran"

    results = Router().dispatch(dag, dispatch_fn, registry=registry)

    assert calls == ["t1"]  # dispatch_fn was NEVER called for the offline agent's task
    assert results["t1"] == "ran"
    assert results["t2"] == {"status": "SKIPPED", "reason": "agent research_agent is OFFLINE"}


def test_dispatch_without_registry_ignores_agent_status():
    """No registry passed -> no health check at all (backward compatible
    with Day 19's plain dispatch)."""
    dag = {"tasks": [make_task("t1")]}
    results = Router().dispatch(dag, lambda task: task["task_id"])
    assert results == {"t1": "t1"}
