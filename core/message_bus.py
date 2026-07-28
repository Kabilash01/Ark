import json
from datetime import datetime, timezone

import redis


class ARKMessageBus:
    """Redis Pub/Sub-based inter-agent communication."""

    def __init__(self, redis_client: redis.Redis | None = None):
        self._redis = redis_client or redis.Redis(host="localhost", port=6379, decode_responses=True)

    def _agent_channel(self, agent_name: str) -> str:
        return f"agent:{agent_name}"

    def publish(self, agent_name: str, message: dict) -> int:
        """Send a message to a specific agent's channel. Returns the number
        of subscribers that received it."""
        return self._redis.publish(self._agent_channel(agent_name), json.dumps(message))

    def broadcast(self, message: dict) -> int:
        """Send a message to every agent listening on ark:broadcast."""
        return self._redis.publish("ark:broadcast", json.dumps(message))

    def subscribe(self, agent_name: str):
        """Returns a redis PubSub object subscribed to this agent's own
        channel plus the broadcast channel. Caller drives it via
        pubsub.get_message(...)."""
        pubsub = self._redis.pubsub()
        pubsub.subscribe(self._agent_channel(agent_name), "ark:broadcast")
        return pubsub

    def subscribe_heartbeat(self):
        pubsub = self._redis.pubsub()
        pubsub.subscribe("ark:heartbeat")
        return pubsub

    def publish_heartbeat(self, agent_name: str, status: str = "IDLE") -> int:
        payload = {
            "agent_name": agent_name,
            "status": status,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        return self._redis.publish("ark:heartbeat", json.dumps(payload))

    def log_to_audit(self, event: dict) -> None:
        entry = {**event, "timestamp": event.get("timestamp") or datetime.now(timezone.utc).isoformat()}
        self._redis.rpush("ark:audit_log", json.dumps(entry))

    def get_audit_log(self, limit: int = 100) -> list[dict]:
        raw_entries = self._redis.lrange("ark:audit_log", -limit, -1)
        return [json.loads(entry) for entry in raw_entries]
