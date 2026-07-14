import uuid
from datetime import datetime, timezone
from enum import Enum
from typing import TypedDict


class Intent(str, Enum):
    RESEARCH = "RESEARCH"
    CODE = "CODE"
    SEARCH = "SEARCH"
    EXECUTE = "EXECUTE"
    STORE = "STORE"
    EVALUATE = "EVALUATE"
    REFLECT = "REFLECT"
    DISCOVER = "DISCOVER"
    SUPERVISE = "SUPERVISE"
    PLAN = "PLAN"
    BROWSE = "BROWSE"
    SECURITY = "SECURITY"
    VISION = "VISION"


class Status(str, Enum):
    PENDING = "PENDING"
    RUNNING = "RUNNING"
    DONE = "DONE"
    FAILED = "FAILED"
    RETRY = "RETRY"


class ARKMessage(TypedDict):
    # ── Routing ──────────────────────────────────────────────────
    task_id: str
    sender: str
    receiver: str
    parent_task_id: str

    # ── Intent and content ──────────────────────────────────────
    intent: str
    payload: dict
    memory_context: list

    # ── Control ──────────────────────────────────────────────────
    priority: int
    status: str
    retry_count: int
    max_retries: int

    # ── Output (populated on completion) ────────────────────────
    output: dict
    quality_score: float  # -1.0 = not yet scored
    timestamp: str  # ISO 8601

    # ── Agent versioning ─────────────────────────────────────────
    agent_version: str  # e.g. "coding_agent@1.2.0"


def create_ark_message(
    sender: str,
    receiver: str,
    intent: str,
    payload: dict,
    priority: int = 5,
    parent_task_id: str = "",
) -> ARKMessage:
    now = datetime.now(timezone.utc)
    return ARKMessage(
        task_id=f"ark_{now.strftime('%Y%m%d_%H%M%S')}_{uuid.uuid4().hex[:6]}",
        sender=sender,
        receiver=receiver,
        parent_task_id=parent_task_id,
        intent=intent,
        payload=payload,
        memory_context=[],
        priority=priority,
        status=Status.PENDING,
        retry_count=0,
        max_retries=3,
        output={},
        quality_score=-1.0,
        timestamp=now.isoformat(),
        agent_version="",
    )
