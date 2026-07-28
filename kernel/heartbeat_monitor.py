from datetime import datetime, timezone

from core.agent_registry import AgentRegistry

HEARTBEAT_INTERVAL_SECONDS = 30
MISSED_PINGS_THRESHOLD = 3
OFFLINE_AFTER_SECONDS = HEARTBEAT_INTERVAL_SECONDS * MISSED_PINGS_THRESHOLD


class HeartbeatMonitor:
    """Watches Agent Registry heartbeats; marks an agent OFFLINE after it
    misses 3 consecutive 30-second heartbeat pings (90s of silence)."""

    def __init__(self, registry: AgentRegistry, offline_after_seconds: float = OFFLINE_AFTER_SECONDS):
        self._registry = registry
        self._offline_after_seconds = offline_after_seconds

    def check_all(self) -> list[str]:
        """Marks any agent silent for longer than the threshold as OFFLINE.
        Returns the names of agents just marked OFFLINE this pass (already-
        offline agents are left alone and not re-reported)."""
        now = datetime.now(timezone.utc)
        newly_offline = []
        for record in self._registry.get_all():
            if record.status == "OFFLINE":
                continue
            last_seen = datetime.fromisoformat(record.last_heartbeat)
            silence = (now - last_seen).total_seconds()
            if silence > self._offline_after_seconds:
                self._registry.update_status(record.name, "OFFLINE")
                newly_offline.append(record.name)
        return newly_offline

    def get_online_agents(self) -> list:
        return [record for record in self._registry.get_all() if record.status != "OFFLINE"]
