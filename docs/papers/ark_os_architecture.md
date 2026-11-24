# ARK OS paper draft

## Architecture

ARK OS is a native C++20 runtime for workflows that combine specialist
workers, local model inference, persistent memory, and isolated code
execution. The CLI and loopback API submit goals to the same execution path.
Workers exchange an `ARKMessage` envelope containing task identity, intent,
status, dependencies, evidence, and agent version. This shared representation
supports both dispatch and post-run inspection.

### Planning and dispatch

The supervisor classifies a goal and the planner produces a directed acyclic
graph of worker tasks. The runtime validates task identities, dependencies,
and available workers before execution. It schedules tasks by dependency
layer, supplies completed upstream results as input, and reuses independent
work during retries. The evaluation worker records structured ratings. Low
ratings can send feedback upstream. The supervisor caps retry attempts and
records unresolved outcomes for inspection.

### Storage and execution boundaries

Redis stores working state, agent registry entries, and agent revisions.
Each revision identifies a model and prompt snapshot. Workers use the
selected snapshot throughout a dispatch, and the runtime records the version
in result messages and episodic records. SQLite stores episodic history and
procedural skills, and Qdrant indexes semantic records for retrieval. Model
and embedding requests use loopback endpoints. Research workers consult a
provider policy for external literature requests. Coding workers run tests in
Docker containers with bounded resources and disabled networking.

### Observability and evaluation

The terminal, REST API, and dashboard expose task status and persistent
evidence, including the selected agent revisions. A WebSocket stream reports
worker activity and storage events. The ARKBench runner uses fixed producer
and evaluator graphs so that repeat runs share a plan. Research cases check
the required citations. Coding cases execute a separate acceptance suite on
the submitted solution. The results record completion, elapsed time, and the
evaluator's ratings alongside the evidence checks. This design supports
comparisons between recorded configurations using the same dataset.

### Resource coordination and native workflows

The runtime bounds concurrent browser, terminal, and arXiv operations with
FIFO semaphores. Research dispatch assigns work among two process-local slots
using assignment counts and evaluator history. An evaluator score below the
configured threshold deprioritizes a slot, while an unrated slot remains
eligible. This policy routes requests among logical instances; it does not
imply separate model servers or machines.

The kernel can execute dependency graphs that combine local document, terminal,
and defensive security workers. The Month 4 benchmark adapter opts these
workers into its validated DAG path and restricts fixture data flow to selected
tool output strings written into document paragraph text. The adapter records
each participating agent version with the case result. In the 20-task
concurrency fixture, observed peaks matched configured resource capacities and
all SQLite writes survived. This measurement covers scheduling and local
storage, not inference or external tool throughput.

The final v1.0 evaluation includes 50 cases. On the ten unchanged research and
coding task IDs, the measured score rose from 0.4035 in the recorded Month 2
baseline to 0.438, an 8.55% relative improvement. The overall 50-case mean is
reported separately because it includes new deterministic tool workflows.
This aggregate does not show a uniform gain: research moved from 0.518 to
0.736, while coding moved from 0.289 to 0.140. Both reports label the agents
version `1.0.0`, so this comparison is not evidence that prompt self-improvement
caused the increase. The Month 4 report also lacks the per-agent model
fingerprints recorded in the Month 2 provenance. These results support a
matched-cohort observation, not a causal claim about the source of the change.

### Integration limits

The shared planner path is narrower than the collection of native worker
modules. A normal `ark-cpp run` offers a limited research/coding/evaluation
set plus selected browse and vision paths, and dispatches only selected agent
types through dedicated branches. Document, terminal, and security workflows
can be composed through the validated native-tool DAG path used by ARKBench,
while DevOps, ML Engineer, Data Analysis, GitHub, and SAGE specialists remain
direct CLI workflows. The flagship goal that activates all 20 agents in one
run has not been demonstrated. The unit and workflow tests establish component
behavior but do not establish that full-system integration criterion.

The prompt-improvement gate and capability evidence map are implemented in
native C++, with fixture tests. Neither has been run against live reflection,
registry, inference, or benchmark services in the current environment. The
multimodal training stage also remains unstarted: the installed llama.cpp
trainer does not implement the guide's projection-plus-LoRA workflow. See the
[Month 5 evidence review](../month5_results.md),
[matched self-improvement analysis](../self_improvement_evidence.md), and
[all-agent integration audit](../day96_agent_integration.md) for the exact
verification scope.

![A research, coding, and evaluation DAG. The coding node is running.](figures/ark_runtime_dag.png)

The task inspector derives the displayed edges from the validated plan.
The figure shows a three-stage workflow with a live-state fixture.

![The Research Agent v1.0.0 ARKBench point with a mean score of 0.518.](figures/ark_benchmark_timeline.png)

The version panel groups scored cases by producer and revision for each
completed benchmark run. This figure shows the first recorded baseline.

## Methods

### Local inference architecture

A shared C++ adapter routes structured chat and embeddings to the selected
loopback inference service. With llama.cpp selected, the adapter sends chat
completions and embedding requests to a local llama-server. The model router
loads the GGUF named by the agent profile and can cap the number of resident
models at one. This supports alternating general, coding, and vision models
on a device with limited memory. Vision inference loads the model's matching
projector and accepts image data with the user message. Ollama manifests can
resolve downloaded GGUF and projector paths for the server presets.

The inference experiment used ten coding prompts and one GGUF quantization
for both engines on an RTX 5060 Laptop GPU. Each engine received a warmup
request before measurement. The runtime recorded request latency, reported
generation throughput, and structured output validity for each prompt.
The coding repair benchmark separately uses locked unittests in Docker to
measure correctness of the generated patch. [Month 3 results](../month3_results.md)
