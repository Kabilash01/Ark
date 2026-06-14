import json
import time
from typing import AsyncIterator

import httpx
from loguru import logger

from backend.config.settings import settings
from backend.models.schemas import LLMResponse
from backend.providers.base import BaseLLMProvider


class OllamaProvider(BaseLLMProvider):

    def __init__(self) -> None:
        self._base_url = settings.ollama_base_url
        self._model = settings.ollama_model
        self._client = httpx.AsyncClient(
            base_url=self._base_url,
            timeout=httpx.Timeout(connect=5.0, read=120.0, write=10.0, pool=5.0),
        )

    @property
    def name(self) -> str:
        return "ollama"

    @property
    def model(self) -> str:
        return self._model

    async def complete(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> LLMResponse:
        start = time.monotonic()
        payload = {
            "model": self._model,
            "messages": messages,
            "stream": False,
            "options": {"temperature": temperature, "num_predict": max_tokens},
        }
        response = await self._client.post("/api/chat", json=payload)
        response.raise_for_status()
        data = response.json()
        latency = time.monotonic() - start
        return LLMResponse(
            content=data["message"]["content"],
            provider=self.name,
            model=self._model,
            latency=latency,
        )

    async def stream(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> AsyncIterator[str]:
        payload = {
            "model": self._model,
            "messages": messages,
            "stream": True,
            "options": {"temperature": temperature, "num_predict": max_tokens},
        }
        async with self._client.stream("POST", "/api/chat", json=payload) as response:
            response.raise_for_status()
            async for line in response.aiter_lines():
                if not line.strip():
                    continue
                try:
                    chunk = json.loads(line)
                except json.JSONDecodeError:
                    logger.warning(f"Ollama: unparseable chunk: {line!r}")
                    continue
                token = chunk.get("message", {}).get("content", "")
                if token:
                    yield token
                if chunk.get("done", False):
                    break

    async def health_check(self) -> bool:
        try:
            response = await self._client.get("/api/tags", timeout=3.0)
            return response.status_code == 200
        except Exception as exc:
            logger.warning(f"Ollama health check failed: {exc}")
            return False

    async def close(self) -> None:
        await self._client.aclose()
