import asyncio

from loguru import logger

from backend.config.settings import settings
from backend.models.schemas import SessionMessage


class SessionManager:

    def __init__(self) -> None:
        self._sessions: dict[str, list[SessionMessage]] = {}
        self._lock = asyncio.Lock()
        self._max_history = settings.session_max_history

    async def get_history(self, session_id: str) -> list[SessionMessage]:
        async with self._lock:
            return list(self._sessions.get(session_id, []))

    async def add_message(self, session_id: str, role: str, content: str) -> None:
        async with self._lock:
            if session_id not in self._sessions:
                self._sessions[session_id] = []
            self._sessions[session_id].append(
                SessionMessage(role=role, content=content)
            )
            history = self._sessions[session_id]
            if len(history) > self._max_history:
                trim_to = self._max_history
                # Ensure we start on a user message to preserve role alternation
                if history[-trim_to].role != "user":
                    trim_to -= 1
                self._sessions[session_id] = history[-trim_to:] if trim_to > 0 else []

    async def clear_session(self, session_id: str) -> None:
        async with self._lock:
            self._sessions.pop(session_id, None)
        logger.info(f"Session cleared: {session_id}")

    def session_exists(self, session_id: str) -> bool:
        return session_id in self._sessions

    async def get_active_count(self) -> int:
        async with self._lock:
            return len(self._sessions)
