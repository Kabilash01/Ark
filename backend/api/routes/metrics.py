import time

from fastapi import APIRouter, Request

from backend.models.schemas import MetricsResponse, ProvidersResponse, ProviderInfo

router = APIRouter()


@router.get("/metrics", response_model=MetricsResponse, tags=["metrics"])
async def metrics(request: Request) -> MetricsResponse:
    state = request.app.state
    uptime = time.monotonic() - state.start_time

    return MetricsResponse(
        requests_count=state.llm_manager.requests_count,
        provider_usage=state.llm_manager.usage_stats,
        active_sessions=await state.session_manager.get_active_count(),
        uptime_seconds=uptime,
    )


@router.get("/providers", response_model=ProvidersResponse, tags=["providers"])
async def providers(request: Request) -> ProvidersResponse:
    state = request.app.state
    provider_list: list[ProviderInfo] = []

    for provider in state.llm_manager._providers:
        healthy = await provider.health_check()
        provider_list.append(
            ProviderInfo(
                name=provider.name,
                model=provider.model,
                status="available" if healthy else "unavailable",
                healthy=healthy,
            )
        )

    return ProvidersResponse(providers=provider_list)
