import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


class EpisodicMemory:
    """Persistent history of past tasks and outcomes — SQLite-backed."""

    def __init__(self, db_path: str | Path = "data/episodic_memory.db"):
        self._db_path = Path(db_path)
        self._db_path.parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(self._db_path)
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        self._conn.execute(
            """
            CREATE TABLE IF NOT EXISTS episodes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                task_id TEXT NOT NULL,
                agent_name TEXT NOT NULL,
                agent_version TEXT NOT NULL,
                intent TEXT NOT NULL,
                summary TEXT NOT NULL,
                quality_score REAL,
                timestamp TEXT NOT NULL,
                metadata TEXT
            )
            """
        )
        self._conn.commit()

    def log(
        self,
        task_id: str,
        agent_name: str,
        agent_version: str,
        intent: str,
        summary: str,
        quality_score: float | None = None,
        metadata: dict | None = None,
        timestamp: str | None = None,
    ) -> int:
        ts = timestamp or datetime.now(timezone.utc).isoformat()
        cursor = self._conn.execute(
            """
            INSERT INTO episodes
                (task_id, agent_name, agent_version, intent, summary, quality_score, timestamp, metadata)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (task_id, agent_name, agent_version, intent, summary, quality_score, ts, json.dumps(metadata or {})),
        )
        self._conn.commit()
        return cursor.lastrowid

    def _row_to_dict(self, row: sqlite3.Row) -> dict:
        record = dict(row)
        record["metadata"] = json.loads(record["metadata"]) if record["metadata"] else {}
        return record

    def _query(self, where: str, params: tuple, limit: int) -> list[dict]:
        rows = self._conn.execute(
            f"SELECT * FROM episodes WHERE {where} ORDER BY timestamp DESC LIMIT ?",
            (*params, limit),
        ).fetchall()
        return [self._row_to_dict(row) for row in rows]

    def query_by_agent(self, agent_name: str, limit: int = 50) -> list[dict]:
        return self._query("agent_name = ?", (agent_name,), limit)

    def query_by_intent(self, intent: str, limit: int = 50) -> list[dict]:
        return self._query("intent = ?", (intent,), limit)

    def query_by_timerange(self, start: str, end: str, limit: int = 50) -> list[dict]:
        return self._query("timestamp >= ? AND timestamp <= ?", (start, end), limit)

    def query_recent(self, limit: int = 50) -> list[dict]:
        rows = self._conn.execute(
            "SELECT * FROM episodes ORDER BY timestamp DESC LIMIT ?", (limit,)
        ).fetchall()
        return [self._row_to_dict(row) for row in rows]

    def search(self, keyword: str, limit: int = 50) -> list[dict]:
        return self._query("summary LIKE ?", (f"%{keyword}%",), limit)

    def query_by_version(self, agent_name: str, version: str, limit: int = 50) -> list[dict]:
        return self._query("agent_name = ? AND agent_version = ?", (agent_name, version), limit)

    def close(self) -> None:
        self._conn.close()
