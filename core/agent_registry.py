import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone

import redis


@dataclass
class AgentRecord:
    name: str
    capabilities: list[str]
    model: str
    version: str
    status: str = "IDLE"
    current_task: str = ""
    last_heartbeat: str = ""
    load_count: int = 0
    avg_quality_score: float = 0.0

    def __post_init__(self):
        if not self.last_heartbeat:
            self.last_heartbeat = datetime.now(timezone.utc).isoformat()


class AgentRegistry:
    """Live catalog of agents, backed by a Redis hash per agent."""

    def __init__(self, redis_client: redis.Redis | None = None):
        self._redis = redis_client or redis.Redis(host="localhost", port=6379, decode_responses=True)

    def _key(self, name: str) -> str:
        return f"registry:{name}"

    def _version_key(self, name: str) -> str:
        return f"registry:version_history:{name}"

    def _to_fields(self, record: AgentRecord) -> dict:
        fields = asdict(record)
        fields["capabilities"] = json.dumps(fields["capabilities"])
        return {k: str(v) for k, v in fields.items()}

    def _from_fields(self, fields: dict) -> AgentRecord:
        return AgentRecord(
            name=fields["name"],
            capabilities=json.loads(fields["capabilities"]),
            model=fields["model"],
            version=fields["version"],
            status=fields["status"],
            current_task=fields["current_task"],
            last_heartbeat=fields["last_heartbeat"],
            load_count=int(fields["load_count"]),
            avg_quality_score=float(fields["avg_quality_score"]),
        )

    def register(
        self,
        name: str,
        capabilities: list[str],
        model: str,
        version: str,
        status: str = "IDLE",
        current_task: str = "",
        load_count: int = 0,
        avg_quality_score: float = 0.0,
    ) -> AgentRecord:
        record = AgentRecord(
            name=name,
            capabilities=capabilities,
            model=model,
            version=version,
            status=status,
            current_task=current_task,
            load_count=load_count,
            avg_quality_score=avg_quality_score,
        )
        self._redis.hset(self._key(name), mapping=self._to_fields(record))
        return record

    def deregister(self, name: str) -> None:
        self._redis.delete(self._key(name))

    def get_agent(self, name: str) -> AgentRecord | None:
        fields = self._redis.hgetall(self._key(name))
        return self._from_fields(fields) if fields else None

    def get_all(self) -> list[AgentRecord]:
        agents = []
        for key in self._redis.scan_iter("registry:*"):
            if key.startswith("registry:version_history:"):
                continue
            fields = self._redis.hgetall(key)
            if fields:
                agents.append(self._from_fields(fields))
        return agents

    def find_agents_for_intent(self, intent: str) -> list[AgentRecord]:
        return [a for a in self.get_all() if intent in a.capabilities]

    def get_least_loaded(self, agents: list[AgentRecord]) -> AgentRecord | None:
        return min(agents, key=lambda a: a.load_count) if agents else None

    def update_status(self, name: str, status: str) -> AgentRecord | None:
        if self.get_agent(name) is None:
            return None
        self._redis.hset(self._key(name), "status", status)
        return self.get_agent(name)

    def update_version(
        self, name: str, new_version: str, benchmark_score: float | None = None, changed_by: str = "manual"
    ) -> AgentRecord | None:
        record = self.get_agent(name)
        if record is None:
            return None
        previous_version = record.version
        self._redis.hset(self._key(name), "version", new_version)
        history_entry = {
            "version": new_version,
            "previous_version": previous_version,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "benchmark_score": benchmark_score,
            "changed_by": changed_by,
        }
        self._redis.rpush(self._version_key(name), json.dumps(history_entry))
        return self.get_agent(name)

    def get_version_history(self, name: str) -> list[dict]:
        return [json.loads(entry) for entry in self._redis.lrange(self._version_key(name), 0, -1)]

    def update_quality(self, name: str, score: float) -> AgentRecord | None:
        record = self.get_agent(name)
        if record is None:
            return None
        if record.load_count == 0:
            new_avg = score
        else:
            new_avg = ((record.avg_quality_score * record.load_count) + score) / (record.load_count + 1)
        self._redis.hset(
            self._key(name),
            mapping={"avg_quality_score": str(new_avg), "load_count": str(record.load_count + 1)},
        )
        return self.get_agent(name)

    def heartbeat(self, name: str) -> AgentRecord | None:
        if self.get_agent(name) is None:
            return None
        self._redis.hset(self._key(name), "last_heartbeat", datetime.now(timezone.utc).isoformat())
        return self.get_agent(name)
