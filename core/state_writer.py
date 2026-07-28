from datetime import datetime, timezone
from pathlib import Path

from kernel.ark_state import ARKState

DEFAULT_STATE_PATH = Path("STATE.md")


class StateWriter:
    """Writes a human-readable snapshot of the current ARK OS state to
    STATE.md at the project root, so a human (or Claude) can read it and
    instantly understand where the system is, without querying the 4-tier
    memory directly. The Supervisor calls update() after every task."""

    def __init__(self, path: Path | str = DEFAULT_STATE_PATH):
        self._path = Path(path)

    def update(self, ark_state: ARKState, last_result: dict) -> None:
        divider = "-" * 37
        lines = [
            "# ARK OS STATE",
            f"Last updated: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}",
            divider,
            f"Current goal:    {ark_state['original_goal']}",
            f"Task ID:         {ark_state['task_id']}",
            f"Status:          {ark_state['status']}",
            f"Iteration:       {ark_state['iteration']}",
            f"Privacy mode:    {ark_state['privacy_mode']}",
            divider,
            f"Last task:       {last_result.get('agent', 'unknown')} -> {last_result.get('task', 'unknown')}",
            f"Last score:      {last_result.get('score', 'n/a')} ({last_result.get('version', 'n/a')})",
        ]
        self._path.write_text("\n".join(lines) + "\n")
