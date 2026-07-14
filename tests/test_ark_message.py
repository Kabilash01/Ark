import pytest

from core.ark_message import Intent, Status, create_ark_message


def test_factory_creates_valid_message():
    msg = create_ark_message(
        sender="planner_agent",
        receiver="coding_agent",
        intent=Intent.CODE,
        payload={"instruction": "write a function"},
    )
    assert msg["sender"] == "planner_agent"
    assert msg["receiver"] == "coding_agent"
    assert msg["intent"] == Intent.CODE
    assert msg["payload"] == {"instruction": "write a function"}
    assert msg["status"] == Status.PENDING
    assert msg["retry_count"] == 0
    assert msg["max_retries"] == 3
    assert msg["quality_score"] == -1.0
    assert msg["output"] == {}
    assert msg["memory_context"] == []
    assert msg["task_id"].startswith("ark_")
    assert msg["timestamp"]
    assert msg["agent_version"] == ""


def test_missing_required_fields_raise_error():
    with pytest.raises(TypeError):
        create_ark_message(sender="planner_agent")


def test_intent_enum_values_accessible():
    expected = {
        "RESEARCH", "CODE", "SEARCH", "EXECUTE", "STORE", "EVALUATE",
        "REFLECT", "DISCOVER", "SUPERVISE", "PLAN", "BROWSE", "SECURITY", "VISION",
    }
    assert {intent.value for intent in Intent} == expected


def test_status_enum_values_accessible():
    expected = {"PENDING", "RUNNING", "DONE", "FAILED", "RETRY"}
    assert {status.value for status in Status} == expected
