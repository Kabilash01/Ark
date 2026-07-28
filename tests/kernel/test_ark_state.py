from kernel.ark_state import create_ark_state


def test_create_ark_state_defaults():
    state = create_ark_state(task_id="task_1", original_goal="research transformers")
    assert state["task_id"] == "task_1"
    assert state["original_goal"] == "research transformers"
    assert state["current_plan"] == {}
    assert state["results"] == {}
    assert state["active_agents"] == []
    assert state["memory_context"] == []
    assert state["iteration"] == 0
    assert state["status"] == "PENDING"
    assert state["privacy_mode"] is False


def test_create_ark_state_privacy_mode():
    state = create_ark_state(task_id="task_1", original_goal="x", privacy_mode=True)
    assert state["privacy_mode"] is True
