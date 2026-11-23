import { inspectors } from "./inspectors.js";

const roster = [
  ["supervisor_agent", "Supervisor", "Management"],
  ["planner_agent", "Planner", "Management"],
  ["memory_agent", "Memory", "Management"],
  ["evaluation_agent", "Evaluation", "Management"],
  ["research_agent", "Research", "Research"],
  ["web_search_agent", "Web Search", "Research"],
  ["browser_agent", "Browser", "Research"],
  ["document_agent", "Document", "Research"],
  ["coding_agent", "Coding", "Engineering"],
  ["github_agent", "GitHub", "Engineering"],
  ["terminal_agent", "Terminal", "Engineering"],
  ["file_system_agent", "File System", "Engineering"],
  ["security_agent", "Security", "Security"],
  ["reflection_agent", "Reflection", "Reflection"],
  ["vision_agent", "Vision", "Intelligence"],
  ["ml_engineer_agent", "ML Engineer", "Intelligence"],
  ["devops_agent", "DevOps", "Intelligence"],
  ["data_analysis_agent", "Data Analysis", "Intelligence"],
  ["self_improvement_agent", "Self-Improvement", "Intelligence"],
  ["capability_discovery_agent", "Capability Discovery", "Intelligence"],
];

const labelById = new Map(roster.map(([id, label]) => [id, label]));
const eventNames = {
  task_created: "Task queued", task_started: "Task started", task_cancelled: "Task cancelled",
  task_completed: "Task completed", task_failed: "Task failed", task_escalated: "Task escalated",
  plan_ready: "Plan ready",
  agent_started: "Agent started", agent_done: "Agent finished", agent_thought: "Agent activity",
  tool_called: "Tool called", tool_result: "Tool result", memory_written: "Memory written",
  conflict_detected: "Conflict detected", version_updated: "Version updated",
};
const statusLabels = {
  idle: "Idle", running: "Running", offline: "Offline", unregistered: "Unregistered", unknown: "Unknown",
  pending: "Queued", done: "Done", failed: "Failed", cancelled: "Cancelled",
};

const byId = (id) => document.getElementById(id);
const eventList = byId("events");
const timeFormat = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23" });
const events = [];
const rosterWithExtras = () => roster.concat([...agents.keys()]
  .filter((id) => !labelById.has(id))
  .sort()
  .map((id) => [id, title(id.replace(/_agent$/, "")), "Registered"]));
let agents = new Map();
let tasks = [];
let agentsLoaded = false;
let tasksLoaded = false;
let filter = "all";
let paused = false;
let eventFrame;
let refreshTimer;
let refreshing = false;
let needsRefresh = false;
let reconnectDelay = 400;
const panel = inspectors({ create, request, badge, eventRow, title, labelById, getTasks: () => tasks, scheduleRefresh });

