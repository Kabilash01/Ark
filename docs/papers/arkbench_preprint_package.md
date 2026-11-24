# ARKBench preprint package

Status: prepared locally; no preprint has been uploaded.

## Proposed title

**ARKBench: Version-Tagged Evaluation of Native Agent Workflows**

## Abstract

ARKBench is a version-tagged evaluation suite for research and coding agents
alongside deterministic browser, document, terminal, security, coordination,
and mixed-workflow cases. Each case records completion status, score, evidence,
duration, and producer revision. The v1.0 report contains 50 cases, of which
44 are marked complete. The ten research and coding task IDs shared with the
Month 2 baseline have a mean score of 0.4035 in the baseline and 0.438 in the
Month 4 run. Research scores increased from 0.518 to 0.736, while coding scores
decreased from 0.289 to 0.140. This is a small observational comparison, not a
causal estimate: both runs report agent version 1.0.0, and the later run lacks
per-agent model fingerprints. The 50-case mean is not compared with the
ten-case baseline because the later suite adds 40 cases and is dominated by
deterministic tool checks. Results for AutoGPT, BabyAGI, or MetaGPT on the same
dataset are not available, so the paper makes no cross-system ranking claim.

## Files to include

- Manuscript: [`arkbench.md`](arkbench.md)
- Dataset: [`benchmark_v1.0.json`](../../benchmarks/arkbench/benchmark_v1.0.json)
- Month 2 report: [`month2_baseline.json`](../../benchmarks/arkbench/results/month2_baseline.json)
- Month 4 report: [`month4_final.json`](../../benchmarks/arkbench/results/month4_final.json)
- Results discussion: [`month4_results.md`](../month4_results.md)
- Matched-run limitations: [`self_improvement_evidence.md`](../self_improvement_evidence.md)

## Pre-upload checks still required

- Choose a repository/subject category and check arXiv's current submission
  requirements and account policy.
- Confirm author names, affiliations, author order, and any acknowledgements.
- Convert the manuscript to the required source format and verify figure
  readability, citations, and generated-data provenance.
- Archive the exact source revision and environment used for both reports.
- Independently recompute the matched means from the raw per-case rows.
- Check that the public artifact contains no credentials, private data, or
  unsupported baseline claims.

This package is not an arXiv submission and does not assert that the manuscript
has passed venue review or independent reproduction.
