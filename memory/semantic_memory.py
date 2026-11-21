import uuid
from typing import Any

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, FieldCondition, Filter, MatchValue, PointStruct, VectorParams
from sentence_transformers import SentenceTransformer

COLLECTION_NAME = "ark_semantic_memory"
EMBEDDING_MODEL = "BAAI/bge-large-en-v1.5"
EMBEDDING_DIM = 1024


class SemanticMemory:
    """Long-term factual/knowledge memory — Qdrant-backed, cosine similarity retrieval."""

    def __init__(
        self,
        client: QdrantClient | None = None,
        encoder: SentenceTransformer | None = None,
        collection_name: str = COLLECTION_NAME,
    ):
        self._client = client or QdrantClient(host="localhost", port=6333)
        # CPU by default -- this shares the 8GB VRAM budget with the local
        # LLMs (Ollama), which need it far more than a short-text encoder does.
        self._encoder = encoder or SentenceTransformer(EMBEDDING_MODEL, device="cpu")
        self._collection_name = collection_name
        self._ensure_collection()

    def _ensure_collection(self) -> None:
        if not self._client.collection_exists(self._collection_name):
            self._client.create_collection(
                collection_name=self._collection_name,
                vectors_config=VectorParams(size=EMBEDDING_DIM, distance=Distance.COSINE),
            )

    def store(self, text: str, metadata: dict[str, Any] | None = None, *, point_id: str | None = None) -> str:
        """Store a vector, optionally replacing a record with a stable UUID."""
        if point_id is None:
            point_id = str(uuid.uuid4())
        vector = self._encoder.encode(text).tolist()
        payload = {"text": text, **(metadata or {})}
        self._client.upsert(
            collection_name=self._collection_name,
            points=[PointStruct(id=point_id, vector=vector, payload=payload)],
        )
        return point_id

    def retrieve(self, query: str, top_k: int = 5) -> list[dict]:
        vector = self._encoder.encode(query).tolist()
        results = self._client.query_points(
            collection_name=self._collection_name, query=vector, limit=top_k
        ).points
        return [self._to_dict(point) for point in results]

    def retrieve_by_metadata(self, metadata: dict[str, Any], top_k: int = 50) -> list[dict]:
        conditions = [FieldCondition(key=key, match=MatchValue(value=value)) for key, value in metadata.items()]
        points, _ = self._client.scroll(
            collection_name=self._collection_name,
            scroll_filter=Filter(must=conditions),
            limit=top_k,
        )
        return [self._to_dict(point) for point in points]

    def delete(self, point_id: str) -> None:
        self._client.delete(collection_name=self._collection_name, points_selector=[point_id])

    def count(self) -> int:
        return self._client.count(collection_name=self._collection_name).count

    def _to_dict(self, point) -> dict:
        payload = dict(point.payload or {})
        text = payload.pop("text", None)
        return {
            "id": point.id,
            "text": text,
            "metadata": payload,
            "score": getattr(point, "score", None),
        }
