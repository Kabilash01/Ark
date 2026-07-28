import concurrent.futures

from core.agent_registry import AgentRegistry


class Router:
    """Dispatches a task DAG (as produced by the Planner) in dependency
    order, running each "layer" of mutually-independent tasks in parallel.
    The actual per-task work is delegated to a dispatch_fn(task) -> Any
    injected by the caller -- Router only owns ordering and parallelism,
    not what a task "doing work" means (that's Research/Coding/Evaluation
    agents, not yet built)."""

    def __init__(self, max_workers: int = 4):
        self._max_workers = max_workers

    def topological_layers(self, dag: dict) -> list[list[str]]:
        """Groups task_ids into ordered layers: every task_id in layer N
        depends only on task_ids in layers 0..N-1."""
        tasks = {task["task_id"]: task for task in dag["tasks"]}
        remaining = {task_id: set(task["depends_on"]) for task_id, task in tasks.items()}
        done: set[str] = set()
        layers: list[list[str]] = []

        while len(done) < len(tasks):
            layer = [task_id for task_id, deps in remaining.items() if task_id not in done and deps <= done]
            if not layer:
                raise ValueError("DAG has a cycle or an unresolvable dependency -- cannot order it")
            layers.append(layer)
            done.update(layer)

        return layers

    def dispatch(self, dag: dict, dispatch_fn, registry: AgentRegistry | None = None) -> dict:
        """Runs dispatch_fn(task) for every task, respecting dependency
        order -- a task is never dispatched until every task it depends_on
        has completed. Tasks in the same layer run concurrently. If a
        registry is given, tasks whose agent is OFFLINE are skipped
        (dispatch_fn is never called for them) rather than dispatched to a
        dead agent. Returns {task_id: dispatch_fn(task) result | skip marker}."""
        tasks = {task["task_id"]: task for task in dag["tasks"]}
        layers = self.topological_layers(dag)
        results: dict = {}

        for layer in layers:
            runnable = []
            for task_id in layer:
                agent_name = tasks[task_id]["agent"]
                record = registry.get_agent(agent_name) if registry is not None else None
                if record is not None and record.status == "OFFLINE":
                    results[task_id] = {"status": "SKIPPED", "reason": f"agent {agent_name} is OFFLINE"}
                    continue
                runnable.append(task_id)

            if not runnable:
                continue
            if len(runnable) == 1:
                task_id = runnable[0]
                results[task_id] = dispatch_fn(tasks[task_id])
                continue
            with concurrent.futures.ThreadPoolExecutor(max_workers=self._max_workers) as executor:
                future_to_id = {executor.submit(dispatch_fn, tasks[task_id]): task_id for task_id in runnable}
                for future in concurrent.futures.as_completed(future_to_id):
                    task_id = future_to_id[future]
                    results[task_id] = future.result()

        return results
