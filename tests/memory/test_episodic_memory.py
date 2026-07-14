from memory.episodic_memory import EpisodicMemory


def test_log_and_query_recent(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(
        task_id="task_1",
        agent_name="coding_agent",
        agent_version="1.0.0",
        intent="CODE",
        summary="Implemented a FastAPI endpoint",
        quality_score=0.9,
    )
    recent = em.query_recent()
    assert len(recent) == 1
    assert recent[0]["task_id"] == "task_1"
    assert recent[0]["agent_name"] == "coding_agent"
    assert recent[0]["quality_score"] == 0.9
    em.close()


def test_persists_across_restart(tmp_path):
    db_path = tmp_path / "episodic.db"

    em1 = EpisodicMemory(db_path=db_path)
    em1.log(
        task_id="task_1",
        agent_name="research_agent",
        agent_version="1.0.0",
        intent="RESEARCH",
        summary="Researched transformer architectures",
        quality_score=0.85,
        metadata={"papers_found": 5},
    )
    em1.close()

    em2 = EpisodicMemory(db_path=db_path)
    recent = em2.query_recent()
    assert len(recent) == 1
    assert recent[0]["summary"] == "Researched transformer architectures"
    assert recent[0]["metadata"] == {"papers_found": 5}
    em2.close()


def test_keyword_search(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(
        task_id="task_1", agent_name="research_agent", agent_version="1.0.0",
        intent="RESEARCH", summary="Researched transformer attention mechanisms",
    )
    em.log(
        task_id="task_2", agent_name="coding_agent", agent_version="1.0.0",
        intent="CODE", summary="Implemented a REST API endpoint",
    )
    results = em.search("transformer")
    assert len(results) == 1
    assert results[0]["task_id"] == "task_1"

    no_match = em.search("nonexistent keyword")
    assert no_match == []
    em.close()


def test_query_by_agent(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(task_id="t1", agent_name="coding_agent", agent_version="1.0.0", intent="CODE", summary="a")
    em.log(task_id="t2", agent_name="research_agent", agent_version="1.0.0", intent="RESEARCH", summary="b")
    results = em.query_by_agent("coding_agent")
    assert [r["task_id"] for r in results] == ["t1"]
    em.close()


def test_query_by_intent(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(task_id="t1", agent_name="coding_agent", agent_version="1.0.0", intent="CODE", summary="a")
    em.log(task_id="t2", agent_name="coding_agent", agent_version="1.0.0", intent="EVALUATE", summary="b")
    results = em.query_by_intent("EVALUATE")
    assert [r["task_id"] for r in results] == ["t2"]
    em.close()


def test_query_by_timerange(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(task_id="old", agent_name="a", agent_version="1.0.0", intent="CODE", summary="x", timestamp="2026-01-01T00:00:00+00:00")
    em.log(task_id="new", agent_name="a", agent_version="1.0.0", intent="CODE", summary="y", timestamp="2026-07-01T00:00:00+00:00")
    results = em.query_by_timerange("2026-06-01T00:00:00+00:00", "2026-08-01T00:00:00+00:00")
    assert [r["task_id"] for r in results] == ["new"]
    em.close()


def test_query_by_version_filters_correctly(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(task_id="t1", agent_name="coding_agent", agent_version="1.0.0", intent="CODE", summary="old version run")
    em.log(task_id="t2", agent_name="coding_agent", agent_version="1.3.1", intent="CODE", summary="new version run")
    em.log(task_id="t3", agent_name="research_agent", agent_version="1.3.1", intent="RESEARCH", summary="different agent")

    v1_results = em.query_by_version("coding_agent", "1.0.0")
    assert [r["task_id"] for r in v1_results] == ["t1"]

    v2_results = em.query_by_version("coding_agent", "1.3.1")
    assert [r["task_id"] for r in v2_results] == ["t2"]
    em.close()


def test_query_recent_respects_limit_and_order(tmp_path):
    em = EpisodicMemory(db_path=tmp_path / "episodic.db")
    em.log(task_id="t1", agent_name="a", agent_version="1.0.0", intent="CODE", summary="x", timestamp="2026-01-01T00:00:00+00:00")
    em.log(task_id="t2", agent_name="a", agent_version="1.0.0", intent="CODE", summary="y", timestamp="2026-01-02T00:00:00+00:00")
    em.log(task_id="t3", agent_name="a", agent_version="1.0.0", intent="CODE", summary="z", timestamp="2026-01-03T00:00:00+00:00")
    results = em.query_recent(limit=2)
    assert [r["task_id"] for r in results] == ["t3", "t2"]
    em.close()
