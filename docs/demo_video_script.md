# ARK OS demo video capture script

Status: capture plan only. No video has been recorded or published.

Target length: about 2 minutes. Capture a clean terminal at 1080p, use a fresh
checkout/build, and keep credentials and personal paths out of frame. The
spoken claims below are bounded to the committed evidence.

## 0:00–0:15 — What ARK is

**Show:** repository README and the native runtime guide.

**Say:** “ARK OS is a local-first agent runtime. The active application is
native C++20. It combines a planner, worker workflows, storage, and sandboxed
execution. Some workflows need local services and models.”

## 0:15–0:35 — Build and available commands

**Show:** run `build/ark-cpp --help`.

**Say:** “The same executable exposes the task runtime, memory, benchmark,
security, science, voice, and MCP entry points. The presence of a command does
not mean every external service for it is installed.”

## 0:35–0:50 — Hardware model selection

**Show:** `build/ark-cpp models 8` and its JSON output.

**Say:** “With an 8 GiB override, the selector reports tier T2 and the configured
general, coding, and vision models. The override demonstrates selection logic;
it does not check whether those model weights are installed.”

## 0:50–1:20 — MCP protocol surface

**Show:** launch `build/ark-cpp mcp` with initialize, initialized notification,
and tools/list requests. Filter output to the negotiated protocol and tool
names:

```bash
build/ark-cpp mcp <<'JSON' | jq -s '[.[0].result.protocolVersion, (.[1].result.tools | map(.name))]'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"demo","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
JSON
```

**Say:** “The C++ server speaks MCP over stdio and advertises five ARK tools.
This local handshake and listing are verified. Client registration and a
service-backed live tool call still need a separate check.”

## 1:20–1:45 — Benchmark evidence

**Show:** `docs/papers/arkbench.md` beside
`benchmarks/arkbench/results/month4_final.json`.

**Say:** “The committed ARKBench report has 50 cases. On the ten matched
research and coding IDs, the recorded mean moved from 0.4035 to 0.438. Research
improved in this sample and coding regressed. The comparison is observational,
not evidence that self-improvement caused the change.”

## 1:45–2:00 — Honest limits

**Show:** `docs/week15_review.md` and the Day 96 integration audit.

**Say:** “The suite reports 37 passed and two service-gated skips. All 20 agents
have not been shown working together in one task. Live SAGE, speech
transcription, and third-party MCP client registration also remain to be
verified.”

## Capture checklist

- Rebuild from a clean checkout and record the source commit in the video notes.
- Run only commands whose output is visible and repeatable; do not splice
  fixture results into a live inference demonstration.
- For a live research or coding segment, first install and document the exact
  local model and service configuration, then retain its raw run output.
- Do not show secrets, API tokens, private paths, or unredacted user data.
- Add captions and a text transcript. Link benchmark data and the open-work
  audit in the video description.
- The video remains unrecorded until a clean-machine run and the required
  service-backed segments are actually captured.
