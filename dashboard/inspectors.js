const tiers = [["working", "Working"], ["episodic", "Episodic"], ["semantic", "Semantic"], ["procedural", "Procedural"]];
const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: "medium", timeStyle: "short" });

export function inspectors({ create, request, badge, eventRow, title, labelById, getTasks, scheduleRefresh }) {
  const dialog = document.getElementById("inspector");
  const context = document.getElementById("inspector-context");
  const heading = document.getElementById("inspector-title");
  const body = document.getElementById("inspector-body");
  const loading = () => create("p", "empty", "Loading…");
  let current;
  let taskRefresh;

  function show(view, label, anchor) {
    current = { ...view, anchor };
    context.textContent = label;
    heading.textContent = current.title;
    body.replaceChildren(loading());
    if (!dialog.open) dialog.showModal();
    return current;
  }

  document.getElementById("close-inspector").addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => {
    const anchor = current?.anchor;
    current = undefined;
    clearTimeout(taskRefresh);
    if (anchor?.dataset.target) {
      document.querySelector(`button[data-target="${CSS.escape(anchor.dataset.target)}"]`)?.focus();
    } else if (anchor?.isConnected) anchor.focus();
  });

  function section(label, content) {
    const panel = create("section", "inspector-section");
    panel.append(create("h3", "", label), content);
    return panel;
  }

  function code(value) {
    const node = create("pre", "inspection-code", JSON.stringify(value, null, 2));
    node.tabIndex = 0;
    return node;
  }

  function download(task) {
    const link = create("a");
    const url = URL.createObjectURL(new Blob([JSON.stringify(task.output, null, 2)], { type: "application/json" }));
    link.href = url;
    link.download = `${task.task_id}.json`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function taskGraph(plan, results) {
    if (!Array.isArray(plan) || !plan.length) return create("p", "empty", "No plan available yet.");
    const graph = create("div", "dag");
    let remaining = [...plan];
    const placed = new Set();
    const dependencies = (task) => Array.isArray(task.depends_on) ? task.depends_on : [];
    while (remaining.length) {
      let layer = remaining.filter((task) => dependencies(task).every((id) => placed.has(id)));
      if (!layer.length) layer = remaining;
      const row = create("div", "dag-layer");
      for (const task of layer) {
        const node = create("article", "dag-node");
        const actor = labelById.get(task.agent) || title(task.agent);
        const taskStatus = results?.[task.task_id]?.status;
        node.append(create("strong", "", task.task_id), create("span", "version", actor));
        if (taskStatus) node.append(badge(taskStatus.toLowerCase()));
        const parents = dependencies(task);
        if (parents.length) node.append(create("p", "", `Depends on ${parents.join(", ")}`));
        placed.add(task.task_id);
        row.append(node);
      }
      graph.append(row);
      remaining = remaining.filter((task) => !placed.has(task.task_id));
    }
    return graph;
  }

  async function loadTask(view) {
    if (view.loading) return;
    view.loading = true;
    try {
      const root = `/task/${encodeURIComponent(view.id)}`;
      const [task, history] = await Promise.all([request(root), request(`${root}/events`)]);
      if (current !== view) return;
      heading.textContent = task.payload?.goal || task.task_id;
      const output = task.output && Object.keys(task.output).length ? task.output : null;
      const overview = create("div", "inspector-bar");
      const status = output?.cancelled ? "cancelled" : String(task.status).toLowerCase();
      overview.append(badge(status), create("span", "version", task.task_id));
      if (output) {
        const button = create("button", "secondary", "Download output");
        button.addEventListener("click", () => download(task));
        overview.append(button);
      }
      const log = create("ol", "inspect-events");
      const workerEvents = history.filter((event) => event.event !== "task_finished");
      log.replaceChildren(...(workerEvents.length ? workerEvents.map(eventRow) : [create("li", "empty", "No runtime events recorded.")]));
      const livePlan = history.findLast((event) => event.event === "plan_ready")?.report?.tasks;
      let liveResults = Object.create(null);
      for (const event of history) {
        if (event.event === "plan_ready") liveResults = Object.create(null);
        const id = event.report?.plan_task_id;
        if (id && event.event === "agent_started") liveResults[id] = { status: "RUNNING" };
        if (id && event.event === "agent_done") liveResults[id] = { status: event.report.status };
      }
      body.replaceChildren(overview, section("Task plan", taskGraph(output?.dag?.tasks || livePlan, output?.results || liveResults)),
        section("Run log", log), section("Output", output ? code(output) : create("p", "empty", "Waiting for task output…")));
    } catch (error) {
      if (current === view) body.replaceChildren(create("p", "task-error", error.message));
    } finally {
      view.loading = false;
    }
  }

  function versionTable(entries, active, view) {
    if (!entries.length) return create("p", "empty", "No recorded versions for this agent.");
    const table = create("table", "inspect-table");
    const header = table.createTHead().insertRow();
    for (const label of ["Version", "Changed", "Score", "Δ", "Change summary", "Action"]) {
      const cell = create("th", "", label);
      cell.scope = "col";
      if (label === "Δ") cell.setAttribute("aria-label", "Score change");
      header.append(cell);
    }
    const byVersion = new Map(entries.map((entry) => [entry.version, entry]));
    const number = (score) => typeof score === "number";
    const rows = table.createTBody();
    for (const item of [...entries].reverse()) {
      const row = rows.insertRow();
      const version = row.insertCell();
      version.append(create("span", "version", `v${item.version}`));
      if (active === item.version) version.append(create("span", "active-version", "Active"));
      const changed = new Date(item.created_at || item.timestamp || "");
      row.insertCell().textContent = Number.isNaN(changed.valueOf()) ? "—" : dateFormat.format(changed);
      const score = item.benchmark_score;
      row.insertCell().textContent = number(score) ? score.toFixed(2) : "—";
      const previous = byVersion.get(item.previous_version)?.benchmark_score;
      const delta = number(previous) && number(score) ? score - previous : null;
      row.insertCell().textContent = delta === null ? "—" : `${delta > 0 ? "+" : ""}${delta.toFixed(2)}`;
      row.insertCell().textContent = item.change_summary || "—";
      const action = row.insertCell();
      if (item.rollback_available && active && active !== item.version) {
        const button = create("button", "secondary", "Roll back");
        button.setAttribute("aria-label", `Roll back to version ${item.version}`);
        button.addEventListener("click", () => void rollback(view, item.version, button));
        action.append(button);
      }
    }
    const wrap = create("div", "table-wrap");
    wrap.append(table);
    return wrap;
  }

  async function loadVersions(view, message = "") {
    try {
      const [entries, registry] = await Promise.all([
        request(`/agents/${encodeURIComponent(view.id)}/versions`).catch((error) => {
          if (error.status === 404) return [];
          throw error;
        }), request("/agents"),
      ]);
      if (current !== view) return;
      const active = registry.find((agent) => agent.name === view.id)?.version;
      const note = create("p", "empty", message);
      note.hidden = !message;
      note.setAttribute("role", "status");
      body.replaceChildren(note, versionTable(entries, active, view));
    } catch (error) {
      if (current === view) body.replaceChildren(create("p", "task-error", error.message));
    }
  }

  async function rollback(view, version, button) {
    for (const action of body.querySelectorAll("button")) action.disabled = true;
    try {
      await request(`/agents/${encodeURIComponent(view.id)}/versions/${encodeURIComponent(version)}/rollback`, {
        method: "POST", headers: { "Content-Type": "application/json" }, body: "{}",
      });
      scheduleRefresh();
      if (current === view) await loadVersions(view, `Restored version ${version}.`);
    } catch (failure) {
      if (current === view) {
        const error = create("p", "task-error", failure.message);
        error.setAttribute("role", "alert");
        body.prepend(error);
        for (const action of body.querySelectorAll("button")) action.disabled = false;
        button.focus();
      }
    }
  }

  function field(name, control) {
    const wrapper = create("div", "memory-field");
    const label = create("label", "", name);
    control.id = `memory-${name.toLowerCase().replace(/\s+/g, "-")}`;
    label.htmlFor = control.id;
    wrapper.append(label, control);
    return wrapper;
  }

  function memoryEntries(data, tier, query) {
    const matches = ([key, item]) => !query || `${key} ${JSON.stringify(item)}`.toLowerCase().includes(query.toLowerCase());
    const entries = tier === "working" && data && !Array.isArray(data)
      ? Object.entries(data).filter(matches) : (Array.isArray(data) ? data.map((item, index) => [null, item, index]) : []);
    if (!entries.length) return [create("p", "empty", "No matching memory records.")];
    return entries.map(([key, item, index]) => {
      const entry = create("details", "memory-entry");
      const label = key ?? (item.summary || item.description || item.text || `Record ${index + 1}`);
      entry.append(create("summary", "", String(label).slice(0, 180)), code(item));
      return entry;
    });
  }

  function openMemory(anchor) {
    const view = show({ kind: "memory", title: "Memory browser" }, "Runtime storage", anchor);
    const form = create("form", "memory-form");
    const tier = create("select");
    for (const [value, label] of tiers) {
      const option = create("option", "", label);
      option.value = value;
      tier.append(option);
    }
    tier.value = "episodic";
    const query = create("input");
    query.type = "search";
    query.maxLength = 512;
    const id = create("input");
    id.setAttribute("list", "memory-task-ids");
    id.maxLength = 100;
    const suggestions = create("datalist");
    suggestions.id = "memory-task-ids";
    for (const task of getTasks()) {
      const option = create("option");
      option.value = task.task_id;
      suggestions.append(option);
    }
    const taskId = field("Task ID", id);
    taskId.append(suggestions);
    const run = create("button", "primary", "Search");
    run.type = "submit";
    form.append(field("Tier", tier), taskId, field("Query", query), run);
    const list = create("div", "memory-entries");
    list.setAttribute("aria-live", "polite");
    body.replaceChildren(form, list);
    let sequence = 0;

    async function load() {
      const generation = ++sequence;
      const selected = tier.value;
      const term = query.value.trim();
      taskId.hidden = selected !== "working";
      if (selected === "working" && !id.value.trim()) {
        list.replaceChildren(create("p", "empty", "Enter a task ID to browse working memory."));
        return;
      }
      list.replaceChildren(loading());
      const params = new URLSearchParams({ limit: "25" });
      if (selected === "working") params.set("task_id", id.value.trim());
      else if (term) params.set("query", term);
      try {
        const data = await request(`/memory/${selected}?${params}`);
        if (current === view && generation === sequence) list.replaceChildren(...memoryEntries(data.items, selected, term));
      } catch (error) {
        if (current === view && generation === sequence) list.replaceChildren(create("p", "task-error", error.message));
      }
    }

    tier.addEventListener("change", () => void load());
    form.addEventListener("submit", (event) => { event.preventDefault(); void load(); });
    void load();
  }

  return {
    task(task, anchor) {
      void loadTask(show({ kind: "task", id: task.task_id, title: task.payload?.goal || task.task_id }, "Task details", anchor));
    },
    versions(id, label, anchor) {
      void loadVersions(show({ kind: "versions", id, title: label }, "Version history", anchor));
    },
    memory: openMemory,
    onEvent(event) {
      if (current?.kind === "task" && current.id === event.task_id) {
        clearTimeout(taskRefresh);
        const view = current;
        taskRefresh = setTimeout(() => { if (current === view) void loadTask(view); }, 180);
      }
    },
  };
}