function create(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

function title(value) {
  return String(value).replace(/_/g, " ").replace(/^./, (letter) => letter.toUpperCase());
}

async function request(path, options) {
  const response = await fetch(path, { cache: "no-store", ...options });
  if (!response.ok) {
    const failure = await response.json().catch(() => ({}));
    const error = new Error(failure.detail || `Request failed (${response.status})`);
    error.status = response.status;
    throw error;
  }
  return response.json();
}

function statusFor(agent) {
  if (!agent) return agentsLoaded ? "unregistered" : "unknown";
  const status = String(agent.status).toLowerCase();
  if (status === "busy" || status === "running") return "running";
  if (status === "idle" || status === "online") return "idle";
  if (status === "offline") return "offline";
  return "unknown";
}

function badge(status) {
  const element = create("span", "status");
  element.dataset.type = status;
  element.append(create("i"), document.createTextNode(statusLabels[status] || title(status)));
  return element;
}

function updateList(list, children) {
  const focused = list.contains(document.activeElement) ? document.activeElement.dataset.target : undefined;
  list.replaceChildren(...children);
  if (focused) list.querySelector(`[data-target="${CSS.escape(focused)}"]`)?.focus({ preventScroll: true });
}

function agentCard([id, label, group]) {
  const agent = agents.get(id);
  const status = statusFor(agent);
  const card = create("article", "agent");
  card.dataset.agent = id;
  card.dataset.status = status;
  const header = create("button", "agent-header");
  header.type = "button";
  header.dataset.target = id;
  header.setAttribute("aria-label", `Version history for ${label}`);
  header.addEventListener("click", () => panel.versions(id, label, header));
  const mark = create("span", "agent-mark", label.split(/[ -]/).slice(0, 2).map((word) => word[0]).join("").toUpperCase());
  mark.setAttribute("aria-hidden", "true");
  const name = create("span", "agent-name", label);
  name.append(create("span", "agent-group", group));
  header.append(mark, name);
  const meta = create("div", "agent-meta");
  const version = agent?.version ? `v${agent.version}` : "—";
  const versionLabel = create("span", "version", version);
  versionLabel.setAttribute("aria-label", agent?.version ? `Version ${agent.version}` : "Version unavailable");
  meta.append(versionLabel, badge(status));
  card.append(header, meta);
  return card;
}

function renderAgents() {
  const visible = rosterWithExtras().filter(([id]) => filter === "all"
    || (filter === "registered" && agents.has(id))
    || (filter === "running" && statusFor(agents.get(id)) === "running"));
  byId("agent-count").textContent = `${visible.length} ${visible.length === 1 ? "agent" : "agents"}`;
  const empty = filter === "running" ? "No agents running." : "No agents registered yet.";
  updateList(byId("agent-grid"), visible.length ? visible.map(agentCard) : [create("p", "empty", empty)]);
  const statuses = [...agents.values()].map(statusFor);
  byId("online-count").textContent = agentsLoaded ? statuses.filter((status) => status === "idle" || status === "running").length : "—";
  byId("running-count").textContent = agentsLoaded ? statuses.filter((status) => status === "running").length : "—";
}

function time(value) {
  const date = new Date(value);
  const element = create("time", "", Number.isNaN(date.valueOf()) ? "—" : timeFormat.format(date));
  if (!Number.isNaN(date.valueOf())) {
    element.dateTime = date.toISOString();
    element.title = date.toLocaleString();
  }
  return element;
}

function taskRow(task) {
  const item = create("li", "task-item");
  item.dataset.task = task.task_id;
  const description = create("div");
  const taskTitle = create("button", "task-title", task.payload?.goal || task.task_id);
  taskTitle.type = "button";
  taskTitle.dataset.target = task.task_id;
  taskTitle.addEventListener("click", () => panel.task(task, taskTitle));
  description.append(taskTitle);
  const timestamp = time(task.timestamp);
  timestamp.className = "task-time";
  description.append(timestamp);
  const status = task.output?.cancelled ? "cancelled" : String(task.status).toLowerCase();
  item.append(description, badge(status));
  return item;
}

function renderTasks() {
  const active = tasks.filter((task) => task.status === "RUNNING" || task.status === "PENDING");
  const recent = tasks.filter((task) => task.status !== "RUNNING" && task.status !== "PENDING");
  const loading = "Waiting for tasks…";
  updateList(byId("active-tasks"), active.length ? active.map(taskRow) : [create("li", "empty", tasksLoaded ? "No active tasks." : loading)]);
  updateList(byId("recent-tasks"), recent.length ? recent.map(taskRow) : [create("li", "empty", tasksLoaded ? "No completed tasks yet." : loading)]);
  byId("active-count").textContent = tasksLoaded ? `${active.length} active` : "—";
  byId("queued-count").textContent = tasksLoaded ? active.filter((task) => task.status === "PENDING").length : "—";
  byId("completed-count").textContent = tasksLoaded ? recent.filter((task) => task.status === "DONE").length : "—";
}

async function refresh() {
  if (refreshing) {
    needsRefresh = true;
    return;
  }
  refreshing = true;
  const [agentResult, taskResult] = await Promise.allSettled([request("/agents"), request("/tasks?limit=50")]);
  const unavailable = [];
  if (agentResult.status === "fulfilled" && Array.isArray(agentResult.value)) {
    agents = new Map(agentResult.value.map((agent) => [agent.name, agent]));
    agentsLoaded = true;
    renderAgents();
  } else unavailable.push("agent registry");
  if (taskResult.status === "fulfilled" && Array.isArray(taskResult.value)) {
    tasks = taskResult.value;
    tasksLoaded = true;
    renderTasks();
  } else unavailable.push("task queue");
  const warning = byId("service-error");
  warning.hidden = !unavailable.length;
  warning.textContent = unavailable.length ? `Could not refresh the ${unavailable.join(" or ")}. Retrying automatically.` : "";
  refreshing = false;
  if (needsRefresh) {
    needsRefresh = false;
    scheduleRefresh();
  }
}

function scheduleRefresh() {
  if (refreshTimer) return;
  refreshTimer = setTimeout(() => {
    refreshTimer = undefined;
    void refresh();
  }, 150);
}

function eventRow(event) {
  const item = create("li", "event");
  const text = create("div");
  const report = event.report || {};
  const agent = report.agent ? labelById.get(report.agent) || title(report.agent) : "";
  const details = [agent, report.activity || report.service || report.tier || report.status,
    report.version ? `v${report.version}` : "", report.http_status || ""]
    .filter(Boolean);
  if (details.length === 0 && event.task_id) details.push(event.task_id);
  text.append(create("strong", "", eventNames[event.event] || title(event.event)));
  if (details.length) text.append(create("small", "", details.join(" · ")));
  item.append(time(event.timestamp), text);
  return item;
}

function renderEvents(follow = false) {
  const atEnd = follow || eventList.scrollHeight - eventList.scrollTop - eventList.clientHeight < 24;
  eventList.replaceChildren(...(events.length ? events.map(eventRow) : [create("li", "empty", "Waiting for runtime events…")]));
  byId("event-count").textContent = `${events.length} ${events.length === 1 ? "event" : "events"}`;
  if (atEnd) eventList.scrollTop = eventList.scrollHeight;
}

function onEvent(frame) {
  let event;
  try { event = JSON.parse(frame.data); } catch { return; }
  if (!event || typeof event.event !== "string") return;
  panel.onEvent(event);
  if (event.event.startsWith("task_") || event.event === "agent_started" || event.event === "agent_done" || event.event === "version_updated") {
    scheduleRefresh();
  }
  if (event.event === "task_finished") return;
  events.push(event);
  if (events.length > 150) events.shift();
  if (!paused && eventFrame === undefined) {
    eventFrame = requestAnimationFrame(() => {
      eventFrame = undefined;
      if (!paused) renderEvents();
    });
  }
}

function connection(state, label) {
  const badge = byId("connection");
  badge.dataset.state = state;
  badge.replaceChildren(create("i"), document.createTextNode(label));
}

function connect() {
  connection("connecting", "Connecting");
  const socket = new WebSocket(`${location.protocol === "https:" ? "wss:" : "ws:"}//${location.host}/ws`);
  socket.addEventListener("open", () => {
    reconnectDelay = 400;
    connection("connected", "Live");
    scheduleRefresh();
  });
  socket.addEventListener("message", onEvent);
  socket.addEventListener("close", () => {
    connection("disconnected", "Reconnecting");
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 5000);
  });
}

