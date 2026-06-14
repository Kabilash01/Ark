import time

from fastapi import APIRouter, Request

from backend.models.schemas import HealthResponse

router = APIRouter()


@router.get("/health", response_model=HealthResponse, tags=["health"])
async def health(request: Request) -> HealthResponse:
    state = request.app.state
    uptime = time.monotonic() - state.start_time

    try:
        provider = await state.llm_manager.get_active_provider()
        active_provider_name = f"{provider.name}/{provider.model}"
        status = "ok"
    except Exception:
        active_provider_name = None
        status = "degraded"

    return HealthResponse(
        status=status,
        uptime=uptime,
        active_provider=active_provider_name,
        version=state.settings.version,
    )
