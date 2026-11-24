# Self-improvement evidence: Month 2 to Month 4

## Matched benchmark cohort

The Month 2 and Month 4 reports contain the same five research and five coding
task IDs. Scores below are means over those five matched tasks; status is the
number of `DONE` rows.

| Category | Month 2 mean | Month 4 mean | Change | Month 2 DONE | Month 4 DONE |
| --- | ---: | ---: | ---: | ---: | ---: |
| Research | 0.518 | 0.736 | +0.218 (+42.1%) | 1/5 | 3/5 |
| Coding | 0.289 | 0.140 | -0.149 (-51.6%) | 2/5 | 1/5 |
| Matched ten-task mean | 0.4035 | 0.438 | +0.0345 (+8.55%) | 3/10 | 4/10 |

The aggregate increase masks the coding regression. The broader Month 4 v1.0
report scores 50/50 rows, has 44 `DONE` rows, and averages 0.8876; that mean is
not comparable with Month 2 because 40 cases were added, including deterministic
tool and workflow checks.

## What this does and does not show

This is a matched-task performance comparison, not causal evidence that
self-improvement raised quality. Both benchmark reports tag the research and
coding agents as version `1.0.0`, so there is no recorded prompt version bump
between the two runs. The Month 2 report records the model and prompt hashes;
the Month 4 report does not include equivalent per-agent model fingerprints.
Those run-condition differences prevent attribution of the gain to a specific
code, prompt, or model change.

The data therefore support only this claim: the matched ten-task mean rose by
8.55%, with research improving and coding regressing. A valid self-improvement
claim still needs a versioned prompt change, a fixed model and evaluation
setup, paired benchmark cases, and repeated runs for both baseline and candidate
versions. Day 92 adds a gated native implementation for that experiment, but it
has not been run because the current episodic database has no queued reflection
failures and local inference/Redis are unavailable.

## Sources

- [Month 2 baseline report](../benchmarks/arkbench/results/month2_baseline.json)
- [Month 4 final 50-task report](../benchmarks/arkbench/results/month4_final.json)
- [Month 4 results review](month4_results.md)