byId("agent-filters").addEventListener("click", (event) => {
  const button = event.target.closest("[data-filter]");
  if (!button) return;
  filter = button.dataset.filter;
  for (const tab of byId("agent-filters").querySelectorAll("button")) tab.setAttribute("aria-pressed", String(tab === button));
  renderAgents();
});

byId("pause-events").addEventListener("click", (event) => {
  paused = !paused;
  event.currentTarget.setAttribute("aria-pressed", String(paused));
  event.currentTarget.textContent = paused ? "Resume" : "Pause";
  if (!paused) renderEvents(true);
});
byId("clear-events").addEventListener("click", () => {
  events.length = 0;
  renderEvents();
});

const navigation = [...document.querySelectorAll(".topbar nav a")];
function updateNavigation() {
  for (const link of navigation) {
    if (link.hash === (location.hash || "#overview")) link.setAttribute("aria-current", "location");
    else link.removeAttribute("aria-current");
  }
}
window.addEventListener("hashchange", updateNavigation);
updateNavigation();
byId("open-memory").addEventListener("click", (event) => panel.memory(event.currentTarget));

const dialog = byId("task-dialog");
byId("open-task").addEventListener("click", () => dialog.showModal());
byId("close-task").addEventListener("click", () => dialog.close());
byId("cancel-task").addEventListener("click", () => dialog.close());
byId("task-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = byId("submit-task");
  const error = byId("task-error");
  error.hidden = true;
  button.disabled = true;
  try {
    await request("/task", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ goal: byId("goal").value.trim(), privacy: byId("private-task").checked }),
    });
    form.reset();
    dialog.close();
    scheduleRefresh();
  } catch (failure) {
    error.textContent = failure.message;
    error.hidden = false;
  } finally {
    button.disabled = false;
  }
});

renderAgents();
renderTasks();
renderEvents();
void refresh();
connect();
setInterval(scheduleRefresh, 5000);
