from datetime import datetime, timezone
from typing import AsyncIterator

from loguru import logger

from backend.memory.session_manager import SessionManager
from backend.models.schemas import ChatRequest, ChatResponse, LLMResponse
from backend.providers.manager import LLMManager


class ChatService:

    def __init__(
        self, session_manager: SessionManager, llm_manager: LLMManager
    ) -> None:
        self._sessions = session_manager
        self._llm = llm_manager

    async def _build_messages(
        self, session_id: str, user_message: str
    ) -> list[dict[str, str]]:
        history = await self._sessions.get_history(session_id)
        messages = [{"role": msg.role, "content": msg.content} for msg in history]
        messages.append({"role": "user", "content": user_message})
        return messages

    async def chat(self, request: ChatRequest) -> ChatResponse:
        messages = await self._build_messages(request.session_id, request.message)
        await self._sessions.add_message(request.session_id, "user", request.message)

        result: LLMResponse = await self._llm.complete(
            messages,
            temperature=request.temperature,
            max_tokens=request.max_tokens,
        )

        await self._sessions.add_message(
            request.session_id, "assistant", result.content
        )

        logger.info(
            f"Chat completed | session={request.session_id} "
            f"provider={result.provider} latency={result.latency:.3f}s"
        )

        return ChatResponse(
            response=result.content,
            provider=result.provider,
            model=result.model,
            latency=result.latency,
            timestamp=datetime.now(timezone.utc),
            session_id=request.session_id,
        )

    async def stream_chat(self, request: ChatRequest) -> AsyncIterator[str]:
        messages = await self._build_messages(request.session_id, request.message)
        await self._sessions.add_message(request.session_id, "user", request.message)

        full_response: list[str] = []

        async for token in self._llm.stream(
            messages,
            temperature=request.temperature,
            max_tokens=request.max_tokens,
        ):
            full_response.append(token)
            yield f"data: {token}\n\n"

        await self._sessions.add_message(
            request.session_id, "assistant", "".join(full_response)
        )
        yield "data: [DONE]\n\n"
