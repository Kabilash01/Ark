import json
import os

import httpx

GROQ_API_URL = "https://api.groq.com/openai/v1/chat/completions"
GROQ_MODEL_PRIMARY = "openai/gpt-oss-120b"
GROQ_MODEL_FALLBACK = "llama-3.3-70b-versatile"
OLLAMA_BASE_URL = "http://localhost:11434"

VALID_AGENTS = {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}

PLAN_SYSTEM_PROMPT = """You are the Planner Agent in ARK OS. Break the user's goal into a \
directed acyclic graph (DAG) of subtasks.

Available agents (use exactly these names):
- research_agent: literature search, reading papers, synthesizing findings
- coding_agent: writing, testing, and debugging code
- evaluation_agent: scoring/judging the quality of a completed subtask's output
- memory_agent: storing or retrieving information from persistent memory

Respond with ONLY valid JSON in this exact shape, no other text:
{
  "tasks": [
    {
      "task_id": "t1",
      "agent": "<one of the agent names above>",
      "intent": "<one of RESEARCH, CODE, SEARCH, EXECUTE, STORE, EVALUATE, REFLECT, DISCOVER>",
      "instruction": "<specific instruction for this subtask>",
      "depends_on": ["<task_id>", ...],
      "parallel_with": ["<task_id>", ...]
    }
  ]
}

Rules:
- task_id values must be unique strings like "t1", "t2", ...
- depends_on lists task_ids that must complete before this task starts.
- parallel_with lists task_ids that can run at the same time as this task
  (only tasks with no dependency relationship to each other).
- The graph must be acyclic -- no task may depend on itself, directly or indirectly.
- Keep the plan focused: most goals need 2-5 subtasks.
"""


class DAGValidationError(ValueError):
    pass


class PlannerAgent:
    """Decomposes a goal into a task DAG. Same Groq gpt-oss-120b ->
    llama-3.3-70b-versatile -> local Ollama fallback chain as
    SupervisorAgent -- see kernel/supervisor_agent.py for why this
    deviates from the plan's literal "claude-sonnet-4-6 -> qwen2.5-72b"."""

    def __init__(
        self,
        groq_api_key: str | None = None,
        local_model: str = "qwen2.5:7b",
        http_client: httpx.Client | None = None,
    ):
        self._groq_api_key = groq_api_key if groq_api_key is not None else os.environ.get("GROQ_API_KEY", "")
        self._local_model = local_model
        self._client = http_client or httpx.Client(timeout=60.0)

    def plan(self, goal: str, privacy_mode: bool = False) -> dict:
        if not privacy_mode and self._groq_api_key:
            for model in (GROQ_MODEL_PRIMARY, GROQ_MODEL_FALLBACK):
                try:
                    dag = self._plan_via_groq(goal, model)
                    self._validate_dag(dag)
                    return dag
                except Exception:
                    continue
        dag = self._plan_via_ollama(goal)
        self._validate_dag(dag)
        return dag

    def _plan_via_groq(self, goal: str, model: str) -> dict:
        response = self._client.post(
            GROQ_API_URL,
            headers={"Authorization": f"Bearer {self._groq_api_key}"},
            json={
                "model": model,
                "messages": [
                    {"role": "system", "content": PLAN_SYSTEM_PROMPT},
                    {"role": "user", "content": goal},
                ],
                "temperature": 0,
                # Generous budget -- gpt-oss-120b spends tokens on internal
                # reasoning before content (see kernel/supervisor_agent.py).
                "max_tokens": 2000,
                "response_format": {"type": "json_object"},
            },
        )
        response.raise_for_status()
        content = response.json()["choices"][0]["message"]["content"]
        return json.loads(content)

    def _plan_via_ollama(self, goal: str) -> dict:
        response = self._client.post(
            f"{OLLAMA_BASE_URL}/api/chat",
            json={
                "model": self._local_model,
                "messages": [
                    {"role": "system", "content": PLAN_SYSTEM_PROMPT},
                    {"role": "user", "content": goal},
                ],
                "stream": False,
                "format": "json",
                "options": {"temperature": 0},
            },
        )
        response.raise_for_status()
        content = response.json()["message"]["content"]
        return json.loads(content)

    def _validate_dag(self, dag: dict) -> None:
        if not isinstance(dag.get("tasks"), list) or not dag["tasks"]:
            raise DAGValidationError("DAG must have a non-empty 'tasks' list")

        task_ids: set[str] = set()
        for task in dag["tasks"]:
            for field in ("task_id", "agent", "intent", "instruction", "depends_on", "parallel_with"):
                if field not in task:
                    raise DAGValidationError(f"task missing required field: {field}")
            if task["task_id"] in task_ids:
                raise DAGValidationError(f"duplicate task_id: {task['task_id']}")
            task_ids.add(task["task_id"])
            if task["agent"] not in VALID_AGENTS:
                raise DAGValidationError(f"unknown agent: {task['agent']}")

        for task in dag["tasks"]:
            for dep in task["depends_on"]:
                if dep == task["task_id"]:
                    raise DAGValidationError(f"task {task['task_id']} depends on itself")
                if dep not in task_ids:
                    raise DAGValidationError(f"task {task['task_id']} depends on unknown task_id: {dep}")

        self._check_acyclic(dag["tasks"], task_ids)

    def _check_acyclic(self, tasks: list[dict], task_ids: set[str]) -> None:
        in_degree = {task_id: 0 for task_id in task_ids}
        adjacency: dict[str, list[str]] = {task_id: [] for task_id in task_ids}
        for task in tasks:
            for dep in task["depends_on"]:
                adjacency[dep].append(task["task_id"])
                in_degree[task["task_id"]] += 1

        queue = [task_id for task_id, degree in in_degree.items() if degree == 0]
        visited = 0
        while queue:
            current = queue.pop()
            visited += 1
            for neighbor in adjacency[current]:
                in_degree[neighbor] -= 1
                if in_degree[neighbor] == 0:
                    queue.append(neighbor)

        if visited != len(task_ids):
            raise DAGValidationError("DAG contains a cycle")
