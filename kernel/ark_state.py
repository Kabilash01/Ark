from typing import TypedDict


class ARKState(TypedDict):
    task_id: str
    original_goal: str
    current_plan: dict  # task DAG produced by the Planner; {} until planned
    results: dict  # per-subtask results, keyed by task_id
    active_agents: list[str]
    memory_context: list
    iteration: int
    status: str
    privacy_mode: bool


def create_ark_state(task_id: str, original_goal: str, privacy_mode: bool = False) -> ARKState:
    return ARKState(
        task_id=task_id,
        original_goal=original_goal,
        current_plan={},
        results={},
        active_agents=[],
        memory_context=[],
        iteration=0,
        status="PENDING",
        privacy_mode=privacy_mode,
    )
