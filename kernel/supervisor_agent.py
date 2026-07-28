import os

import httpx

from core.ark_message import Intent
from core.message_bus import ARKMessageBus
from kernel.planner_agent import PlannerAgent
from memory.memory_agent import MemoryAgent

GROQ_API_URL = "https://api.groq.com/openai/v1/chat/completions"
GROQ_MODEL_PRIMARY = "openai/gpt-oss-120b"
GROQ_MODEL_FALLBACK = "llama-3.3-70b-versatile"
OLLAMA_BASE_URL = "http://localhost:11434"

CLASSIFY_SYSTEM_PROMPT = """You are the Supervisor Agent in ARK OS, a multi-agent system.
Classify the user's goal into exactly ONE of these intents. Each has a precise
meaning -- use the definition, not just the label, since several intents sound
similar in everyday language:

RESEARCH  Gather and synthesize knowledge from papers, literature, or multiple
          sources into a coherent summary. Deep, multi-source, produces understanding.
SEARCH    A single, narrow factual lookup (a price, a date, a definition) --
          not synthesis, not multiple sources, not deep understanding.
CODE      Write, edit, refactor, or improve source code itself.
EXECUTE   Run an already-written program/script/command and observe its output.
          Not about writing or changing code -- only running it.
EVALUATE  Score or judge the quality of an existing piece of work against criteria.
DISCOVER  Ask what capabilities, tools, or agents this system itself has --
          questions ABOUT the system, not about navigating a website.
BROWSE    Interact with a specific website through a browser: navigate, click,
          extract data from a page.
SECURITY  Scan or audit code/systems for vulnerabilities.
VISION    Interpret an image, screenshot, or visual figure -- the input is
          fundamentally visual, not a request to judge/score something.

Respond with ONLY the intent word, nothing else."""


class SupervisorAgent:
    """Routes tasks: classifies goal intent, arbitrates, final authority.

    Model tier: Groq gpt-oss-120b -> llama-3.3-70b-versatile -> local Ollama.
    Deviates from the plan's literal "claude-sonnet-4-6 primary ->
    groq:qwen2.5-72b fallback": no ANTHROPIC_API_KEY is configured, and
    qwen2.5-72b no longer exists on Groq (see project memory on the
    orchestration model decision). Groq is used as the sole cloud tier.

    Privacy mode: when True, Groq is never called -- classification goes
    straight to the local Ollama model, regardless of whether a Groq key
    is configured.
    """

    VALID_INTENTS = {
        Intent.RESEARCH, Intent.CODE, Intent.SEARCH, Intent.EXECUTE,
        Intent.EVALUATE, Intent.DISCOVER, Intent.BROWSE, Intent.SECURITY, Intent.VISION,
    }

    def __init__(
        self,
        groq_api_key: str | None = None,
        local_model: str = "qwen2.5:7b",
        http_client: httpx.Client | None = None,
        planner: PlannerAgent | None = None,
        memory_agent: MemoryAgent | None = None,
        message_bus: ARKMessageBus | None = None,
    ):
        self._groq_api_key = groq_api_key if groq_api_key is not None else os.environ.get("GROQ_API_KEY", "")
        self._local_model = local_model
        self._client = http_client or httpx.Client(timeout=30.0)
        self._planner = planner or PlannerAgent(groq_api_key=self._groq_api_key, local_model=local_model)
        self._memory_agent = memory_agent or MemoryAgent()
        self._message_bus = message_bus or ARKMessageBus()

    def handle_goal(self, task_id: str, goal: str, privacy_mode: bool = False) -> dict:
        """Full Supervisor -> Planner flow: classify intent, pull relevant
        memory context before routing (per the plan: "Memory Agent
        inject_context called before every routing step"), then hand off
        to the Planner for a task DAG. Every step is logged to the shared
        audit log on the Redis message bus."""
        self._message_bus.log_to_audit({"event": "supervisor_classify_start", "task_id": task_id, "goal": goal})
        intent = self.classify(goal, privacy_mode=privacy_mode)
        self._message_bus.log_to_audit({"event": "supervisor_classified", "task_id": task_id, "intent": intent})

        memory_context = self._memory_agent.inject_context(task_id, query=goal)

        self._message_bus.log_to_audit({"event": "planner_start", "task_id": task_id})
        dag = self._planner.plan(goal, privacy_mode=privacy_mode)
        self._message_bus.log_to_audit(
            {"event": "planner_done", "task_id": task_id, "task_count": len(dag["tasks"])}
        )

        return {
            "task_id": task_id,
            "goal": goal,
            "intent": intent,
            "memory_context": memory_context,
            "dag": dag,
        }

    def classify(self, goal: str, privacy_mode: bool = False) -> str:
        if not privacy_mode and self._groq_api_key:
            for model in (GROQ_MODEL_PRIMARY, GROQ_MODEL_FALLBACK):
                try:
                    return self._classify_via_groq(goal, model)
                except Exception:
                    continue
        return self._classify_via_ollama(goal)

    def _classify_via_groq(self, goal: str, model: str) -> str:
        response = self._client.post(
            GROQ_API_URL,
            headers={"Authorization": f"Bearer {self._groq_api_key}"},
            json={
                "model": model,
                "messages": [
                    {"role": "system", "content": CLASSIFY_SYSTEM_PROMPT},
                    {"role": "user", "content": goal},
                ],
                "temperature": 0,
                # gpt-oss-120b is a reasoning model -- it spends tokens on an
                # internal `reasoning` field before emitting `content`. A
                # small max_tokens truncates it before any answer appears
                # (finish_reason="length", content=""), so this must be
                # generous even though the final answer is one word.
                "max_tokens": 500,
            },
        )
        response.raise_for_status()
        content = response.json()["choices"][0]["message"]["content"]
        return self._parse_intent(content)

    def _classify_via_ollama(self, goal: str) -> str:
        response = self._client.post(
            f"{OLLAMA_BASE_URL}/api/chat",
            json={
                "model": self._local_model,
                "messages": [
                    {"role": "system", "content": CLASSIFY_SYSTEM_PROMPT},
                    {"role": "user", "content": goal},
                ],
                "stream": False,
                "options": {"temperature": 0},
            },
        )
        response.raise_for_status()
        content = response.json()["message"]["content"]
        return self._parse_intent(content)

    def _parse_intent(self, content: str) -> str:
        upper = content.strip().upper()
        for intent in self.VALID_INTENTS:
            if intent.value in upper:
                return intent.value
        raise ValueError(f"model returned an unrecognized intent: {content!r}")
