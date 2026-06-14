FROM python:3.11-slim

RUN pip install --no-cache-dir uv

WORKDIR /app

# Copy dependency spec first for layer cache efficiency
COPY pyproject.toml ./

# Install all runtime dependencies into system Python
RUN uv pip install --system --no-cache .

COPY backend/ ./backend/

EXPOSE 8000

# Exec form ensures uvicorn is PID 1 and receives SIGTERM for graceful shutdown
CMD ["uvicorn", "backend.main:app", "--host", "0.0.0.0", "--port", "8000"]
