# Conflict Handling in ARK OS

## Draft status

This is the methods and instrumentation draft for the second ARK OS paper.
The initial capture in [`conflict_log.json`](../../research/conflict_data/conflict_log.json)
contains zero production cases. Earlier runs did not persist a complete
conflict journal, so this draft makes no frequency or resolution-rate claim.
The runtime collector can populate later rolling 30-day reports.

## Conflict classes

The runtime distinguishes four events:

1. **Resource contention** occurs when a worker waits for a bounded browser,
   terminal, or arXiv permit. The observation time is the wait duration until
   acquisition.
2. **Output disagreement** occurs when the supervisor's review identifies a
   contradiction between related research and code outputs. Reconciliation
   rounds are recorded; elapsed time is measured when the case resolves or is
   escalated for human review.
3. **Priority competition** occurs when multiple queued tasks compete for
   selection. Queue time ends when the resolver selects a task.
4. **Procedural-memory disagreement** occurs when a newly stored procedure
   conflicts with an existing one. Review latency is calculated from stored
   creation and review timestamps when a review has completed.

## Measurement and privacy

`ark-cpp conflict-report [report.json]` groups events from the preceding 30
days by conflict type and latest outcome, with mean elapsed time where timing
is available. Repeated status updates for a case count once. The SQLite journal
stores a pseudonymous case identifier, class, outcome, observation timestamp,
and optional elapsed time. It excludes prompts, outputs, source code, and
explanations. Controlled tests verify queue observation, case deduplication,
timing aggregation, and removal of raw details.

## Initial observation

The first committed capture has no observations in any class. This indicates
that no data was present in the new journal or recent procedural-memory review
history at capture time. It does not establish that no conflicts occurred in
earlier runs. A later empirical analysis should report the actual observation
window and event counts, separate test fixtures from production operation, and
avoid extrapolating from an incomplete collection period.

## Prospective analysis protocol

The collector currently emits observations rather than a complete exposure
denominator. A future report should therefore present distinct case counts and
outcome counts, not rates per task, request, or unit of runtime. For timed
cases, report the number of timed observations with the mean, and include a
median and percentile range computed from the raw case-level durations before
making latency comparisons. Keep each conflict class separate: resource waits
measure semaphore acquisition delay; priority waits use queue age at selection;
output conflicts record bounded reconciliation and escalation; memory events
record review outcomes only where review timestamps exist.

Use a predeclared window with its start and end timestamps. Report whether the
window has complete production logging and give the number of running tasks,
resource requests, and memory reviews only if those denominators are collected
and validated. Do not treat missing elapsed time as zero. Repeated updates to a
case are one case for case-level outcome counts; retain the event history when
describing state transitions. Analyze fixture events separately from production
observations. Any claim that the resolver improves outcomes requires a
comparison design with a defined baseline; the current journal has no such
control group.

The current zero-case capture cannot support a statistical comparison. The
protocol above is prospective and does not alter the reported initial result.
