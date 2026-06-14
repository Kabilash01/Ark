from __future__ import annotations

import uuid
from datetime import datetime, timezone
from typing import Literal

from pydantic import BaseModel, Field


class ChatRequest(BaseModel):
    message: str
    session_id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    temperature: float = Field(default=0.7, ge=0.0, le=2.0)
    max_tokens: int = Field(default=2048, ge=1, le=8192)
    stream: bool = False


class ChatResponse(BaseModel):
    response: str
    provider: str
    model: str
    latency: float
    timestamp: datetime
    session_id: str


class SessionMessage(BaseModel):
    role: Literal["user", "assistant", "system"]
    content: str
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class LLMResponse(BaseModel):
    content: str
    provider: str
    model: str
    latency: float


class ProviderInfo(BaseModel):
    name: str
    model: str
    status: Literal["available", "unavailable"]
    healthy: bool


class HealthResponse(BaseModel):
    status: Literal["ok", "degraded"]
    uptime: float
    active_provider: str | None
    version: str


class ProvidersResponse(BaseModel):
    providers: list[ProviderInfo]


class MetricsResponse(BaseModel):
    requests_count: int
    provider_usage: dict[str, int]
    active_sessions: int
    uptime_seconds: float
