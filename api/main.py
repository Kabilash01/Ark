"""FastAPI REST + WebSocket interface for ARK OS (Day 36).

Weeks 2-5 (Memory Agent, Supervisor, Planner, Router, Research/Coding/
Evaluation agents) were skipped to reach Week 6 directly, per user request.
Task submission below is real -- it creates and stores valid ARKMessages --
but nothing actually orchestrates them yet, since there's no Supervisor to
consume them, so tasks stay PENDING until cancelled. The /memory routes
return empty results for the same reason. Replace `_InMemoryTaskStore` and
the memory stub handlers with the real kernel components once Weeks 2-5
are backfilled.
"""

import json
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel

from api.events import ConnectionManager
from core.agent_registry import AgentRegistry
from core.ark_message import ARKMessage, Intent, Status, create_ark_message

REPO_ROOT = Path(__file__).resolve().parent.parent
PRIVACY_CONFIG_PATH = REPO_ROOT / "config" / "privacy_config.json"
MEMORY_TIERS = {"working", "episodic", "semantic", "procedural"}


class _InMemoryTaskStore:
    """Placeholder for the task tracking the Memory Agent's episodic tier
    will provide once Week 2 is built."""

    def __init__(self):
        self._tasks: dict[str, ARKMessage] = {}

    def create(self, goal: str, privacy: bool) -> ARKMessage:
        message = create_ark_message(
            sender="user",
            receiver="supervisor_agent",
            intent=Intent.SUPERVISE,
            payload={"goal": goal, "privacy": privacy},
        )
        self._tasks[message["task_id"]] = message
        return message

    def get(self, task_id: str) -> ARKMessage | None:
        return self._tasks.get(task_id)

    def list_recent(self, limit: int = 50) -> list[ARKMessage]:
        return sorted(self._tasks.values(), key=lambda m: m["timestamp"], reverse=True)[:limit]

    def cancel(self, task_id: str) -> ARKMessage:
        message = self._tasks[task_id]
        message["status"] = Status.FAILED
        message["output"] = {"cancelled": True, "reason": "cancelled by user"}
        return message


class TaskRequest(BaseModel):
    goal: str
    privacy: bool = False


class PrivacyConfigRequest(BaseModel):
    privacy_mode: bool
    airgap_mode: bool | None = None


class MemorySearchRequest(BaseModel):
    query: str
    top_k: int = 5


def _load_privacy_config() -> dict:
    with open(PRIVACY_CONFIG_PATH) as f:
        return json.load(f)


def _save_privacy_config(config: dict) -> None:
    with open(PRIVACY_CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=2)


app = FastAPI(title="ARK OS", version="0.1.0")
app.state.task_store = _InMemoryTaskStore()
app.state.agent_registry = AgentRegistry()
app.state.connections = ConnectionManager()


@app.post("/task")
async def submit_task(request: TaskRequest):
    message = app.state.task_store.create(request.goal, request.privacy)
    await app.state.connections.broadcast(
        {"event": "task_created", "task_id": message["task_id"], "timestamp": message["timestamp"]}
    )
    return message


@app.get("/task/{task_id}")
async def get_task(task_id: str):
    message = app.state.task_store.get(task_id)
    if message is None:
        raise HTTPException(status_code=404, detail="task not found")
    return message


@app.get("/tasks")
async def list_tasks(limit: int = 50):
    return app.state.task_store.list_recent(limit)


@app.delete("/task/{task_id}")
async def cancel_task(task_id: str):
    message = app.state.task_store.get(task_id)
    if message is None:
        raise HTTPException(status_code=404, detail="task not found")
    if message["status"] not in (Status.PENDING, Status.RUNNING):
        raise HTTPException(status_code=409, detail="task already in a terminal state")
    cancelled = app.state.task_store.cancel(task_id)
    await app.state.connections.broadcast(
        {"event": "task_cancelled", "task_id": task_id, "timestamp": datetime.now(timezone.utc).isoformat()}
    )
    return cancelled


@app.get("/agents")
async def list_agents():
    return [asdict(record) for record in app.state.agent_registry.get_all()]


@app.get("/agents/{name}/versions")
async def get_agent_versions(name: str):
    if app.state.agent_registry.get_agent(name) is None:
        raise HTTPException(status_code=404, detail="agent not found")
    return app.state.agent_registry.get_version_history(name)


@app.get("/memory/{tier}")
async def get_memory(tier: str):
    if tier not in MEMORY_TIERS:
        raise HTTPException(status_code=400, detail=f"unknown memory tier: {tier}")
    return {"tier": tier, "items": [], "note": "Memory Agent not yet implemented (Week 2 skipped)"}


@app.post("/memory/search")
async def search_memory(request: MemorySearchRequest):
    return {
        "query": request.query,
        "top_k": request.top_k,
        "results": [],
        "note": "Memory Agent not yet implemented (Week 2 skipped)",
    }


@app.get("/config/privacy")
async def get_privacy_config():
    return _load_privacy_config()


@app.post("/config/privacy")
async def set_privacy_config(request: PrivacyConfigRequest):
    config = _load_privacy_config()
    config["privacy_mode"] = request.privacy_mode
    if request.airgap_mode is not None:
        config["airgap_mode"] = request.airgap_mode
    _save_privacy_config(config)
    return config


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await app.state.connections.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        app.state.connections.disconnect(websocket)
