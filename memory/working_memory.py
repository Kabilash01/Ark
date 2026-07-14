import json
from typing import Any

import redis


class WorkingMemory:
    """Task-scoped ephemeral memory tier — Redis-backed, TTL'd, cleared per task."""

    def __init__(self, redis_client: redis.Redis | None = None):
        self._redis = redis_client or redis.Redis(host="localhost", port=6379, decode_responses=True)

    def _key(self, task_id: str, key: str) -> str:
        return f"working:{task_id}:{key}"

    def set(self, task_id: str, key: str, value: Any, ttl: int = 3600) -> None:
        self._redis.set(self._key(task_id, key), json.dumps(value), ex=ttl)

    def get(self, task_id: str, key: str) -> Any | None:
        raw = self._redis.get(self._key(task_id, key))
        return json.loads(raw) if raw is not None else None

    def get_all(self, task_id: str) -> dict[str, Any]:
        prefix = self._key(task_id, "")
        result = {}
        for redis_key in self._redis.scan_iter(f"{prefix}*"):
            raw = self._redis.get(redis_key)
            if raw is not None:
                result[redis_key[len(prefix):]] = json.loads(raw)
        return result

    def delete(self, task_id: str, key: str) -> None:
        self._redis.delete(self._key(task_id, key))

    def clear_task(self, task_id: str) -> None:
        keys = list(self._redis.scan_iter(f"{self._key(task_id, '')}*"))
        if keys:
            self._redis.delete(*keys)

    def exists(self, task_id: str, key: str) -> bool:
        return bool(self._redis.exists(self._key(task_id, key)))
