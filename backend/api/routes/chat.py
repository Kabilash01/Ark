from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import StreamingResponse

from backend.models.schemas import ChatRequest, ChatResponse
from backend.providers.manager import ProviderUnavailableException

router = APIRouter()


@router.post("/chat", response_model=ChatResponse, tags=["chat"])
async def chat(request_body: ChatRequest, request: Request):
    chat_service = request.app.state.chat_service

    if request_body.stream:
        return StreamingResponse(
            chat_service.stream_chat(request_body),
            media_type="text/event-stream",
            headers={
                "Cache-Control": "no-cache",
                "X-Accel-Buffering": "no",
                "Connection": "keep-alive",
            },
        )

    try:
        return await chat_service.chat(request_body)
    except ProviderUnavailableException as exc:
        raise HTTPException(status_code=503, detail=str(exc))
