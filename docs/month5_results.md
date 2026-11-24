# Month 5 review: evidence and open work

This review records the current C++ implementation status against Days 71–98.
It does not mark Month 5 complete: several guide criteria require live services
and an integrated worker path that are not available in this checkout.

## Verified work

| Area | Evidence | Status |
| --- | --- | --- |
| Native ARKBench | The existing v1.0 report contains all 50 tasks, numeric scores, and agent-version metadata. 44 rows have `DONE` status; all 50 are scored. | Complete for the Day 97 50-task run. |
| Month 2 comparison | The ten shared task IDs increased from 0.4035 to 0.438 (8.55%). The matched categories are research and coding. | Improvement shown in two categories; the guide's three-category target is not established. |
| Native prompt improvement | Day 92 implements reflection filtering, paired ten-case scoring, a strict 0.05 promotion gate, and version history updates. | Unit-tested; no live promotion was run. |
| Capability discovery | Day 93 builds a registry/reflection/ARKBench evidence map and answers a ten-question fixture. | Unit-tested; no live map refresh was run. |
| Voice input | Day 95 adds C++ CLI capture through ALSA or PipeWire, local `whisper-cli` transcription, and ARK task submission. | Unit-tested; live transcription was not available. |
| C++ verification | The full CTest run after Day 95 reports 36 passing and 2 skipped service-gated tests. | Passed. |

The 50-task combined score is 0.8876. It is not directly comparable with the
ten-task Month 2 mean because the final suite adds deterministic tool and
workflow tasks. See [Month 4 results](month4_results.md) for the dataset,
per-task report, and scoring scope.

## Remaining Month 5 criteria

- **All-agent flagship integration (Day 96): open.** The normal run planner is
  offered only a narrow worker set, and existing specialist modules are exposed
  as separate commands. The required single task cannot yet activate all 20
  agents. See [Day 96 integration audit](day96_agent_integration.md).
- **SAGE runs: not verified.** The Materials Project API key and local data
  services needed by the workflow are unavailable here; no complete two-run
  evidence is present.
- **ARK-VLM Stage 2: not started.** The installed C++ trainer is text-only and
  work-in-progress; the compatible base model and training datasets are absent.
  See [Day 94 training status](day94_vlm_training.md).
- **Live voice: not verified.** The machine has audio recorders but no
  `whisper-cli` binary or Whisper model. The command needs both configured to
  run the requested spoken-task smoke test.
- **Self-improvement across agents: not verified.** Day 92 currently supports
  research and coding agents, and the local episodic store has no reflection
  queue entries to learn from.

## Live service check

At review time, Ollama (11434), Redis (6379), Qdrant (6333), and Neo4j (7474)
were unreachable on loopback. Tests that need those services remain skipped or
unrun; fixture results are not described as live end-to-end results.
