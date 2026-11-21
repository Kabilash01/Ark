"""Day 22 literature tools. Fetch metadata/abstracts, not PDF full text.

HTTP errors (including rate limits) and malformed responses propagate to callers.
Pass a client to reuse connections or test without network access.
"""

import os
import re
import threading
import time
from urllib.parse import quote
from xml.etree import ElementTree as ET

import httpx

ARXIV_URL = "https://export.arxiv.org/api/query"
SCHOLAR_URL = "https://api.semanticscholar.org/graph/v1"
PAPER_FIELDS = "title,abstract,authors,year,url,externalIds,citationCount,openAccessPdf"
ATOM = "{http://www.w3.org/2005/Atom}"
_arxiv_lock = threading.Lock()
_last_arxiv_request = 0.0


def _get(url, params, client, headers=None):
    if client is None:
        with httpx.Client(timeout=30.0) as owned_client:
            return _get(url, params, owned_client, headers)
    response = client.get(url, params=params, headers=headers, timeout=30.0)
    response.raise_for_status()
    return response


def _page(query, limit, offset, maximum):
    if not isinstance(query, str) or not query.strip():
        raise ValueError("query or paper ID must not be empty")
    if type(limit) is not int or not 1 <= limit <= maximum:
        raise ValueError(f"limit must be between 1 and {maximum}")
    if type(offset) is not int or offset < 0:
        raise ValueError("offset must be a non-negative integer")


def _arxiv(params, client):
    global _last_arxiv_request
    # arXiv asks clients to leave at least three seconds between requests.
    with _arxiv_lock:
        delay = 3.0 - (time.monotonic() - _last_arxiv_request)
        if delay > 0:
            time.sleep(delay)
        try:
            response = _get(ARXIV_URL, params, client)
        finally:
            _last_arxiv_request = time.monotonic()
    root = ET.fromstring(response.content)
    if root.tag != f"{ATOM}feed":
        raise ValueError("Expected an arXiv Atom feed")
    papers = []
    for entry in root.findall(f"{ATOM}entry"):
        def field(name):
            return " ".join((entry.findtext(f"{ATOM}{name}") or "").split())

        url = field("id")
        if url.endswith("/errors"):
            raise ValueError(f"arXiv API error: {field('summary')}")
        if not url or not field("title"):
            raise ValueError("arXiv entry is missing its ID or title")
        papers.append({
            "arxiv_id": url.split("/abs/")[-1],
            "title": field("title"), "abstract": field("summary"),
            "authors": [a.findtext(f"{ATOM}name") for a in entry.findall(f"{ATOM}author")],
            "published": field("published"), "updated": field("updated"),
            "url": url,
            "pdf_url": next((link.get("href") for link in entry.findall(f"{ATOM}link")
                             if link.get("title") == "pdf"), None),
            "categories": [c.get("term") for c in entry.findall(f"{ATOM}category")],
        })
    return papers


def search_arxiv(query: str, max_results: int = 5, start: int = 0, *,
                 client: httpx.Client | None = None) -> list[dict]:
    """Search using arXiv query syntax; start is a zero-based page offset."""
    _page(query, max_results, start, 100)
    return _arxiv({"search_query": query, "max_results": max_results,
                   "start": start, "sortBy": "relevance"}, client)


def fetch_arxiv_paper(arxiv_id: str, *, client: httpx.Client | None = None) -> dict:
    """Fetch one paper's metadata and abstract using a modern or legacy ID."""
    if not isinstance(arxiv_id, str) or not re.fullmatch(
        r"(?:\d{4}\.\d{4,5}|[a-zA-Z.-]+/\d{7})(?:v\d+)?", arxiv_id
    ):
        raise ValueError("Expected an arXiv ID, e.g. 1706.03762 or hep-th/9901001")
    papers = _arxiv({"id_list": arxiv_id}, client)
    if not papers:
        raise LookupError(f"arXiv paper not found: {arxiv_id}")
    return papers[0]


def _scholar(path, params, client, api_key):
    key = api_key if api_key is not None else os.environ.get("SEMANTIC_SCHOLAR_API_KEY", "")
    result = _get(f"{SCHOLAR_URL}/{path}", params, client,
                  {"x-api-key": key} if key else {}).json()
    if not isinstance(result, dict) or not isinstance(result.get("data"), list):
        raise ValueError("Expected a Semantic Scholar result page")
    return result


def search_semantic_scholar(query: str, limit: int = 5, offset: int = 0, *,
                            client: httpx.Client | None = None,
                            api_key: str | None = None) -> dict:
    """Return a result page, preserving total/offset/next pagination metadata."""
    _page(query, limit, offset, 100)
    return _scholar("paper/search", {"query": query, "limit": limit,
                    "offset": offset, "fields": PAPER_FIELDS}, client, api_key)


def get_paper_citations(paper_id: str, limit: int = 100, offset: int = 0, *,
                        client: httpx.Client | None = None,
                        api_key: str | None = None) -> dict:
    """Return a page of incoming citingPaper records, not outgoing references.

    IDs may be Semantic Scholar IDs or identifiers such as ARXIV:1706.03762.
    Follow the returned next offset explicitly to fetch further pages.
    """
    _page(paper_id, limit, offset, 1000)
    return _scholar(f"paper/{quote(paper_id, safe='')}/citations",
                    {"limit": limit, "offset": offset, "fields": PAPER_FIELDS}, client, api_key)
