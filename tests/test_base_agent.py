import pytest

from core.ark_message import Intent, Status, create_ark_message
from core.base_agent import BaseAgent


class StubAgent(BaseAgent):
    """Minimal concrete BaseAgent for testing shared behavior."""

    def run(self, message):
        if message["payload"].get("boom"):
            raise RuntimeError("simulated failure")
        message["status"] = Status.DONE
        message["output"] = {"result": "ok"}
        message["quality_score"] = 0.9
        return message

    def get_capabilities(self):
        return [Intent.CODE.value]


class StubRegistry:
    def __init__(self):
        self.registered = None

    def register(self, **kwargs):
        self.registered = kwargs


def make_agent(name="stub_agent", version="1.0.0"):
    return StubAgent(agent_name=name, model_name="qwen2.5-coder:7b", version=version)


def make_message(receiver="stub_agent"):
    return create_ark_message(
        sender="planner_agent",
        receiver=receiver,
        intent=Intent.CODE,
        payload={},
    )


def test_cannot_instantiate_base_agent_directly():
    with pytest.raises(TypeError):
        BaseAgent(agent_name="x", model_name="y")


def test_concrete_subclass_instantiates():
    agent = make_agent()
    assert agent.agent_name == "stub_agent"
    assert agent.model_name == "qwen2.5-coder:7b"
    assert agent.version == "1.0.0"


def test_register_passes_expected_fields_to_registry():
    agent = make_agent()
    registry = StubRegistry()
    agent.register(registry)
    assert registry.registered == {
        "name": "stub_agent",
        "capabilities": [Intent.CODE.value],
        "model": "qwen2.5-coder:7b",
        "version": "1.0.0",
        "status": "IDLE",
    }


def test_health_check_shape():
    agent = make_agent(version="1.2.0")
    health = agent.health_check()
    assert health["status"] == "IDLE"
    assert health["current_task"] is None
    assert health["version"] == "1.2.0"
    assert health["last_heartbeat"]


def test_validate_message_true_for_pending_message_addressed_to_agent():
    agent = make_agent()
    assert agent.validate_message(make_message()) is True


def test_validate_message_false_for_wrong_receiver():
    agent = make_agent()
    assert agent.validate_message(make_message(receiver="other_agent")) is False


def test_validate_message_false_for_missing_required_field():
    agent = make_agent()
    message = make_message()
    del message["timestamp"]
    assert agent.validate_message(message) is False


def test_validate_message_false_when_not_pending():
    agent = make_agent()
    message = make_message()
    message["status"] = Status.DONE
    assert agent.validate_message(message) is False


def test_on_error_sets_failed_status_and_output():
    agent = make_agent()
    message = make_message()
    result = agent.on_error(message, ValueError("bad input"))
    assert result["status"] == Status.FAILED
    assert result["quality_score"] == 0.0
    assert result["output"]["error_type"] == "ValueError"
    assert result["output"]["error_message"] == "bad input"
    assert result["output"]["agent"] == "stub_agent"
    assert result["output"]["agent_version"] == "1.0.0"


def test_run_with_error_handling_success_path():
    agent = make_agent()
    result = agent._run_with_error_handling(make_message())
    assert result["status"] == Status.DONE
    assert result["output"] == {"result": "ok"}
    assert result["agent_version"] == "stub_agent@1.0.0"


def test_run_with_error_handling_invalid_message_returns_failed():
    agent = make_agent()
    result = agent._run_with_error_handling(make_message(receiver="other_agent"))
    assert result["status"] == Status.FAILED
    assert result["output"]["error_type"] == "ValueError"


def test_run_with_error_handling_catches_exception_from_run():
    agent = make_agent()
    message = make_message()
    message["payload"] = {"boom": True}
    result = agent._run_with_error_handling(message)
    assert result["status"] == Status.FAILED
    assert result["output"]["error_type"] == "RuntimeError"
    assert result["output"]["error_message"] == "simulated failure"
