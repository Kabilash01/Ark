# ARK OS Week 5 review

The native C++ runtime is implemented through Day 34. All 15 CTest suites
passed with a dedicated Redis-compatible instance and the local Docker Engine.

| Guide item | Verification |
| --- | --- |
| Retry and escalation | Transient failures retried. A failed dispatch escalated after three attempts, persisted the report, and ran reflection. The API tests cover the escalation event. |
| Agent versioning | Redis preserved revisions across connections. A CLI dispatch used the bumped evaluator prompt and model, then restored the original request and SQLite version after rollback. |
| Reflection | Success and escalation fixtures exercised automatic reflection, skill creation, and a durable pending recommendation tied to the agent revision. |
| Privacy and airgap | A private CLI coding task completed through loopback services and real Docker. An airgapped research task rejected an allowlisted literature provider before making a provider request. |
| Web search and file operations | Tavily fixtures verified request structure, citation validation, and privacy decisions. The file CLI read, listed, and wrote within a configured workspace. Tests rejected traversal and symlinks and checked the SQLite write audit. |

Day 32 priority scheduling also passed unit tests for priority, timestamps,
and domain rotation. The API integration test checked execution order and
the conflict event when two queued tasks competed.

Reflection, evaluation, and web search synthesis used model response fixtures
for these checks. Live reflection quality and a credentialed Tavily request
remain unverified. ARKBench baseline measurement is scheduled for Days 40–41.

```bash
cmake --build build --parallel
ARK_TEST_REDIS_PORT=16379 ARK_TEST_DOCKER_SOCKET=/var/run/docker.sock \
  ctest --test-dir build --output-on-failure
```

Use a Redis instance dedicated to testing. Runtime setup and commands are
documented in the [native C++ guide](native_cpp.md).
