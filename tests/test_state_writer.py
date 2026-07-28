from core.state_writer import StateWriter
from kernel.ark_state import create_ark_state


def test_writes_state_file_with_expected_content(tmp_path):
    state_path = tmp_path / "STATE.md"
    writer = StateWriter(path=state_path)
    state = create_ark_state(task_id="ark_1", original_goal="Research transformers")
    state["status"] = "DONE"
    state["iteration"] = 2

    writer.update(state, last_result={"agent": "coding_agent", "task": "implement_demo", "score": 0.84, "version": "coding_agent@1.0.0"})

    content = state_path.read_text()
    assert "Research transformers" in content
    assert "ark_1" in content
    assert "DONE" in content
    assert "coding_agent -> implement_demo" in content
    assert "0.84 (coding_agent@1.0.0)" in content


def test_overwrites_existing_state_file(tmp_path):
    state_path = tmp_path / "STATE.md"
    writer = StateWriter(path=state_path)
    state = create_ark_state(task_id="ark_1", original_goal="first goal")
    writer.update(state, last_result={})
    first_content = state_path.read_text()

    state2 = create_ark_state(task_id="ark_2", original_goal="second goal")
    writer.update(state2, last_result={})
    second_content = state_path.read_text()

    assert "first goal" not in second_content
    assert "second goal" in second_content
    assert first_content != second_content
