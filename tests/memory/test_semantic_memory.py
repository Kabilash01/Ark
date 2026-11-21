from uuid import uuid4

import pytest
from qdrant_client import QdrantClient

from memory.semantic_memory import SemanticMemory

TEST_COLLECTION = "test_ark_semantic_memory"


@pytest.fixture
def sm(encoder):
    client = QdrantClient(host="localhost", port=6333)
    if client.collection_exists(TEST_COLLECTION):
        client.delete_collection(TEST_COLLECTION)
    memory = SemanticMemory(client=client, encoder=encoder, collection_name=TEST_COLLECTION)
    yield memory
    client.delete_collection(TEST_COLLECTION)


def test_store_and_count(sm):
    sm.store("Transformers use self-attention to process sequences.")
    sm.store("Convolutional networks use spatial filters.")
    assert sm.count() == 2


def test_store_with_same_point_id_replaces_the_existing_record(sm):
    point_id = str(uuid4())
    sm.store("First abstract", metadata={"source": "arxiv"}, point_id=point_id)
    updated_id = sm.store("Updated abstract", metadata={"source": "arxiv"}, point_id=point_id)

    assert updated_id == point_id
    assert sm.count() == 1
    assert sm.retrieve_by_metadata({"source": "arxiv"})[0]["text"] == "Updated abstract"


def test_semantic_similarity_retrieval(sm):
    sm.store("The transformer architecture relies on self-attention mechanisms.")
    sm.store("Bananas are a good source of potassium.")
    sm.store("Attention is all you need -- the original transformer paper.")

    results = sm.retrieve("How does self-attention work in transformers?", top_k=2)

    assert len(results) == 2
    texts = [r["text"] for r in results]
    assert "Bananas are a good source of potassium." not in texts
    assert any("transformer" in t.lower() or "attention" in t.lower() for t in texts)


def test_retrieve_scores_are_ordered_descending(sm):
    sm.store("The transformer architecture relies on self-attention mechanisms.")
    sm.store("Bananas are a good source of potassium.")

    results = sm.retrieve("self-attention in transformers", top_k=2)
    assert results[0]["score"] >= results[1]["score"]
    assert results[0]["text"] == "The transformer architecture relies on self-attention mechanisms."


def test_metadata_stored_and_returned(sm):
    sm.store("Qwen2.5 is a family of open-weight LLMs.", metadata={"source": "arxiv", "topic": "llm"})
    results = sm.retrieve("open-weight language models", top_k=1)
    assert results[0]["metadata"] == {"source": "arxiv", "topic": "llm"}


def test_retrieve_by_metadata_filter(sm):
    sm.store("Paper A about transformers.", metadata={"source": "arxiv", "topic": "llm"})
    sm.store("Paper B about diffusion models.", metadata={"source": "arxiv", "topic": "vision"})
    sm.store("Blog post about transformers.", metadata={"source": "blog", "topic": "llm"})

    results = sm.retrieve_by_metadata({"source": "arxiv", "topic": "llm"})
    assert len(results) == 1
    assert results[0]["text"] == "Paper A about transformers."


def test_delete_removes_point(sm):
    point_id = sm.store("This will be deleted.")
    assert sm.count() == 1
    sm.delete(point_id)
    assert sm.count() == 0


def test_count_empty_collection(sm):
    assert sm.count() == 0
