#!/usr/bin/env python3
"""Exercise the running ARK dashboard and task API using only the standard library."""

import argparse
import json
import time
import urllib.error
import urllib.request


def get(base, path):
    with urllib.request.urlopen(base + path, timeout=5) as response:
        return response.status, response.headers.get("Content-Type", ""), response.read()


def main():
    parser = argparse.ArgumentParser(description="Smoke test a running ARK product server")
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    for path, marker in [("/", b"Agent roster"), ("/dashboard/app.js", b"agent-grid"),
                         ("/dashboard/styles.css", b"agent-grid")]:
        status, _, content = get(base, path)
        if status != 200 or marker not in content:
            raise RuntimeError(f"Dashboard asset failed: {path}")

    goal = "Analyze this CSV: sample,value\nA,1\nB,2"
    request = urllib.request.Request(base + "/task", data=json.dumps({"goal": goal}).encode(),
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        submitted = json.load(response)
    task_id = submitted["task_id"]
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        _, _, body = get(base, "/task/" + task_id)
        task = json.loads(body)
        if task["status"] in ("DONE", "FAILED", "CANCELLED"):
            break
        time.sleep(0.2)
    else:
        raise RuntimeError(f"Task did not finish: {task_id}")

    if task["status"] != "DONE":
        raise RuntimeError(f"Task failed: {json.dumps(task.get('output', {}))[:500]}")
    result = task["output"]["results"]["csv-analysis"]["output"]
    if result["rows"] != 2 or result["numeric_columns"][0]["mean"] != 1.5:
        raise RuntimeError("CSV analysis returned an unexpected result")
    _, _, body = get(base, "/task/" + task_id + "/events")
    events = json.loads(body)
    if not any(event.get("type", event.get("event")) == "task_completed" for event in events):
        raise RuntimeError("Completed task event was not recorded")

    invalid = urllib.request.Request(base + "/task", data=b'{"goal":""}',
                                     headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(invalid, timeout=5)
    except urllib.error.HTTPError as error:
        if error.code != 422:
            raise RuntimeError(f"Invalid goal returned HTTP {error.code}, expected 422") from error
    else:
        raise RuntimeError("Invalid goal was accepted")

    print(f"PASS dashboard, task API, CSV result, task events, validation (task {task_id})")


if __name__ == "__main__":
    main()
