# ARKBench: version-tagged evaluation of native agent workflows

**Status:** benchmark design and internal results draft. External baseline
comparisons are not available.

## Abstract

ARKBench evaluates research and coding workers alongside deterministic browser,
document, terminal, security, coordination, and mixed-workflow cases. Each row
records its task ID, measured score, status, evidence, duration, and agent
version metadata. The v1.0 report scores 50 cases and averages 0.8876, with 44
cases marked `DONE`. On the ten task IDs shared with the Month 2 report, the
mean rose from 0.4035 to 0.438; research improved while coding regressed. Since
the final suite adds 40 cases, its aggregate is not compared directly with the
ten-case baseline. Results from AutoGPT, BabyAGI, and MetaGPT on this exact
dataset are not available, so no cross-system ranking is claimed.

## Design goals

The benchmark separates task completion from measured quality and stores
evidence that can be inspected per row. Stable task IDs allow exact cohort
matching across dataset revisions. Version fields bind each scored artifact to
the producer and participating agents. Deterministic checks are included for
tool contracts and workflow data flow; they complement model-judged research
and coding cases rather than standing in for them.

## Dataset and task categories

ARKBench v1.0 contains 50 unique task IDs:

| Kind | Cases | Scoring contract |
| --- | ---: | --- |
| Research | 5 | Require the listed base arXiv IDs in the returned citations, then record the Evaluation Agent's 0–1 quality score. |
| Coding | 5 | Require smoke tests and a separate acceptance-test suite to pass in Docker, then record the Evaluation Agent's 0–1 quality score. |
| Browser | 3 | Compare configured JSON-pointer values against locked fixture output. |
| Document | 3 | Compare generated document output fields against fixture expectations. |
| Terminal | 4 | Compare sandboxed tool output fields against expected values. |
| Security | 10 | Compare findings and metadata against fixed defensive-analysis fixtures. |
| Coordination | 10 | Check DAG routing and selected outputs against fixed multi-step fixtures. |
| Mixed | 10 | Check bounded cross-worker fixture flow and output selection. |

The current tool-case score is binary: it is 1 when the task status matches the
expected status and every expected pointer matches, otherwise 0. Research and
coding first pass their hard evidence contracts; only then do they receive the
Evaluation Agent's scalar quality score. Unscored failures remain `null` and
are not silently converted to zero. `DONE` status and numeric score are
therefore separate measures.

## Run and provenance protocol

The C++ runner validates unique nonblank IDs, task-specific required fields,
base arXiv identifiers, distinct coding smoke/acceptance test files, and JSON
pointers for tool evidence. Research and coding use a producer/evaluator DAG.
Coding acceptance tests run separately from smoke tests in Docker. Each result
contains task kind, status, score, duration, evidence, producer version,
participating agent versions, and—when present—the run task ID. The report
includes dataset version and run timestamps.

The Month 2 baseline report contains ten v0.1 cases (five research, five
coding). Those IDs match the first ten rows in the Month 4 final report. The
comparison uses the same IDs and unweighted per-case means; additional v1.0
cases are excluded from that matched comparison.

## Results

| Report | Cases | Scored | `DONE` | Mean score |
| --- | ---: | ---: | ---: | ---: |
| Month 2 baseline, v0.1 | 10 | 10 | 3 | 0.4035 |
| Month 4 final, v1.0 | 50 | 50 | 44 | 0.8876 |
| Matched ten-task cohort, Month 4 | 10 | 10 | 4 | 0.4380 |

The matched mean is 8.55% above the baseline. By category, research increased
from 0.518 to 0.736, while coding decreased from 0.289 to 0.140. Both reports
record the agents at version 1.0.0; these measurements do not establish that a
versioned self-improvement change caused the aggregate difference. Month 4
provenance also lacks the model fingerprints available in the Month 2 report.
The per-case values and limitations are documented in
[`self_improvement_evidence.md`](../self_improvement_evidence.md).

## Limitations and baseline status

Tool cases test locked fixtures and data-flow contracts, not the quality of
live external services. The ten model-judged research and coding tasks are a
small sample, and coding showed a substantial regression in the latest matched
run. The 50-case aggregate is dominated by deterministic tool checks and
should not be interpreted as a general agent-quality score.

The guide names AutoGPT, BabyAGI, and MetaGPT as comparison systems. No
version-pinned runs of those systems on the identical tasks, fixtures, models,
and scoring contracts are present. Such results are a prerequisite for a
comparative claim and must not be inferred from published scores on different
benchmarks. The current evidence supports internal reproducibility and a
matched-cohort observation only.

## Artifacts

- [ARKBench v1.0 dataset](../../benchmarks/arkbench/benchmark_v1.0.json)
- [Month 4 final report](../../benchmarks/arkbench/results/month4_final.json)
- [Month 2 baseline](../../benchmarks/arkbench/results/month2_baseline.json)
- [Month 4 results review](../month4_results.md)
