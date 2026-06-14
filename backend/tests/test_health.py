from httpx import AsyncClient


async def test_health_returns_200(async_client: AsyncClient) -> None:
    response = await async_client.get("/health")
    assert response.status_code == 200


async def test_health_response_fields(async_client: AsyncClient) -> None:
    response = await async_client.get("/health")
    data = response.json()
    assert "status" in data
    assert "uptime" in data
    assert "active_provider" in data
    assert "version" in data
    assert data["status"] in ("ok", "degraded")


async def test_health_uptime_positive(async_client: AsyncClient) -> None:
    response = await async_client.get("/health")
    data = response.json()
    assert data["uptime"] >= 0


async def test_health_version_present(async_client: AsyncClient) -> None:
    response = await async_client.get("/health")
    data = response.json()
    assert data["version"] == "0.1.0"
