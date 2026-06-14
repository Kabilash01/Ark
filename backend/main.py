import time
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from loguru import logger

from backend.api.routes import chat, health, metrics
from backend.config.settings import settings
from backend.memory.session_manager import SessionManager
from backend.providers.manager import LLMManager, ProviderUnavailableException
from backend.providers.nim_provider import NimProvider
from backend.providers.ollama_provider import OllamaProvider
from backend.services.chat_service import ChatService
from backend.utils.logging import configure_logging


@asynccontextmanager
async def lifespan(app: FastAPI):
    configure_logging()
    logger.info(f"Starting {settings.app_name} v{settings.version}")

    app.state.settings = settings
    app.state.start_time = time.monotonic()

    nim = NimProvider()
    ollama = OllamaProvider()
    llm_manager = LLMManager([nim, ollama])

    session_manager = SessionManager()
    chat_service = ChatService(session_manager, llm_manager)

    app.state.llm_manager = llm_manager
    app.state.session_manager = session_manager
    app.state.chat_service = chat_service

    logger.info("All services initialized")

    yield

    logger.info("Shutting down ARK OS")
    await ollama.close()
    logger.info("Ollama HTTP client closed")


app = FastAPI(
    title=settings.app_name,
    version=settings.version,
    description="Privacy-first, self-hosted conversational backend — Phase 1",
    lifespan=lifespan,
)


@app.middleware("http")
async def timing_middleware(request: Request, call_next):
    start = time.monotonic()
    response = await call_next(request)
    elapsed = time.monotonic() - start
    response.headers["X-Process-Time"] = f"{elapsed:.4f}"
    logger.debug(
        f"{request.method} {request.url.path} → {response.status_code} ({elapsed:.4f}s)"
    )
    return response


cors_origins = ["*"] if settings.debug else []
app.add_middleware(
    CORSMiddleware,
    allow_origins=cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.exception_handler(ProviderUnavailableException)
async def provider_unavailable_handler(
    request: Request, exc: ProviderUnavailableException
):
    logger.error(f"Provider unavailable: {exc}")
    return JSONResponse(status_code=503, content={"detail": str(exc)})


@app.exception_handler(Exception)
async def generic_exception_handler(request: Request, exc: Exception):
    logger.exception(f"Unhandled exception on {request.method} {request.url.path}")
    return JSONResponse(
        status_code=500,
        content={"detail": "Internal server error"},
    )


app.include_router(chat.router)
app.include_router(health.router)
app.include_router(metrics.router)


@app.get("/", tags=["root"])
async def root():
    return {
        "name": settings.app_name,
        "version": settings.version,
        "docs": "/docs",
        "health": "/health",
        "providers": "/providers",
    }
