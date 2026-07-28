import pytest
from sentence_transformers import SentenceTransformer

from memory.procedural_memory import EMBEDDING_MODEL


@pytest.fixture(scope="session")
def encoder():
    """Shared across every test module in this directory -- loading this
    model is slow, and separate copies compete for the same VRAM budget
    as the local LLMs. CPU-only, one instance for the whole test session."""
    return SentenceTransformer(EMBEDDING_MODEL, device="cpu")
