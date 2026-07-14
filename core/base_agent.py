from abc import ABC, abstractmethod
from datetime import datetime, timezone

from core.ark_message import ARKMessage, Status


class BaseAgent(ABC):
    def __init__(self, agent_name: str, model_name: str, version: str = "1.0.0"):
        self.agent_name = agent_name
        self.model_name = model_name
        self.version = version

    # ── Abstract — must implement ───────────────────────────────────
    @abstractmethod
    def run(self, message: ARKMessage) -> ARKMessage:
        pass

    @abstractmethod
    def get_capabilities(self) -> list[str]:
        pass

    # ── Provided defaults — override if needed ──────────────────────
    def register(self, registry) -> None:
        registry.register(
            name=self.agent_name,
            capabilities=self.get_capabilities(),
            model=self.model_name,
            version=self.version,
            status="IDLE",
        )

    def health_check(self) -> dict:
        return {
            "status": "IDLE",
            "current_task": None,
            "version": self.version,
            "last_heartbeat": datetime.now(timezone.utc).isoformat(),
        }

    def validate_message(self, message: ARKMessage) -> bool:
        required = ["task_id", "sender", "receiver", "intent", "payload", "status", "timestamp"]
        return (
            all(f in message for f in required)
            and message["status"] == Status.PENDING
            and message["receiver"] == self.agent_name
        )

    def on_error(self, message: ARKMessage, exception: Exception) -> ARKMessage:
        message["status"] = Status.FAILED
        message["output"] = {
            "error_type": type(exception).__name__,
            "error_message": str(exception),
            "agent": self.agent_name,
            "agent_version": self.version,
        }
        message["quality_score"] = 0.0
        return message

    def _run_with_error_handling(self, message: ARKMessage) -> ARKMessage:
        message["agent_version"] = f"{self.agent_name}@{self.version}"
        if not self.validate_message(message):
            return self.on_error(message, ValueError("Invalid ARKMessage"))
        try:
            return self.run(message)
        except Exception as e:
            return self.on_error(message, e)
