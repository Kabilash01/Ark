import time
from typing import AsyncIterator

from loguru import logger
from openai import AsyncOpenAI, APIConnectionError, AuthenticationError

from backend.config.settings import settings
from backend.models.schemas import LLMResponse
from backend.providers.base import BaseLLMProvider


class NimProvider(BaseLLMProvider):

    def __init__(self) -> None:
        self._model = settings.nim_model
        self._api_key = settings.nim_api_key
        # SDK requires non-empty string; placeholder is never used when key is empty
        # because health_check() returns False first
        self._client = AsyncOpenAI(
            api_key=self._api_key or "placeholder",
            base_url=settings.nim_base_url,
        )

    @property
    def name(self) -> str:
        return "nim"

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
        completion = await self._client.chat.completions.create(
            model=self._model,
            messages=messages,
            temperature=temperature,
            max_tokens=max_tokens,
            stream=False,
        )
        latency = time.monotonic() - start
        content = completion.choices[0].message.content or ""
        return LLMResponse(
            content=content,
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
        response_stream = await self._client.chat.completions.create(
            model=self._model,
            messages=messages,
            temperature=temperature,
            max_tokens=max_tokens,
            stream=True,
        )
        async for chunk in response_stream:
            delta = chunk.choices[0].delta.content
            if delta:
                yield delta

    async def health_check(self) -> bool:
        if not self._api_key:
            logger.debug("NIM health check skipped: no API key configured")
            return False
        try:
            models = await self._client.models.list()
            return len(models.data) > 0
        except (APIConnectionError, AuthenticationError) as exc:
            logger.warning(f"NIM health check failed: {exc}")
            return False
        except Exception as exc:
            logger.warning(f"NIM health check unexpected error: {exc}")
            return False
