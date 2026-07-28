from langgraph.graph import END, START, StateGraph

from kernel.ark_state import ARKState


def _stub_node(state: ARKState) -> dict:
    """Placeholder node -- proves the graph compiles and state flows
    through it. Replaced by the real Supervisor node on Day 16."""
    return {"iteration": state["iteration"] + 1, "status": "STUB_PROCESSED"}


def build_ark_graph():
    graph = StateGraph(ARKState)
    graph.add_node("stub", _stub_node)
    graph.add_edge(START, "stub")
    graph.add_edge("stub", END)
    return graph.compile()
