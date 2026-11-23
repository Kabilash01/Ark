# Week 9 verification

| Day | Verified outcome |
| --- | --- |
| 57 | Both workers received the other's evidence. Two failed reconciliation rounds flagged the task for human review. A calibrated local reviewer detected all five controlled Docker comparisons. [Evidence](../benchmarks/conflicts/output_conflicts.json) |
| 58 | The offline scanner matched six of eight expected findings in the selected WebGoat sources and reported zero findings in two controls. Both misses were advanced SQL examples. [Scan results](../benchmarks/security/webgoat_source_results.json) |
| 59 | A 100-task exercise checked Redis expiry, a 10,000-episode active limit with archival, unused-vector eviction, and low-success skill eviction. |
| 60 | Median warm memory retrieval was 31.8 ms. The five-paper research run cited all five sources in 24.3 seconds, coding passed Docker checks in 6.2 seconds, and the evaluator returned its decision in 2.8 seconds. [Performance samples](../benchmarks/performance/day60.json) |
| 61 | A live task completed with Redis and Qdrant endpoints unavailable. SQLite vector search and process-local working memory remained available. Tests verified Qdrant replay and Groq fallback. |
| 62 | Chromium rendered dependency arrows, the running state with reduced motion, and the measured research score from the native history endpoint. [Paper figures](papers/ark_os_architecture.md) |

The review separates runtime checks from the measured quality of the output.
The WebGoat result covers the selected static checks. The latency profile
records one workload on one machine, and its evaluation decision was a retry.
The controlled conflict examples were used to calibrate the reviewer.
