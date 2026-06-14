from typing import AsyncIterator

import pytest
import pytest_asyncio
from httpx import ASGITransport, AsyncClient

from backend.models.schemas import LLMResponse
from backend.providers.base import BaseLLMProvider


class MockProvider(BaseLLMProvider):

    def __init__(self, name: str = "mock", healthy: bool = True) -> None:
        self._name = name
        self._healthy = healthy
        self._model_name = "mock-model"

    @property
    def name(self) -> str:
        return self._name

    @property
    def model(self) -> str:
        return self._model_name

    async def complete(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> LLMResponse:
        return LLMResponse(
            content="Mock response",
            provider=self._name,
            model=self._model_name,
            latency=0.001,
        )

    async def stream(
        self,
        messages: list[dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 2048,
    ) -> AsyncIterator[str]:
        for token in ["Hello", " world", "!"]:
            yield token

    async def health_check(self) -> bool:
        return self._healthy


@pytest.fixture
def mock_healthy_provider() -> MockProvider:
    return MockProvider(name="mock", healthy=True)


@pytest.fixture
def mock_unhealthy_provider() -> MockProvider:
    return MockProvider(name="mock_dead", healthy=False)


@pytest_asyncio.fixture
async def async_client() -> AsyncIterator[AsyncClient]:
    from backend.main import app, lifespan

    # ASGITransport does not send ASGI lifespan events, so we run the
    # lifespan context manager manually to populate app.state.
    async with lifespan(app):
        async with AsyncClient(
            transport=ASGITransport(app=app),
            base_url="http://test",
        ) as client:
            yield client
