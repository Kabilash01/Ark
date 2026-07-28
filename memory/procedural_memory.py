import json
import uuid
from datetime import datetime, timezone
from pathlib import Path

import faiss
import numpy as np
from sentence_transformers import SentenceTransformer

EMBEDDING_MODEL = "BAAI/bge-large-en-v1.5"
EMBEDDING_DIM = 1024


class ProceduralMemory:
    """Skill / how-to library. JSON metadata + FAISS nearest-neighbor
    retrieval over skill descriptions, ranked by similarity weighted by
    each skill's success_rate."""

    def __init__(self, storage_dir: str | Path = "data/procedural", encoder: SentenceTransformer | None = None):
        self._dir = Path(storage_dir)
        self._dir.mkdir(parents=True, exist_ok=True)
        self._skills_path = self._dir / "skills.json"
        self._index_path = self._dir / "skills.faiss"
        # CPU by default -- this shares the 8GB VRAM budget with the local
        # LLMs (Ollama), which need it far more than a short-text encoder does.
        self._encoder = encoder or SentenceTransformer(EMBEDDING_MODEL, device="cpu")

        self._skills: dict[str, dict] = {}
        self._faiss_ids: dict[str, int] = {}
        self._next_faiss_id = 0
        self._index = faiss.IndexIDMap(faiss.IndexFlatIP(EMBEDDING_DIM))

        self._load()

    def _embed(self, text: str) -> np.ndarray:
        vector = self._encoder.encode(text, normalize_embeddings=True)
        return np.asarray([vector], dtype="float32")

    def add_skill(self, intent: str, description: str, steps: list[str], source: str = "manual") -> str:
        skill_id = str(uuid.uuid4())
        self._skills[skill_id] = {
            "skill_id": skill_id,
            "intent": intent,
            "description": description,
            "steps": steps,
            "success_rate": 0.5,  # neutral prior until first recorded use
            "use_count": 0,
            "last_used": None,
            "source": source,
        }

        faiss_id = self._next_faiss_id
        self._next_faiss_id += 1
        self._faiss_ids[skill_id] = faiss_id
        self._index.add_with_ids(self._embed(description), np.asarray([faiss_id], dtype="int64"))

        self._save()
        return skill_id

    def retrieve(self, query: str, top_k: int = 5, candidate_pool: int = 20) -> list[dict]:
        if not self._skills:
            return []
        pool = min(candidate_pool, self._index.ntotal)
        scores, ids = self._index.search(self._embed(query), pool)

        id_to_skill_id = {faiss_id: skill_id for skill_id, faiss_id in self._faiss_ids.items()}
        ranked = []
        for score, faiss_id in zip(scores[0], ids[0]):
            if faiss_id == -1:
                continue
            skill_id = id_to_skill_id.get(int(faiss_id))
            if skill_id is None:
                continue
            skill = self._skills[skill_id]
            ranked.append((float(score) * skill["success_rate"], skill))

        ranked.sort(key=lambda pair: pair[0], reverse=True)
        return [skill for _, skill in ranked[:top_k]]

    def record_use(self, skill_id: str, success: bool) -> dict | None:
        skill = self._skills.get(skill_id)
        if skill is None:
            return None
        n = skill["use_count"]
        outcome = 1.0 if success else 0.0
        skill["success_rate"] = ((skill["success_rate"] * n) + outcome) / (n + 1)
        skill["use_count"] = n + 1
        skill["last_used"] = datetime.now(timezone.utc).isoformat()
        self._save()
        return skill

    def get_skill(self, skill_id: str) -> dict | None:
        return self._skills.get(skill_id)

    def all_skills(self) -> list[dict]:
        return list(self._skills.values())

    def _save(self) -> None:
        with open(self._skills_path, "w") as f:
            json.dump(
                {"skills": self._skills, "faiss_ids": self._faiss_ids, "next_faiss_id": self._next_faiss_id},
                f,
                indent=2,
            )
        faiss.write_index(self._index, str(self._index_path))

    def _load(self) -> None:
        if self._skills_path.exists():
            with open(self._skills_path) as f:
                data = json.load(f)
            self._skills = data.get("skills", {})
            self._faiss_ids = data.get("faiss_ids", {})
            self._next_faiss_id = data.get("next_faiss_id", 0)
        if self._index_path.exists():
            self._index = faiss.read_index(str(self._index_path))
