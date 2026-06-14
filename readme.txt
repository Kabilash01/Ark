# ARK OS

Privacy-first, self-hosted multi-agent operating system.
**Phase 1**: Production-grade conversational FastAPI backend with NIM → Ollama automatic fallback.

## Architecture

```
backend/
├── api/routes/         # chat, health, metrics, providers endpoints
├── config/             # pydantic-settings Settings
├── memory/             # in-memory session manager (Phase 2: PostgreSQL)
├── models/             # Pydantic v2 schemas
├── providers/          # BaseLLMProvider ABC, NimProvider, OllamaProvider, LLMManager
├── services/           # ChatService (orchestration layer)
├── utils/              # Loguru logging setup
├── tests/
└── main.py             # FastAPI app, lifespan, middleware
```

## Quick Start (local dev)

```bash
# 1. Install uv (one-time)
conda run -n ark pip install uv

# 2. Install dependencies
conda run -n ark uv pip install -e ".[dev]"

# 3. Configure environment
cp .env.example .env
# Edit .env — add NIM_API_KEY if you have one; Ollama works out of the box

# 4. Run dev server
conda run -n ark uvicorn backend.main:app --reload
```

- API: http://localhost:8000
- Interactive docs: http://localhost:8000/docs

## Docker

```bash
cp .env.example .env
docker compose up --build
```

## API Endpoints

| Method | Path         | Description                        |
|--------|--------------|------------------------------------|
| POST   | `/chat`      | Chat (supports streaming via SSE)  |
| GET    | `/health`    | Health check + active provider     |
| GET    | `/providers` | Provider status list               |
| GET    | `/metrics`   | Request counts, provider usage     |

### Chat request

```json
{
  "message": "What is 2+2?",
  "session_id": "optional-uuid",
  "temperature": 0.7,
  "max_tokens": 2048,
  "stream": false
}
```

### Streaming

```bash
curl -N http://localhost:8000/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Count to 5.", "stream": true}'
```

Returns SSE lines: `data: <token>\n\n` … `data: [DONE]\n\n`

## LLM Providers

| Provider | Priority | Config vars                              |
|----------|----------|------------------------------------------|
| NIM      | Primary  | `NIM_API_KEY`, `NIM_BASE_URL`, `NIM_MODEL` |
| Ollama   | Fallback | `OLLAMA_BASE_URL`, `OLLAMA_MODEL`         |

NIM is skipped automatically when `NIM_API_KEY` is empty — Ollama takes over with zero configuration.

## Testing

```bash
conda run -n ark pytest -v
```

## Phase 2 (coming)

PostgreSQL · Redis · Qdrant · LangGraph · Planner Agent · Memory Agent · Research Agent
