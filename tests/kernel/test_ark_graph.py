from kernel.ark_graph import build_ark_graph
from kernel.ark_state import create_ark_state


def test_graph_compiles():
    graph = build_ark_graph()
    assert graph is not None


def test_state_flows_through_stub_node():
    graph = build_ark_graph()
    initial_state = create_ark_state(task_id="task_1", original_goal="research transformers")

    result = graph.invoke(initial_state)

    assert result["iteration"] == 1
    assert result["status"] == "STUB_PROCESSED"
    # Fields the stub node didn't touch should pass through unchanged.
    assert result["task_id"] == "task_1"
    assert result["original_goal"] == "research transformers"
    assert result["privacy_mode"] is False


def test_privacy_mode_flows_through_state():
    graph = build_ark_graph()
    initial_state = create_ark_state(task_id="task_1", original_goal="confidential research", privacy_mode=True)

    result = graph.invoke(initial_state)

    assert result["privacy_mode"] is True
