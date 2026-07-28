import pytest

from kernel.planner_agent import DAGValidationError, PlannerAgent

GOALS = [
    "Write a Python function to compute Fibonacci numbers and test it",
    "Research recent advances in reinforcement learning",
    "Research transformer architectures and then implement a simple working demo",
    "Evaluate the correctness of this existing sorting implementation",
    "Store a summary of what we learned about attention mechanisms in long-term memory",
]


@pytest.mark.live_llm
@pytest.mark.parametrize("goal", GOALS)
def test_produces_valid_acyclic_dag_via_groq(goal):
    """Done-when criterion from the plan: 'Planner produces valid acyclic
    DAGs for 5 goal types.' Hits the real Groq API -- the DAG's structural
    validity (not exact task content, which is inherently variable) is
    what's asserted."""
    planner = PlannerAgent()
    dag = planner.plan(goal)  # raises DAGValidationError if invalid -- the test itself is the assertion
    assert len(dag["tasks"]) >= 1
    for task in dag["tasks"]:
        assert task["agent"] in {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}


def _make_planner_returning(dag: dict) -> PlannerAgent:
    planner = PlannerAgent(groq_api_key="")  # force local path, which we'll monkeypatch anyway
    planner._plan_via_ollama = lambda goal: dag
    return planner


def test_validate_dag_accepts_valid_linear_chain():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "research_agent", "intent": "RESEARCH", "instruction": "x", "depends_on": [], "parallel_with": []},
            {"task_id": "t2", "agent": "coding_agent", "intent": "CODE", "instruction": "y", "depends_on": ["t1"], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    result = planner.plan("irrelevant, ollama path is stubbed")
    assert result == dag


def test_validate_dag_rejects_self_dependency():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "coding_agent", "intent": "CODE", "instruction": "x", "depends_on": ["t1"], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="depends on itself"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_two_node_cycle():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "coding_agent", "intent": "CODE", "instruction": "x", "depends_on": ["t2"], "parallel_with": []},
            {"task_id": "t2", "agent": "evaluation_agent", "intent": "EVALUATE", "instruction": "y", "depends_on": ["t1"], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="cycle"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_unknown_dependency():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "coding_agent", "intent": "CODE", "instruction": "x", "depends_on": ["nonexistent"], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="unknown task_id"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_duplicate_task_id():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "coding_agent", "intent": "CODE", "instruction": "x", "depends_on": [], "parallel_with": []},
            {"task_id": "t1", "agent": "research_agent", "intent": "RESEARCH", "instruction": "y", "depends_on": [], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="duplicate task_id"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_unknown_agent():
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "not_a_real_agent", "intent": "CODE", "instruction": "x", "depends_on": [], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="unknown agent"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_empty_task_list():
    planner = _make_planner_returning({"tasks": []})
    with pytest.raises(DAGValidationError, match="non-empty"):
        planner.plan("irrelevant")


def test_validate_dag_rejects_missing_field():
    dag = {"tasks": [{"task_id": "t1", "agent": "coding_agent", "intent": "CODE", "instruction": "x", "depends_on": []}]}
    planner = _make_planner_returning(dag)
    with pytest.raises(DAGValidationError, match="missing required field"):
        planner.plan("irrelevant")


def test_validate_dag_accepts_larger_diamond_shaped_dag():
    """t1 -> (t2, t3) -> t4 -- a real DAG shape, not just a linear chain."""
    dag = {
        "tasks": [
            {"task_id": "t1", "agent": "research_agent", "intent": "RESEARCH", "instruction": "a", "depends_on": [], "parallel_with": []},
            {"task_id": "t2", "agent": "coding_agent", "intent": "CODE", "instruction": "b", "depends_on": ["t1"], "parallel_with": ["t3"]},
            {"task_id": "t3", "agent": "coding_agent", "intent": "CODE", "instruction": "c", "depends_on": ["t1"], "parallel_with": ["t2"]},
            {"task_id": "t4", "agent": "evaluation_agent", "intent": "EVALUATE", "instruction": "d", "depends_on": ["t2", "t3"], "parallel_with": []},
        ]
    }
    planner = _make_planner_returning(dag)
    result = planner.plan("irrelevant")
    assert result == dag
