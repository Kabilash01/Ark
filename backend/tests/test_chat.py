from datetime import datetime, timezone
from unittest.mock import AsyncMock, patch

from httpx import ASGITransport, AsyncClient

from backend.models.schemas import ChatResponse


async def test_chat_non_stream_returns_response(async_client: AsyncClient) -> None:
    mock_response = ChatResponse(
        response="Hello from mock",
        provider="mock",
        model="mock-model",
        latency=0.01,
        timestamp=datetime.now(timezone.utc),
        session_id="test-session",
    )

    with patch(
        "backend.services.chat_service.ChatService.chat",
        new_callable=AsyncMock,
        return_value=mock_response,
    ):
        response = await async_client.post(
            "/chat",
            json={
                "message": "Hello",
                "session_id": "test-session",
                "stream": False,
            },
        )

    assert response.status_code == 200
    data = response.json()
    assert data["response"] == "Hello from mock"
    assert data["provider"] == "mock"
    assert "session_id" in data


async def test_chat_missing_message_returns_422(async_client: AsyncClient) -> None:
    response = await async_client.post("/chat", json={})
    assert response.status_code == 422


async def test_chat_session_id_auto_generated(async_client: AsyncClient) -> None:
    mock_response = ChatResponse(
        response="ok",
        provider="mock",
        model="mock-model",
        latency=0.01,
        timestamp=datetime.now(timezone.utc),
        session_id="auto-uuid",
    )

    with patch(
        "backend.services.chat_service.ChatService.chat",
        new_callable=AsyncMock,
        return_value=mock_response,
    ):
        response = await async_client.post("/chat", json={"message": "test"})

    assert response.status_code == 200
    assert "session_id" in response.json()


async def test_chat_stream_returns_event_stream(async_client: AsyncClient) -> None:
    async def mock_stream_chat(request):
        yield "data: Hello\n\n"
        yield "data: world\n\n"
        yield "data: [DONE]\n\n"

    with patch(
        "backend.services.chat_service.ChatService.stream_chat",
        side_effect=mock_stream_chat,
    ):
        response = await async_client.post(
            "/chat",
            json={"message": "Hi", "stream": True},
        )

    assert response.status_code == 200
    assert "text/event-stream" in response.headers["content-type"]


async def test_providers_endpoint(async_client: AsyncClient) -> None:
    response = await async_client.get("/providers")
    assert response.status_code == 200
    data = response.json()
    assert "providers" in data
    assert isinstance(data["providers"], list)


async def test_metrics_endpoint(async_client: AsyncClient) -> None:
    response = await async_client.get("/metrics")
    assert response.status_code == 200
    data = response.json()
    assert "requests_count" in data
    assert "provider_usage" in data
    assert "active_sessions" in data
    assert "uptime_seconds" in data
