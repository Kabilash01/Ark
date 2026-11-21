import json
from unittest.mock import Mock
from uuid import NAMESPACE_URL, uuid5

import httpx
import pytest

from core.ark_message import Intent, Status, create_ark_message
from workers.phase1 import research_agent as research

PAPER_ID = "1706.03762v7"
ABSTRACT = "The Transformer uses attention mechanisms rather than recurrence."
FINDINGS = [{"text": "The Transformer uses attention.", "arxiv_ids": [PAPER_ID]}]
FEED = f"""<feed xmlns="http://www.w3.org/2005/Atom"><entry>
<id>http://arxiv.org/abs/{PAPER_ID}</id><title>Attention Is All You Need</title>
<summary>{ABSTRACT}</summary><author><name>Ashish Vaswani</name></author>
<published>2017-06-12T00:00:00Z</published></entry></feed>"""
EMPTY_FEED = '<feed xmlns="http://www.w3.org/2005/Atom"/>'


@pytest.fixture
def setup(tmp_path, monkeypatch):
    active = tmp_path / "active-models.json"
    active.write_text(json.dumps({"models": {"general": "local-test-model"}}))
    privacy = tmp_path / "privacy_config.json"
    privacy.write_text(json.dumps({"allowed_external": ["export.arxiv.org"]}))
    monkeypatch.setattr(research, "PRIVACY_CONFIG_PATH", privacy)
    monkeypatch.setattr("tools.research_tools.time.sleep", lambda _: None)
    return active, privacy, Mock(spec=["store"])


def make_message(**payload):
    return create_ark_message("router", "research_agent", Intent.RESEARCH, payload)


def ollama_response(output):
    return httpx.Response(200, json={"message": {"content": json.dumps(output)}})


def test_research_worker_from_planner_instruction(setup):
    active, _, memory = setup
    calls = []

    def respond(request):
        if request.url.host == "localhost":
            body = json.loads(request.content)
            assert body["model"] == "local-test-model"
            assert body["format"] == "json" and body["stream"] is False
            if not calls:
                calls.append("query")
                return ollama_response({"query": "ti:attention"})
            calls.append("synthesis")
            memory.store.assert_called_once()
            inputs = json.loads(body["messages"][1]["content"])
            assert inputs["papers"][0]["abstract"] == ABSTRACT
            return ollama_response({"summary": "The paper introduces the Transformer.", "findings": FINDINGS})
        assert request.url.host == "export.arxiv.org"
        if "id_list" in request.url.params:
            calls.append("fetch")
            assert request.url.params["id_list"] == PAPER_ID
        else:
            calls.append("search")
            assert request.url.params["search_query"] == "ti:attention"
        return httpx.Response(200, text=FEED)

    with httpx.Client(transport=httpx.MockTransport(respond)) as client:
        agent = research.ResearchAgent(memory, active, http_client=client)
        result = agent._run_with_error_handling(make_message(instruction="Research the Transformer", privacy=True))

    assert calls == ["query", "search", "fetch", "synthesis"]
    assert result["status"] == Status.DONE
    assert result["agent_version"] == "research_agent@1.0.0"
    assert result["output"]["findings"] == FINDINGS
    citation, = result["output"]["citations"]
    assert citation["url"] == f"https://arxiv.org/abs/{PAPER_ID}"
    memory.store.assert_called_once_with(
        "semantic", text=ABSTRACT, metadata={"source": "arxiv", **citation},
        point_id=str(uuid5(NAMESPACE_URL, citation["url"])),
    )


def test_uses_scholar_arxiv_ids_when_arxiv_search_is_empty(setup):
    active, _, memory = setup
    requests = []

    def respond(request):
        requests.append(request.url.host)
        if request.url.host == "api.semanticscholar.org":
            assert request.url.params["query"] == "attention transformer"
            return httpx.Response(200, json={"data": [{"externalIds": {"ArXiv": "1706.03762"}}]})
        if request.url.host == "localhost":
            return ollama_response({"summary": "The Transformer uses attention.", "findings": FINDINGS})
        feed = FEED if request.url.params.get("id_list") == "1706.03762" else EMPTY_FEED
        return httpx.Response(200, text=feed)

    with httpx.Client(transport=httpx.MockTransport(respond)) as client:
        agent = research.ResearchAgent(memory, active, http_client=client)
        result = agent._run_with_error_handling(make_message(query="ti:attention AND all:transformer"))
    assert result["status"] == Status.DONE
    assert requests == ["export.arxiv.org", "api.semanticscholar.org", "export.arxiv.org", "localhost"]


@pytest.mark.parametrize("config,payload", [
    ({"airgap_mode": True}, {"airgap_mode": False}),
    ({}, {"airgap_mode": True}),
    ({"privacy_mode": True, "allowed_external": []}, {"privacy": False}),
])
def test_privacy_blocks_requests(setup, config, payload):
    active, privacy, memory = setup
    privacy.write_text(json.dumps(config))

    def unexpected(request):
        pytest.fail(f"Blocked task made request to {request.url.host}")

    with httpx.Client(transport=httpx.MockTransport(unexpected)) as client:
        agent = research.ResearchAgent(memory, active, http_client=client)
        result = agent._run_with_error_handling(make_message(query="ti:attention", **payload))
    assert result["status"] == Status.FAILED
    assert "block" in result["output"]["error_message"]
    memory.store.assert_not_called()


def test_privacy_prevents_unlisted_scholar_fallback(setup):
    active, _, memory = setup

    def respond(request):
        assert request.url.host == "export.arxiv.org"
        return httpx.Response(200, text=EMPTY_FEED)

    with httpx.Client(transport=httpx.MockTransport(respond)) as client:
        agent = research.ResearchAgent(memory, active, http_client=client)
        result = agent._run_with_error_handling(make_message(query="ti:attention", privacy=True))
    assert result["status"] == Status.FAILED
    assert result["output"]["error_type"] == "LookupError"
    memory.store.assert_not_called()


def test_rejects_model_citation_outside_retrieved_papers(setup):
    active, _, memory = setup

    def respond(request):
        if request.url.host == "localhost":
            return ollama_response({
                "summary": "Placeholder",
                "findings": [{"text": "Unsupported attribution.", "arxiv_ids": ["9999.99999"]}],
            })
        return httpx.Response(200, text=FEED)

    with httpx.Client(transport=httpx.MockTransport(respond)) as client:
        agent = research.ResearchAgent(memory, active, http_client=client)
        result = agent._run_with_error_handling(make_message(query="ti:attention"))
    assert result["status"] == Status.FAILED
    assert "outside the retrieved sources" in result["output"]["error_message"]
    assert "citations" not in result["output"]


def test_missing_active_models_gives_setup_hint(setup):
    active, _, memory = setup
    with pytest.raises(FileNotFoundError, match="scripts/select-model.py"):
        research.ResearchAgent(memory, active.with_name("missing.json"))
