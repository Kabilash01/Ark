from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from backend.providers.manager import LLMManager, ProviderUnavailableException
from backend.providers.nim_provider import NimProvider
from backend.providers.ollama_provider import OllamaProvider
from backend.tests.conftest import MockProvider


class TestNimProvider:

    async def test_health_check_false_when_key_empty(self) -> None:
        provider = NimProvider()
        provider._api_key = ""
        result = await provider.health_check()
        assert result is False

    async def test_health_check_false_on_connection_error(self) -> None:
        from openai import APIConnectionError

        provider = NimProvider()
        provider._api_key = "fake-key"
        with patch.object(
            provider._client.models, "list", new_callable=AsyncMock
        ) as mock_list:
            mock_list.side_effect = APIConnectionError(request=MagicMock())
            result = await provider.health_check()
        assert result is False


class TestOllamaProvider:

    async def test_health_check_success(self) -> None:
        provider = OllamaProvider()
        mock_response = MagicMock()
        mock_response.status_code = 200
        with patch.object(provider._client, "get", new_callable=AsyncMock) as mock_get:
            mock_get.return_value = mock_response
            result = await provider.health_check()
        assert result is True

    async def test_health_check_false_on_exception(self) -> None:
        provider = OllamaProvider()
        with patch.object(provider._client, "get", new_callable=AsyncMock) as mock_get:
            mock_get.side_effect = Exception("Connection refused")
            result = await provider.health_check()
        assert result is False

    async def test_complete_parses_response(self) -> None:
        provider = OllamaProvider()
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {
            "message": {"role": "assistant", "content": "Hello!"},
            "done": True,
        }
        with patch.object(provider._client, "post", new_callable=AsyncMock) as mock_post:
            mock_post.return_value = mock_response
            result = await provider.complete([{"role": "user", "content": "Hi"}])
        assert result.content == "Hello!"
        assert result.provider == "ollama"


class TestLLMManager:

    async def test_fallback_to_second_provider(self) -> None:
        dead = MockProvider(name="dead", healthy=False)
        alive = MockProvider(name="alive", healthy=True)
        manager = LLMManager([dead, alive])
        result = await manager.complete([{"role": "user", "content": "Hi"}])
        assert result.provider == "alive"

    async def test_raises_when_all_providers_fail(self) -> None:
        dead1 = MockProvider(name="dead1", healthy=False)
        dead2 = MockProvider(name="dead2", healthy=False)
        manager = LLMManager([dead1, dead2])
        with pytest.raises(ProviderUnavailableException):
            await manager.complete([{"role": "user", "content": "Hi"}])

    async def test_usage_stats_incremented(self) -> None:
        provider = MockProvider(name="mock", healthy=True)
        manager = LLMManager([provider])
        await manager.complete([{"role": "user", "content": "Hi"}])
        assert manager.usage_stats["mock"] == 1
        assert manager.requests_count == 1

    async def test_get_active_provider_returns_first_healthy(self) -> None:
        dead = MockProvider(name="dead", healthy=False)
        alive = MockProvider(name="alive", healthy=True)
        manager = LLMManager([dead, alive])
        provider = await manager.get_active_provider()
        assert provider.name == "alive"
