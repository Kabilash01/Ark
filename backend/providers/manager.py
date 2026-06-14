from typing import AsyncIterator

from loguru import logger

from backend.models.schemas import LLMResponse
from backend.providers.base import BaseLLMProvider


class ProviderUnavailableException(Exception):
    pass


class LLMManager:

    def __init__(self, providers: list[BaseLLMProvider]) -> None:
        self._providers = providers
        self._usage: dict[str, int] = {p.name: 0 for p in providers}
        self._requests_count = 0

    async def get_active_provider(self) -> BaseLLMProvider:
        for provider in self._providers:
            try:
                if await provider.health_check():
                    logger.debug(f"Active provider: {provider.name}/{provider.model}")
                    return provider
            except Exception as exc:
                logger.warning(f"Provider {provider.name} health check raised: {exc}")
        raise ProviderUnavailableException("No healthy LLM provider available")

    async def complete(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> LLMResponse:
        self._requests_count += 1
        last_exc: Exception | None = None

        for provider in self._providers:
            try:
                if not await provider.health_check():
                    continue
                logger.info(f"Completing with provider: {provider.name}")
                result = await provider.complete(
                    messages, temperature=temperature, max_tokens=max_tokens
                )
                self._usage[provider.name] = self._usage.get(provider.name, 0) + 1
                logger.info(
                    f"Provider {provider.name} responded in {result.latency:.3f}s"
                )
                return result
            except Exception as exc:
                logger.warning(
                    f"Provider {provider.name} failed, trying next: {exc}"
                )
                last_exc = exc

        raise ProviderUnavailableException(
            f"All providers failed. Last error: {last_exc}"
        )

    async def stream(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> AsyncIterator[str]:
        self._requests_count += 1

        for provider in self._providers:
            try:
                if not await provider.health_check():
                    continue
                logger.info(f"Streaming with provider: {provider.name}")
                self._usage[provider.name] = self._usage.get(provider.name, 0) + 1
                async for token in provider.stream(
                    messages, temperature=temperature, max_tokens=max_tokens
                ):
                    yield token
                return
            except Exception as exc:
                logger.warning(f"Provider {provider.name} stream failed: {exc}")

        raise ProviderUnavailableException("All providers failed during streaming")

    @property
    def active_provider_name(self) -> str | None:
        return self._providers[0].name if self._providers else None

    @property
    def usage_stats(self) -> dict[str, int]:
        return dict(self._usage)

    @property
    def requests_count(self) -> int:
        return self._requests_count
