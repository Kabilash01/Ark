# SAGE and evidence-grounded hypothesis generation

**Status:** methods draft; the materials case study and live end-to-end results
are not yet available.

## Abstract

ARK OS implements a checkpointed scientific workflow that joins literature
retrieval, citation-graph structure, embedding-based novelty estimates,
feasibility scoring, experiment design, materials database screening, and
report generation. Hypotheses must cite records supplied to the generator;
their novelty is estimated against the retrieved paper embeddings, while graph
gaps are treated as candidates for investigation rather than proof of novelty.
The native C++ workflow can screen Materials Project records and emit a cited
LaTeX draft, but it does not execute a physical experiment. The API key and
local graph, vector, and inference services needed for a complete live run were
unavailable during this review, so this paper reports the implementation and
verification scope without claiming a materials discovery.

## Introduction and related work

Automated scientific systems combine operations that are often evaluated
separately: finding prior work, proposing hypotheses, writing and executing
code, analyzing outputs, and communicating results. The AI Scientist presents
an end-to-end system that generates ideas, writes code, runs experiments,
visualizes results, writes a paper, and simulates review
([Lu et al., 2024](https://arxiv.org/abs/2408.06292)). AI Scientist-v2 explores
agentic tree search for workshop-level discovery
([Yamada et al., 2025](https://arxiv.org/abs/2504.08066)). These systems motivate
evaluating a research workflow as a connected process, while their reported
capabilities should not be inferred from a software implementation alone.

ARK OS approaches this problem as a reproducible materials-screening workflow.
It retrieves literature into semantic memory, obtains citation-concept gap
candidates from Neo4j, asks a local model for five bounded and testable
hypotheses, ranks the candidates using evidence embeddings and feasibility,
designs experiments, queries Materials Project records, analyzes the returned
properties, and writes cited LaTeX reports. Unlike open-ended experimental
execution, this workflow is currently a database-screening pipeline. Its
outputs are not measurements from newly run laboratory or simulation
experiments.

The present contribution is an implementation method and verification design.
The current evidence does not support a performance comparison with AI
Scientist or a claim of scientific discovery. Such claims require completed
runs, evaluated hypotheses, and a controlled comparison that is not yet
available.

## Methods

### Literature grounding and hypothesis contract

The Hypothesis Engine retrieves up to 20 records for the requested domain and
requires at least five nonempty abstracts. It supplies each record's exact
paper identifier, title, URL, and a bounded abstract to the local model. The
model must return exactly five hypotheses. Each includes a testable statement,
a mechanism rationale, a feasibility score in `[0, 1]`, and one to three paper
IDs selected from the supplied evidence. IDs outside that evidence set are
rejected. The prompt requires a material or intervention, measurable outcome,
expected direction, comparator, and feasible validation; it explicitly tells
the model not to claim novelty.

This is a provenance check on references, not a check that the scientific
claims are true. The retrieved abstracts and citation graph remain inputs to
human or downstream verification.

### Semantic novelty and ranking

Let $e_h$ be the embedding of a candidate hypothesis and $P$ the embeddings of
the retrieved reference papers. The implementation computes:

$$
s_{max}(h) = \max_{p \in P} \cos(e_h, e_p), \\
N(h) = 1 - s_{max}(h), \\
R(h) = N(h) \cdot F(h),
$$

where $F(h)$ is the model-provided feasibility score. Candidates are sorted by
$R(h)$ in descending order, with hypothesis text as a deterministic tie-break.
With cosine similarity in `[-1, 1]`, this novelty proxy ranges from 0 to 2; it
is not calibrated as a probability and is not a literature novelty judgment.
It estimates distance from the retrieved corpus only. Coverage of that corpus,
embedding quality, and hypothesis wording can change the result.

### Citation-concept gap signal

The graph query finds pairs of `Concept` nodes that are both mentioned by the
same `Paper`, have no `RELATED` edge, and satisfy a stable lexical ordering.
It returns the number of distinct co-mentioning papers, sorted by count and
then concept name, with a bounded result limit. The graph signal is supplied
to hypothesis generation as context. A missing edge can indicate an
under-modeled relationship in the current graph; it does not establish that
the relationship is scientifically unexplored. Graph population and entity
resolution affect this signal.

### Experiment design and Materials Project screening

The native SAGE workflow designs a validation plan for each of the top two
ranked hypotheses. It queries the Materials Project summary endpoint for
requested material IDs and properties, then analyzes returned rows as CSV.
The workflow labels this as database screening and does not describe returned
database values as new experimental measurements. Each report includes
supporting paper references and can include Vision Agent descriptions of
user-provided figures. LaTeX output is a draft artifact and is marked
unverified.

### Checkpointing and resumption

The workflow has nine ordered checkpoint stages: literature, hypothesis,
experiment, analysis, vision figures, first report, second experiment, second
analysis, and second report. A stage is checkpointed only after it returns
`DONE`. Restart loads completed JSON artifacts and begins at the first missing
stage. Checkpoint files are limited to 16 MiB and use validated loop and step
names. The workflow signature binds version 3, topic, material IDs, and figure
paths; a loop ID cannot be resumed with different values for those inputs.
Model identifiers, model hashes, API response snapshots, and service versions
are not part of the signature, so the checkpoint mechanism alone does not
guarantee deterministic replay across changed services.

## Verification and case-study status

Unit tests cover hypothesis schema and reference validation, cosine ranking,
the citation-gap query, checkpoint interruption and resume, and Materials
Project request construction. The full Materials Project workflow has not
completed in this environment: `MP_API_KEY` and live Ollama, Qdrant, and Neo4j
services are unavailable. No completed SAGE report or materials-screening
case-study dataset is available to report here. Consequently, novelty scores,
graph gaps, and checkpoint behavior are implementation-level evidence only;
they are not evidence of discovery quality or end-to-end scientific utility.

## References

1. Chris Lu et al. [The AI Scientist: Towards Fully Automated Open-Ended
   Scientific Discovery](https://arxiv.org/abs/2408.06292). arXiv:2408.06292,
   2024.
2. Yutaro Yamada et al. [The AI Scientist-v2: Workshop-Level Automated
   Scientific Discovery via Agentic Tree Search](https://arxiv.org/abs/2504.08066).
   arXiv:2504.08066, 2025.
