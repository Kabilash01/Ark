import pytest

from memory.procedural_memory import ProceduralMemory


@pytest.fixture
def pm(tmp_path, encoder):
    return ProceduralMemory(storage_dir=tmp_path / "procedural", encoder=encoder)


def test_add_skill_and_get(pm):
    skill_id = pm.add_skill(
        intent="CODE",
        description="Write a FastAPI CRUD endpoint with input validation",
        steps=["define pydantic model", "write route handler", "add tests"],
        source="manual",
    )
    skill = pm.get_skill(skill_id)
    assert skill["intent"] == "CODE"
    assert skill["success_rate"] == 0.5
    assert skill["use_count"] == 0
    assert skill["last_used"] is None
    assert skill["steps"] == ["define pydantic model", "write route handler", "add tests"]


def test_get_skill_missing_returns_none(pm):
    assert pm.get_skill("nonexistent") is None


def test_retrieve_finds_relevant_skill(pm):
    pm.add_skill(
        intent="CODE",
        description="Implement a REST API endpoint in FastAPI with request validation",
        steps=["a"],
    )
    pm.add_skill(
        intent="RESEARCH",
        description="Summarize a scientific paper's methodology section",
        steps=["b"],
    )
    results = pm.retrieve("How do I build a FastAPI endpoint?", top_k=1)
    assert len(results) == 1
    assert results[0]["intent"] == "CODE"


def test_retrieve_empty_when_no_skills(pm):
    assert pm.retrieve("anything") == []


def test_retrieve_weighted_by_success_rate(pm):
    # Two near-identical skills for the same query; the one with a proven
    # higher success_rate should outrank the other despite similar text.
    good_id = pm.add_skill(
        intent="CODE", description="Fix a failing pytest test by reading the traceback", steps=["a"]
    )
    bad_id = pm.add_skill(
        intent="CODE", description="Fix a failing pytest test by reading the traceback carefully", steps=["b"]
    )
    for _ in range(5):
        pm.record_use(good_id, success=True)
    for _ in range(5):
        pm.record_use(bad_id, success=False)

    results = pm.retrieve("How do I fix a failing test?", top_k=2)
    assert results[0]["skill_id"] == good_id


def test_record_use_updates_success_rate_and_use_count(pm):
    skill_id = pm.add_skill(intent="CODE", description="Some skill", steps=["a"])
    pm.record_use(skill_id, success=True)
    skill = pm.get_skill(skill_id)
    assert skill["success_rate"] == 1.0
    assert skill["use_count"] == 1
    assert skill["last_used"] is not None

    pm.record_use(skill_id, success=False)
    skill = pm.get_skill(skill_id)
    assert skill["success_rate"] == 0.5
    assert skill["use_count"] == 2


def test_record_use_missing_skill_returns_none(pm):
    assert pm.record_use("nonexistent", success=True) is None


def test_all_skills(pm):
    pm.add_skill(intent="CODE", description="a", steps=["x"])
    pm.add_skill(intent="RESEARCH", description="b", steps=["y"])
    assert len(pm.all_skills()) == 2


def test_persistence_across_restart(tmp_path, encoder):
    storage_dir = tmp_path / "procedural"
    pm1 = ProceduralMemory(storage_dir=storage_dir, encoder=encoder)
    skill_id = pm1.add_skill(
        intent="CODE", description="Implement a REST API endpoint in FastAPI", steps=["a", "b"]
    )
    pm1.record_use(skill_id, success=True)

    assert (storage_dir / "skills.json").exists()
    assert (storage_dir / "skills.faiss").exists()

    pm2 = ProceduralMemory(storage_dir=storage_dir, encoder=encoder)
    skill = pm2.get_skill(skill_id)
    assert skill is not None
    assert skill["success_rate"] == 1.0

    results = pm2.retrieve("FastAPI REST endpoint", top_k=1)
    assert results[0]["skill_id"] == skill_id
