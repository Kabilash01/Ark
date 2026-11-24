# ARK OS architecture paper: submission package

Status: internal preparation only. No venue has been selected and no submission
has been made.

## Working title

**ARK OS: A Native C++ Runtime for Evidence-Carrying Local Agent Workflows**

## Abstract

Local agent systems combine model inference, specialist tools, persistent
context, and execution services, but their results are difficult to reproduce
when plans, tool boundaries, and agent revisions are not recorded together.
ARK OS is a C++20 runtime that represents work as validated dependency graphs
and records task identity, evidence, status, and agent revision in a shared
message format. Its runtime includes bounded retries, dependency-aware
dispatch, local memory stores, isolated coding execution, and an evaluation
path. We describe the architecture, its storage and execution boundaries, and
a version-tagged benchmark of 50 tasks spanning research, coding, and
deterministic tool workflows. On ten unchanged research and coding task IDs,
the recorded mean score increased from 0.4035 in the Month 2 baseline to 0.438
in the Month 4 run, an 8.55% relative increase. Category results were mixed:
research rose from 0.518 to 0.736, while coding fell from 0.289 to 0.140. The
comparison is observational, not causal; both runs label agents version
1.0.0, and the later run lacks per-agent model fingerprints. A separate
20-task concurrency fixture reached the configured limits for browser,
terminal, and literature resources and retained all SQLite writes. These
measurements characterize selected local workflows rather than full-system
throughput. The planned single-task integration of all 20 agents has not been
demonstrated. We provide the architecture and evidence limits to support
reproduction and guide future integration work.

## Keywords

Local agent systems; workflow orchestration; reproducibility; persistent
memory; software agents; C++.

## Claims and evidence map

| Manuscript statement | Evidence | Qualification |
| --- | --- | --- |
| The runtime validates and dispatches dependency graphs with bounded retries. | C++ kernel, DAG and supervisor tests, architecture methods section. | Component behavior is tested; this does not establish all-agent integration. |
| The 50-task benchmark records task outcomes and revisions. | `benchmarks/arkbench/results/month4_final.json`; [`arkbench.md`](arkbench.md). | It combines model-graded research/coding with deterministic tool cases. |
| The matched ten-task score moved from 0.4035 to 0.438. | Month 2 and Month 4 result artifacts; [`self_improvement_evidence.md`](../self_improvement_evidence.md). | Same nominal agent version, missing Month 4 model fingerprints, and a coding regression prevent causal attribution. |
| Resource limits and SQLite writes behaved as configured in a 20-task fixture. | `benchmarks/performance/day68_stress.json`. | Synthetic fixture; not a model inference or external service throughput result. |
| One goal can activate all 20 planned agents. | No supporting result. | Explicitly not claimed; see [`day96_agent_integration.md`](../day96_agent_integration.md). |

## Required author and venue decisions

- Select a target venue and check its current scope, template, word limits,
  anonymization rules, artifact policy, and submission deadline.
- Confirm author names, affiliations, corresponding author, and author order.
- Decide whether the paper is an engineering systems report, a research paper,
  or a systems demonstration. The current evidence does not support a causal
  self-improvement claim.
- Prepare a blinded manuscript if required. This draft has not been reviewed
  for identifying metadata or venue-specific formatting.

## Reproducibility materials still needed

- Archive the exact source revision, compiler and dependency versions, build
  options, benchmark inputs, scorer configuration, and all raw run outputs.
- Recover per-agent model fingerprints for both matched benchmark runs if they
  were logged elsewhere. If they cannot be recovered, keep the limitation
  prominent.
- Document the unavailable live-service checks and provide scripts for the
  concurrency fixture and benchmark report generation.
- Have an independent reviewer reproduce the matched score and inspect the
  coding category regression.
- Confirm that every figure is generated from committed data and that every
  reference is complete and correctly formatted for the selected venue.

## Submission gate

Do not submit until the target venue and author details are confirmed, the
manuscript has been checked against that venue's current rules, and the
reproducibility materials and limitations above have been reviewed. This file
is a preparation artifact, not a statement that the paper is submission-ready
or has been submitted.
