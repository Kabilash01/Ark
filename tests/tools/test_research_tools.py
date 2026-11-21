import httpx
import pytest

from tools import research_tools as research

FEED = '''<feed xmlns="http://www.w3.org/2005/Atom"><entry>
<id>http://arxiv.org/abs/1706.03762v7</id><title>Attention Is
 All You Need</title><summary>Transformer abstract.</summary>
<author><name>A. Author</name></author><published>2017-06-12</published>
<updated>2023-08-02</updated><category term="cs.CL"/>
<link title="pdf" href="http://arxiv.org/pdf/1706.03762v7"/>
</entry></feed>'''


@pytest.fixture(autouse=True)
def no_wait(monkeypatch):
    monkeypatch.setattr(research.time, "sleep", lambda seconds: None)


def client(handler):
    return httpx.Client(transport=httpx.MockTransport(handler))


def test_arxiv_search_parses_metadata_and_encodes_query():
    def respond(request):
        assert request.url.params["search_query"] == "ti:attention AND au:vaswani"
        assert request.url.params["start"] == "5"
        return httpx.Response(200, text=FEED)
    with client(respond) as http:
        paper, = research.search_arxiv("ti:attention AND au:vaswani", start=5, client=http)
    assert paper["arxiv_id"] == "1706.03762v7"
    assert paper["title"] == "Attention Is All You Need"
    assert paper["authors"] == ["A. Author"]
    assert paper["abstract"] == "Transformer abstract."
    assert paper["categories"] == ["cs.CL"]
    assert paper["pdf_url"].endswith("1706.03762v7")


@pytest.mark.parametrize("paper_id", ["1706.03762v7", "hep-th/9901001"])
def test_fetch_uses_id_list(paper_id):
    def respond(request):
        assert request.url.params["id_list"] == paper_id
        return httpx.Response(200, text=FEED)
    with client(respond) as http:
        assert research.fetch_arxiv_paper(paper_id, client=http)["title"]
        assert not http.is_closed


def test_empty_search_and_missing_paper():
    with client(lambda r: httpx.Response(200, text='<feed xmlns="http://www.w3.org/2005/Atom"/>')) as http:
        assert research.search_arxiv("missing", client=http) == []
        with pytest.raises(LookupError):
            research.fetch_arxiv_paper("1706.03762", client=http)


def test_arxiv_error_feed():
    feed = FEED.replace("http://arxiv.org/abs/1706.03762v7", "http://arxiv.org/api/errors")
    with client(lambda r: httpx.Response(200, text=feed)) as http:
        with pytest.raises(ValueError, match="API error"):
            research.search_arxiv("bad", client=http)


def test_scholar_search_preserves_nulls_and_pagination(monkeypatch):
    monkeypatch.setenv("SEMANTIC_SCHOLAR_API_KEY", "test-key")
    page = {"total": 20, "offset": 0, "next": 1, "data": [{"paperId": "abc", "abstract": None}]}
    def respond(request):
        assert request.headers["x-api-key"] == "test-key"
        assert request.url.params["limit"] == "1"
        assert "abstract" in request.url.params["fields"]
        return httpx.Response(200, json=page)
    with client(respond) as http:
        assert research.search_semantic_scholar("attention", limit=1, client=http) == page


def test_citations_are_incoming_and_paginated():
    page = {"offset": 10, "next": 11, "data": [{"citingPaper": {"paperId": "citing"}}]}
    def respond(request):
        assert request.url.path.endswith("/paper/ARXIV:1706.03762/citations")
        assert request.url.params["offset"] == "10"
        assert "x-api-key" not in request.headers
        return httpx.Response(200, json=page)
    with client(respond) as http:
        assert research.get_paper_citations("ARXIV:1706.03762", offset=10, client=http, api_key="") == page


@pytest.mark.parametrize("status", [404, 429, 500])
@pytest.mark.parametrize("function", [research.search_arxiv, research.search_semantic_scholar, research.get_paper_citations])
def test_http_errors_are_not_empty_results(status, function):
    with client(lambda r: httpx.Response(status)) as http:
        with pytest.raises(httpx.HTTPStatusError) as error:
            function("query", client=http)
    assert error.value.response.status_code == status


def test_timeout_propagates():
    def respond(request):
        raise httpx.ReadTimeout("timed out", request=request)
    with client(respond) as http:
        with pytest.raises(httpx.ReadTimeout):
            research.search_arxiv("query", client=http)


@pytest.mark.parametrize("function,kwargs", [
    (research.search_arxiv, {"query": ""}),
    (research.search_arxiv, {"query": "x", "start": -1}),
    (research.search_arxiv, {"query": "x", "max_results": 0}),
    (research.search_semantic_scholar, {"query": "x", "limit": 101}),
    (research.get_paper_citations, {"paper_id": " "}),
    (research.fetch_arxiv_paper, {"arxiv_id": "https://untrusted.example"}),
])
def test_invalid_input_never_requests(function, kwargs):
    def unexpected(request):
        pytest.fail("Invalid input triggered network access")
    with client(unexpected) as http:
        with pytest.raises(ValueError):
            function(**kwargs, client=http)


def test_arxiv_request_spacing(monkeypatch):
    delays = []
    monkeypatch.setattr(research, "_last_arxiv_request", 99.0)
    monkeypatch.setattr(research.time, "monotonic", lambda: 100.0)
    monkeypatch.setattr(research.time, "sleep", delays.append)
    with client(lambda r: httpx.Response(200, text=FEED)) as http:
        research.search_arxiv("query", client=http)
    assert delays == [2.0]


@pytest.mark.parametrize("body", [{}, {"data": None}, []])
def test_malformed_scholar_response(body):
    with client(lambda r: httpx.Response(200, json=body)) as http:
        with pytest.raises(ValueError, match="result page"):
            research.search_semantic_scholar("query", client=http)
