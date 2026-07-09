# ARK OS — Complete Master Document
## From Problem Statement to Architecture, Models, Methods, Papers, and Datasets

**Who this is for:** Kabilash (E0323036) — B.Tech Final Year, SRIHER Chennai
**Purpose:** One document that explains everything about ARK OS from zero
**Version:** 1.0 — June 2026

---

# TABLE OF CONTENTS

```
Part 1 — The Why
  1. The One-Line Summary
  2. Problem Statement
  3. Purpose & Vision
  4. What Makes This Different

Part 2 — The What
  5. Features (What ARK OS Does)
  6. The 20 Agents Explained Simply
  7. SAGE Mode Explained Simply

Part 3 — The How (Concepts)
  8. Concept 1 — What is a Transformer?
  9. Concept 2 — What is an LLM?
  10. Concept 3 — What is a VLM? (Vision vs Text Agents)
  11. Concept 4 — What is RAG?
  12. Concept 5 — What is a ReAct Agent?
  13. Concept 6 — What is Multi-Agent Orchestration?
  14. Concept 7 — What is Memory in AI Systems?
  15. Concept 8 — What is Tool Use / Function Calling?
  16. Concept 9 — What is Mixture of Experts (MoE)?
  17. Concept 10 — What is Quantization?
  18. Concept 11 — What is LoRA / QLoRA Fine-Tuning?

Part 4 — The Models
  19. Model Architecture From Scratch
  20. Best Models in 2026 — Full Comparison
  21. Which Models ARK OS Uses and Why
  22. How to Train / Fine-Tune on Your GPU

Part 5 — The Research
  23. Original Papers (Foundations)
  24. Evaluation Methods
  25. Datasets

Part 6 — The Landscape
  26. Existing Similar Projects
  27. How ARK OS Differs

Part 7 — The Stack
  28. Complete Tech Stack with Reasoning
```

---

# PART 1 — THE WHY

## 1. The One-Line Summary

> ARK OS is an operating system for AI agents — 20 specialized AI workers that share memory, communicate through a standard protocol, and autonomously handle research, coding, cybersecurity, and scientific discovery without human prompting at every step.

If that still feels abstract, here is a concrete analogy:

```
Traditional AI Tools:
You ask ChatGPT a question → it answers → you move on
You ask Copilot to write code → it writes → you test manually
You run a security scanner → you read the report → you fix manually

ARK OS:
You give a goal: "Research quantum computing papers,
                  implement the most promising algorithm,
                  scan it for vulnerabilities,
                  benchmark it and write a report"

ARK OS does all of this. Autonomously. While you sleep.
```

---

## 2. Problem Statement

### The Core Problem

**Knowledge work is fragmented.** A researcher today uses:
- ChatGPT to summarize papers
- GitHub Copilot to write code
- A separate security scanner
- A different experiment tracker
- Manual report writing

None of these systems talk to each other. None remember what happened yesterday. None can pursue a goal across multiple steps without human hand-holding at every transition.

### The Deeper Problem

Even the best autonomous AI tools (AutoGPT, Devin, BabyAGI) have four fundamental flaws:

```
Flaw 1 — Single Domain
  Devin does code only.
  AutoGPT does general tasks but badly.
  Nobody does: research + code + security + science together.

Flaw 2 — No Persistent Memory
  Every conversation starts from zero.
  An agent that fixed a yfinance bug yesterday
  hits the exact same bug tomorrow.
  It has no memory. It relearns everything.

Flaw 3 — No Inter-Agent Communication Standard
  If you build Agent A and Agent B,
  they cannot talk to each other without custom integration.
  No standard protocol exists for agent-to-agent messages.

Flaw 4 — No Self-Improvement
  Existing agents do not learn from their mistakes.
  They have no mechanism to get better at a task
  just because they did it 100 times before.
```

### The Research Gap

In academic literature, the following has never been built as a unified system:
- A **kernel-based multi-agent architecture** (not just a pipeline)
- With a **frozen inter-agent communication protocol** (AgentSpec)
- With a **four-tier memory system** shared across all agents
- With a **self-improvement loop** (Reflection → Self-Improvement cycle)
- With a **scientific discovery mode** (SAGE) that closes literature → hypothesis → experiment → paper

Each component has been studied in isolation. Nobody combined them into a coherent OS.

---

## 3. Purpose & Vision

### Immediate Purpose (6 months)
Build a working 20-agent autonomous OS that handles the four domains of knowledge work:
- 📚 Research (reading papers, synthesizing knowledge)
- 💻 Software Engineering (writing, testing, fixing code)
- 🔐 Cybersecurity (scanning, detecting threats, auditing)
- ⚗️ Scientific Discovery (hypotheses, experiments, findings)

### Academic Purpose
Extract 4–5 publishable research papers from the system — each contributing something new to the multi-agent AI field.

### 15-Year Purpose
Every company, lab, and government doing knowledge work will run systems like ARK OS. Building the reference architecture in 2026 means you shaped the field.

---

## 4. What Makes This Different

```
┌─────────────────────────────────────────────────────────────────┐
│              COMPARISON WITH EXISTING SYSTEMS                   │
├────────────────┬───────┬───────┬────────┬────────┬─────────────┤
│ Feature        │AutoGPT│BabyAGI│ Devin  │MetaGPT │   ARK OS   │
├────────────────┼───────┼───────┼────────┼────────┼─────────────┤
│ Multi-domain   │  ❌   │  ❌   │  ❌    │  ⚠️   │     ✅     │
│ Persistent mem │  ❌   │  ❌   │  ❌    │  ❌   │     ✅     │
│ Agent protocol │  ❌   │  ❌   │  ❌    │  ⚠️   │     ✅     │
│ Kernel model   │  ❌   │  ❌   │  ❌    │  ❌   │     ✅     │
│ Self-improve   │  ❌   │  ❌   │  ❌    │  ❌   │     ✅     │
│ Science mode   │  ❌   │  ❌   │  ❌    │  ❌   │     ✅     │
│ Fully local    │  ❌   │  ❌   │  ❌    │  ❌   │     ✅     │
│ Open source    │  ✅   │  ✅   │  ❌    │  ✅   │     ✅     │
│ Published eval │  ❌   │  ❌   │  ❌    │  ⚠️   │     ✅     │
└────────────────┴───────┴───────┴────────┴────────┴─────────────┘
```

---

# PART 2 — THE WHAT

## 5. Features (What ARK OS Does)

### Feature Set A — Autonomous Research
- Reads and indexes papers from arXiv, Semantic Scholar, PubMed
- Builds a searchable knowledge base from scientific literature
- Writes literature review sections automatically
- Identifies research gaps and open problems
- Generates hypotheses ranked by novelty and feasibility (SAGE Mode)

### Feature Set B — Autonomous Software Engineering
- Writes complete, tested code from natural language specifications
- Debugs runtime errors autonomously (detects → diagnoses → patches → reruns)
- Reviews code for quality, style, and security issues
- Creates pull requests, triages GitHub issues, monitors repo health
- Evolves a codebase over time without human hand-holding

### Feature Set C — Autonomous Cybersecurity
- Monitors CVE databases for new vulnerabilities
- Scans codebases with Semgrep and Bandit for known vulnerability patterns
- Generates structured security audit reports
- Simulates red-team attack scenarios on approved targets (locally only)
- Tracks OWASP Top 10 compliance

### Feature Set D — Scientific Discovery (SAGE Mode)
- Reads 50+ papers on a domain → identifies what is not yet known
- Generates hypotheses sorted by: novelty score × feasibility score
- Designs computational experiments to test each hypothesis
- Runs experiments, tracks results in MLflow
- Writes a LaTeX paper draft with proper citations
- Simulates peer reviewer feedback and triggers revision loop

### Feature Set E — System Intelligence
- Learns from past tasks (gets better with every run)
- Knows what it can and cannot do (Capability Discovery)
- Routes tasks to the right agent automatically
- Handles failures gracefully with bounded retry loops
- Runs entirely locally — no data leaves your machine

---

## 6. The 20 Agents Explained Simply

Think of each agent as one specialist employee:

```
MANAGEMENT LAYER (always running)
├── Supervisor     — The CEO. Every task goes through here first.
│                    Routes work, resolves conflicts, final authority.
├── Planner        — The Project Manager. Takes a goal, makes a plan.
│                    "Do task A, then B and C in parallel, then D."
├── Memory         — The Librarian. Remembers everything for everyone.
│                    No agent touches storage directly. Only Memory does.
├── Evaluation     — The QA Lead. Scores every piece of work 0–1.
│                    Below threshold → retry. Above → ship it.
└── Agent Registry — The HR System. Tracks who exists and what they do.

RESEARCH FLOOR (Months 1–3)
├── Research       — Reads scientific papers, builds knowledge base.
├── Web Search     — Finds current information on the live web.
├── Browser        — Actually opens websites and interacts with them.
└── Document       — Reads and writes PDFs, Word docs, spreadsheets.

ENGINEERING FLOOR (Months 2–4)
├── Coding         — Writes, tests, and fixes code autonomously.
├── GitHub         — Manages repositories, PRs, issues.
├── Terminal       — Runs commands in the terminal safely.
└── File System    — Reads, writes, organizes files on disk.

SECURITY FLOOR (Month 4)
└── Security       — Scans for vulnerabilities, monitors threats.

REFLECTION LAYER (Month 4)
└── Reflection     — After every task: what worked? Updates strategies.

INTELLIGENCE LAYER (Months 5–6)
├── Vision         — Understands screenshots and images.
├── ML Engineer    — Runs machine learning experiments.
├── DevOps         — Manages Docker, CI/CD, deployments.
├── Data Analysis  — Analyzes data, creates charts, writes reports.
├── Self-Improve   — Makes the whole system smarter over time.
└── Capability Disc— Answers: "Can ARK OS do X?"
```

---

## 7. SAGE Mode Explained Simply

SAGE = **Scientific Autonomous Generation Engine**

When you activate SAGE, ARK OS stops doing tasks and starts generating knowledge.

```
Normal ARK OS:   User gives task → ARK OS executes → Known answer
SAGE Mode:       ARK OS reads literature → Forms hypothesis
                 → Designs experiment → Runs it → Finds new knowledge
                 → Writes paper draft → Unknown → Known
```

SAGE is not a separate system. It is the same 20 agents, orchestrated differently.
Three small new components connect them into a discovery loop:
1. **Hypothesis Engine** — reads papers, finds gaps, ranks hypotheses
2. **Experiment Designer** — translates hypothesis into runnable code
3. **Scientific Report Generator** — assembles findings into LaTeX paper

---

# PART 3 — THE HOW (CONCEPTS)

## 8. Concept 1 — What is a Transformer?

The Transformer is the architecture behind every modern AI model you have used.
Introduced in the paper **"Attention Is All You Need" (Vaswani et al., 2017)**.

### The Core Idea

Before Transformers, AI processed sequences one word at a time (RNNs, LSTMs).
The Transformer processes the entire sequence at once and asks:
**"How much should every word pay attention to every other word?"**

### Self-Attention (The Key Operation)

```
Input sentence: "The cat sat on the mat because it was tired"

For the word "it":
  - "it" attends to "cat" with weight 0.85   ← cat is what "it" refers to
  - "it" attends to "mat" with weight 0.05
  - "it" attends to "tired" with weight 0.10

These attention weights are LEARNED during training.
The model learns which words are relevant to which other words.
```

Mathematically:
```
Attention(Q, K, V) = softmax(QK^T / √d_k) × V

Where:
  Q = Query  (what this word is asking for)
  K = Key    (what each word offers)
  V = Value  (the actual content of each word)
  d_k = dimension of the key vectors (scaling factor)
```

### Transformer Block Structure

```
Input Tokens
      │
      ▼
[Token Embeddings]         ← convert words to vectors (e.g., 4096-dim)
      │
      ▼
[Positional Encoding]      ← inject position information
      │
      ▼
┌─────────────────────┐    ← this block repeats N times (e.g., 32 layers)
│  Multi-Head         │
│  Self-Attention     │    ← multiple attention heads in parallel
│         +           │
│  Feed-Forward       │    ← two linear layers with GELU activation
│  Network            │
│         +           │
│  Layer Norm         │    ← stabilize training
│  + Residual         │    ← add input back (helps gradients flow)
└─────────────────────┘
      │
      ▼
[Output Projection]        ← project to vocabulary size
      │
      ▼
[Softmax → Probabilities]  ← probability over all possible next tokens
```

### Why It's Revolutionary

```
RNNs/LSTMs (before 2017):
  - Sequential processing → slow, cannot parallelize
  - Struggle with long-range dependencies
  - "The cat... [500 words later]... it" → forgets "cat"

Transformers (2017+):
  - Parallel processing → GPU-friendly, fast training
  - Global attention → "it" directly attends to "cat" regardless of distance
  - Scales with compute → more params + more data = better model
```

---

## 9. Concept 2 — What is an LLM?

A **Large Language Model** is a Transformer trained to predict the next token
on massive amounts of text — then fine-tuned to follow instructions.

### Training Pipeline

```
Stage 1 — Pre-training (very expensive, done once by labs)
  Data: Internet text, books, code, papers (trillions of tokens)
  Task: predict the next word (autoregressive language modeling)
  Objective: minimize cross-entropy loss on next-token prediction
  Cost: Millions of dollars in compute (you don't do this)

Stage 2 — Supervised Fine-Tuning (SFT)
  Data: High-quality (instruction, response) pairs
  Task: learn to follow instructions
  Cost: Thousands of dollars (you CAN do this cheaply with LoRA)

Stage 3 — RLHF / DPO (Alignment)
  Data: Human preference rankings of model outputs
  Task: learn to prefer helpful, harmless, honest responses
  Cost: Moderate (DPO is cheaper than full RLHF)
```

### How Text Generation Works

```python
# Auto-regressive generation (simplified)
prompt = "The capital of France is"
tokens = tokenize(prompt)  # [498, 2982, 310, 3444, 338]

for step in range(max_new_tokens):
    logits = model(tokens)          # forward pass through Transformer
    next_token = sample(logits[-1]) # sample from probability distribution
    tokens.append(next_token)
    if next_token == EOS_TOKEN:
        break

# Output: "The capital of France is Paris."
```

### Tokenization

```
Text → Tokens (sub-word units, not full words)

"unbelievable" → ["un", "believ", "able"]   (3 tokens)
"cat"          → ["cat"]                     (1 token)
"ARK OS"       → ["ARK", " OS"]              (2 tokens)

Why: handles unknown words, multilingual, efficient vocabulary
Common tokenizers: BPE (GPT), SentencePiece (LLaMA), Tiktoken (OpenAI)
```

### Context Window

```
Context window = maximum number of tokens the model can "see" at once

GPT-2 (2019):   1,024 tokens
LLaMA-1 (2023): 2,048 tokens
LLaMA-3 (2024): 128,000 tokens
Qwen2.5 (2024): 128,000 tokens
Gemma 4 (2025): 1,000,000 tokens (1M!)

Why it matters for ARK OS:
  Memory Agent injects context into every agent call.
  Longer context = more relevant history per agent = smarter decisions.
```

---

## 10. Concept 3 — What is a VLM? (Vision vs Text Agents)

A **Vision-Language Model (VLM)** is a model that understands both images and text.

### Architecture: How Vision Gets Into a Text Model

```
Image Input
    │
    ▼
┌───────────────────┐
│   Vision Encoder  │   ← converts image to a grid of feature vectors
│   (e.g. SigLIP)   │   ← input: 224×224 or 448×448 pixels
│                   │   ← output: N patch embeddings (e.g. 256 × 1152-dim)
└───────────────────┘
    │
    ▼
┌───────────────────┐
│   Projection MLP  │   ← bridges visual space → language space
│   (2 linear layers│   ← input: 256 × 1152-dim visual features
│    + GELU)        │   ← output: 256 × 4096-dim (matches LLM dimension)
└───────────────────┘
    │
    ▼
┌───────────────────┐
│   Language Model  │   ← text tokens + visual tokens concatenated
│   (e.g. Qwen2.5)  │   ← LLM processes them jointly
│                   │   ← generates text response about the image
└───────────────────┘
    │
    ▼
Text Output ("The chart shows a 40% increase in Q3...")
```

### Text Agent vs Vision Agent — Key Differences

```
┌─────────────────────────────────────────────────────────────────┐
│                 TEXT AGENT vs VISION AGENT                      │
├──────────────────┬──────────────────────────────────────────────┤
│ Dimension        │ Text Agent          │ Vision Agent           │
├──────────────────┼─────────────────────┼────────────────────────┤
│ Input            │ Text only           │ Image + Text           │
│ Modality         │ Single              │ Multi-modal            │
│ Architecture     │ Transformer only    │ ViT encoder + LLM      │
│ Token types      │ Text tokens         │ Visual tokens + text   │
│ Context          │ Text context window │ Image patches + text   │
│ Training data    │ Text corpora        │ Image-text pairs       │
│ Use in ARK OS    │ Most agents         │ Vision Agent only      │
│ VRAM needed      │ Less                │ ~2GB more for encoder  │
│ Speed            │ Faster              │ Slightly slower        │
│ Examples         │ GPT-4, Qwen2.5      │ GPT-4V, Qwen2-VL       │
└──────────────────┴─────────────────────┴────────────────────────┘
```

### What Vision Agents See (Patch Embeddings)

```
Input Image (448×448 pixels)
       │
       ▼
Split into patches (e.g., 14×14 pixels each)
       │
       ▼
32×32 = 1024 patches for 448×448 image
       │
       ▼
Each patch → a 1152-dimensional vector (SigLIP)
       │
       ▼
1024 vectors → projected → inserted into LLM context as "visual tokens"
```

### Best Vision Encoders in 2026

```
Model          Params   Resolution   Strength           Used In
SigLIP-400M    400M     224/384px   Best text-image    PaliGemma, LLaVA
InternViT-6B   6B       448px       Best fine detail   InternVL-2
EVA-CLIP-18B   18B      448px       Strongest overall  EVA series
CLIP ViT-L/14  307M     224px       Battle-tested      LLaVA original
DFN-CLIP       307M     224px       Best zero-shot     OpenCLIP
```

---

## 11. Concept 4 — What is RAG?

**Retrieval Augmented Generation** — the model retrieves relevant documents before generating its answer.

### Why RAG Exists

```
Problem with pure LLMs:
  - Knowledge is frozen at training cutoff
  - Cannot access your private documents
  - Hallucinations when asked about specific facts

RAG solution:
  - Store documents in a vector database
  - At query time, retrieve relevant chunks
  - Inject them into the LLM context
  - LLM generates answer grounded in retrieved facts
```

### RAG Pipeline

```
INDEXING (offline, done once)
Documents → Chunking → Embedding Model → Vector DB
(papers)    (500 tokens  (sentence-      (Qdrant)
             per chunk)   transformers)

RETRIEVAL (online, at query time)
User query → Embed query → Vector similarity search → Top-K chunks
"RSI strategy" → [0.2, 0.8, ...] → cosine similarity → ["RSI definition",
                                                           "RSI trading paper",
                                                           "RSI parameters"]
GENERATION
Retrieved chunks + Original query → LLM → Grounded answer

Final prompt to LLM:
"Context: [chunk1] [chunk2] [chunk3]
 Question: What is the best RSI threshold for mean reversion?
 Answer based on the provided context."
```

### Embedding Models (Turn Text into Vectors)

```
Model                  Dims   Speed    Best For
all-MiniLM-L6-v2      384    Fast     Quick prototyping
BAAI/bge-large-en     1024   Medium   ARK OS (best balance)
text-embedding-3-small 1536  API only OpenAI (paid)
E5-large-v2           1024   Medium   Scientific text
Nomic-embed-text-v1.5 768    Fast     Long documents
```

### RAG in ARK OS

```
Research Agent uses RAG to:
  - Index all papers it has ever read (Qdrant)
  - Retrieve relevant prior knowledge for every new query
  - Ground hypothesis generation in actual literature
  - Avoid re-reading papers it already processed
```

---

## 12. Concept 5 — What is a ReAct Agent?

**ReAct = Reasoning + Acting**. Introduced in **"ReAct: Synergizing Reasoning and Acting in Language Models" (Yao et al., 2022)**.

### The Core Loop

```
Traditional LLM: Input → Output (one shot)

ReAct Agent:
Thought 1:  "I need to find the current price of Google stock"
Action 1:   web_search("GOOGL current stock price")
Observation 1: "GOOGL: $178.43 as of June 2026"

Thought 2:  "Now I need to calculate RSI for the last 14 days"
Action 2:   code_execute("import yfinance; df = yf.download('GOOGL', period='1mo')")
Observation 2: "RSI: 28.4 (oversold)"

Thought 3:  "RSI is below 30. This is a buy signal per our strategy."
Action 3:   report_write("Buy signal detected: GOOGL RSI=28.4")
Observation 3: "Report written to /outputs/signal_report.html"

Final Answer: "Buy signal confirmed. Report generated."
```

### Why ReAct Changed Everything

```
Before ReAct:
  LLM could think but not act
  LLM could act but not explain why

ReAct:
  Think → Act → Observe → Think → Act → ...
  The interleaving of reasoning and action
  lets the model correct itself in real time
```

### ReAct in ARK OS

Every ARK OS agent IS a ReAct agent. When the Coding Agent hits a bug:
```
Thought: "The script failed with ModuleNotFoundError for 'plotly'"
Action:  terminal_execute("pip install plotly")
Obs:     "Successfully installed plotly-5.20.0"
Thought: "Now re-run the original script"
Action:  terminal_execute("python backtest.py")
Obs:     "Report generated: backtest_report.html"
Answer:  "Fixed. Report at /outputs/backtest_report.html"
```

---

## 13. Concept 6 — What is Multi-Agent Orchestration?

A **multi-agent system** is multiple AI agents working together, each with a specialization, coordinated by an orchestrator.

### Why Multi-Agent Over Single-Agent?

```
Single Agent Problems:
  - Context window overflow on long tasks
  - Cannot specialize deeply (jack of all trades)
  - One failure kills the entire task
  - Cannot parallelize

Multi-Agent Solutions:
  - Each agent handles one domain deeply
  - Tasks split across agents' context windows
  - One agent failure → retry, not full collapse
  - Independent subtasks run in parallel
```

### Orchestration Patterns

```
Pattern 1 — Sequential Chain (simple, rigid)
  A → B → C → D
  Output of each becomes input of next
  Used for: simple pipelines

Pattern 2 — DAG Execution (ARK OS uses this)
  Planner creates: A → (B ∥ C) → D → E
  B and C run in parallel
  D waits for both B and C to finish
  Used for: complex multi-step goals

Pattern 3 — Supervisor-Worker (ARK OS uses this)
  Supervisor receives goal
  Assigns subtasks to specialist workers
  Workers report back
  Supervisor aggregates and decides next step
  Used for: dynamic task allocation

Pattern 4 — Role-Playing (MetaGPT style)
  Agents play defined roles (PM, Dev, QA, Designer)
  Communicate through structured documents
  Used for: software development simulation
```

### LangGraph — ARK OS Orchestration Engine

```python
# LangGraph models agent workflows as state machines

from langgraph.graph import StateGraph, END

# Define the state that flows between agents
class ARKState(TypedDict):
    task: str
    plan: list
    results: dict
    current_agent: str
    status: str

# Build the workflow graph
workflow = StateGraph(ARKState)

# Add agent nodes
workflow.add_node("supervisor", supervisor_agent)
workflow.add_node("planner", planner_agent)
workflow.add_node("research", research_agent)
workflow.add_node("coding", coding_agent)
workflow.add_node("evaluation", evaluation_agent)

# Add edges (transitions)
workflow.add_edge("supervisor", "planner")
workflow.add_conditional_edges(
    "planner",
    route_to_workers,          # function that decides next agent
    {
        "needs_research": "research",
        "needs_coding": "coding",
    }
)
workflow.add_edge("research", "evaluation")
workflow.add_edge("coding", "evaluation")
workflow.add_conditional_edges(
    "evaluation",
    check_quality,             # if score < 0.7, retry; else END
    {"retry": "planner", "done": END}
)
```

---

## 14. Concept 7 — What is Memory in AI Systems?

Without memory, every agent starts from zero. With memory, agents accumulate knowledge.

### The Four Memory Types (ARK OS)

```
┌────────────────────────────────────────────────────────────────┐
│ MEMORY TYPE    │ ANALOGY          │ WHAT IT STORES             │
├────────────────┼──────────────────┼────────────────────────────┤
│ Working        │ Your desk right  │ Current task context,      │
│ (Redis)        │ now              │ active variables, temp data │
│                │                  │ Duration: current task only │
├────────────────┼──────────────────┼────────────────────────────┤
│ Episodic       │ Your diary       │ "On June 15, I ran a       │
│ (SQLite)       │                  │ backtest on GOOGL. RSI     │
│                │                  │ strategy returned 12.3%"   │
│                │                  │ Duration: permanent         │
├────────────────┼──────────────────┼────────────────────────────┤
│ Semantic       │ Your knowledge   │ Facts, paper abstracts,    │
│ (Qdrant)       │ base             │ code patterns, definitions │
│                │                  │ Retrieved by similarity     │
├────────────────┼──────────────────┼────────────────────────────┤
│ Procedural     │ Your skills      │ "How to fix MultiIndex bug"│
│ (JSON+FAISS)   │                  │ "How to call arXiv API"    │
│                │                  │ Retrieved by intent        │
└────────────────┴──────────────────┴────────────────────────────┘
```

### How Vector Memory Works (Qdrant)

```
Storage:
  Text: "RSI below 30 indicates oversold conditions"
  → Embed: all-MiniLM → [0.23, -0.87, 0.12, ..., 0.45]  (384-dim vector)
  → Store in Qdrant with metadata {source: "paper_123", date: "2026-06-15"}

Retrieval:
  Query: "when should I buy based on RSI?"
  → Embed query → [0.21, -0.83, 0.15, ..., 0.42]
  → Cosine similarity against all stored vectors
  → Return top-3 most similar: ["RSI below 30 = oversold", ...]
```

### Why ARK OS Has 4 Tiers (Not Just One)

```
If you used only vector DB for everything:
  - Working memory: too slow (vector search latency)
  - Episodic: vectors don't preserve chronology well
  - Procedural: skills need exact retrieval, not fuzzy

Each tier is optimized for its specific retrieval pattern:
  Working → exact key lookup (Redis O(1))
  Episodic → time-range queries (SQLite indexed)
  Semantic → similarity search (Qdrant ANN)
  Procedural → intent matching (FAISS + reranker)
```

---

## 15. Concept 8 — What is Tool Use / Function Calling?

**Tool use** is the ability of an LLM to call external functions instead of just generating text.

### How It Works

```python
# You tell the LLM what tools are available
tools = [
    {
        "name": "web_search",
        "description": "Search the web for current information",
        "parameters": {
            "query": {"type": "string", "description": "search query"}
        }
    },
    {
        "name": "run_code",
        "description": "Execute Python code",
        "parameters": {
            "code": {"type": "string", "description": "Python code to run"}
        }
    }
]

# LLM generates a tool call instead of text:
# {"tool": "web_search", "parameters": {"query": "GOOGL stock price today"}}

# Your code executes the tool and returns result to LLM
# LLM uses result to generate final answer
```

### Tool Use in ARK OS

```
Each ARK OS agent has a toolbox:

Research Agent tools:
  arxiv_search(query, max_results)
  semantic_scholar_search(query)
  get_paper_full_text(paper_id)
  store_to_knowledge_base(paper, summary)

Coding Agent tools:
  write_file(path, content)
  run_python(code) → stdout, stderr
  install_package(name)
  read_file(path)
  git_commit(message)

Security Agent tools:
  run_semgrep(target_dir)
  run_bandit(target_file)
  check_cve(package_name, version)
  generate_report(findings)
```

---

## 16. Concept 9 — What is Mixture of Experts (MoE)?

MoE makes large models feasible on small hardware.

### The Insight

```
Dense Model (Traditional):
  Every token → ALL parameters activate → expensive

MoE Model:
  Every token → Router picks best 2-8 "expert" networks
             → Only those experts activate
             → Same VRAM as small model
             → Same knowledge as large model
```

### Architecture

```
Input Token
    │
    ▼
┌──────────────────────────────────────────────────────┐
│  TRANSFORMER BLOCK (MoE version)                     │
│                                                      │
│  Self-Attention (dense, always active)               │
│        │                                             │
│        ▼                                             │
│  ┌────────────┐                                      │
│  │   Router   │ ← small linear layer                 │
│  │  (Gating)  │   picks top-2 experts from N        │
│  └─────┬──────┘                                      │
│        │                                             │
│   ┌────▼────────────────────────────┐                │
│   │  Expert 1  Expert 2  Expert 3  │  ← only 2      │
│   │   [ON]      [ON]      [off]    │    activate     │
│   │  Expert 4  Expert 5  Expert 6  │                 │
│   │   [off]     [off]     [off]    │                 │
│   └────────────────────────────────┘                │
│        │                                             │
│  Weighted sum of active expert outputs               │
└──────────────────────────────────────────────────────┘
```

### Gemma 4 26B-A4B Explained

```
26B = total parameters (all experts combined)
A4B = active 4B parameters per token (only top experts fire)
MoE = uses Mixture of Experts architecture

Effective VRAM: ~4-5GB (only active params need to be in VRAM)
Effective intelligence: ~26B (all expert knowledge available)
Result: 26B knowledge on 8GB VRAM hardware
```

### Why This Matters for ARK OS

ARK OS replaces Qwen2.5-7B with Gemma 4 26B-A4B for general tasks:
- Same VRAM cost
- 3–4× stronger reasoning
- Better instruction following
- Near-state-of-the-art performance on consumer hardware

---

## 17. Concept 10 — What is Quantization?

Quantization reduces model size by using lower precision numbers.

### Precision Levels

```
FP32 (32-bit float): Full precision
  - Each weight: 4 bytes
  - 7B model: ~28 GB
  - Not feasible for consumer hardware

FP16 (16-bit float): Half precision
  - Each weight: 2 bytes
  - 7B model: ~14 GB
  - Still too large for 8GB VRAM

INT8 (8-bit integer): 
  - Each weight: 1 byte
  - 7B model: ~7 GB
  - Borderline feasible

INT4 (4-bit integer):
  - Each weight: 0.5 bytes
  - 7B model: ~3.5 GB
  - Comfortably fits in 8GB VRAM
  - ~5% quality loss (acceptable)
```

### Quantization Methods Compared

```
Method          Quality    Speed    Notes
───────────────────────────────────────────────────────
GPTQ            ★★★★☆     ★★★★☆   Post-training, calibration dataset needed
AWQ             ★★★★★     ★★★★★   Activation-aware, best quality/speed
GGUF Q4_K_M     ★★★★☆     ★★★★★   llama.cpp format, most portable
GGUF Q4_K_XL    ★★★★★     ★★★★☆   Better accuracy than Q4_K_M
QAT (Quantization Aware Training)
                ★★★★★     ★★★★★   Train with quantization simulated
                                   → best possible quality at INT4
```

### QAT vs PTQ (Your Post-Training Quantization)

```
Post-Training Quantization (PTQ):
  Full model (FP32) → Compress after training → INT4
  Problem: model wasn't trained to handle precision loss
  Result: 5-10% quality degradation

Quantization-Aware Training (QAT):
  Train in FP32 but SIMULATE INT4 during training
  Model learns to compensate for precision loss while training
  Then compress to INT4
  Result: <2% quality degradation
  Used by: Gemma 4 QAT, newer LLaMA quants
```

---

## 18. Concept 11 — What is LoRA / QLoRA Fine-Tuning?

Fine-tuning makes a model better at your specific task without retraining from scratch.

### The Problem with Full Fine-Tuning

```
Qwen2.5-7B has 7 billion parameters.
Full fine-tuning = updating all 7B parameters.
  - Needs ~80GB VRAM (multiple A100s)
  - Takes weeks
  - Costs thousands of dollars
  → Impossible on RTX 5060
```

### LoRA — The Solution

**LoRA = Low-Rank Adaptation.** Paper: **"LoRA: Low-Rank Adaptation of Large Language Models" (Hu et al., 2021)**

```
Key Insight:
  When you fine-tune, the CHANGE in weights (ΔW) is low-rank.
  You don't need to update all 7B weights.
  You only need to learn the low-rank update matrix.

Implementation:
  Original weight matrix W: shape [4096 × 4096] = 16.7M parameters
  LoRA adds two small matrices:
    A: [4096 × 16]   = 65,536 parameters
    B: [16 × 4096]   = 65,536 parameters
  
  Updated weight = W + B×A  (rank-16 update)
  
  Instead of updating 16.7M parameters,
  you only train 131,072 (131K) — a 127× reduction.

During training: W is frozen. Only A and B are updated.
During inference: merge W + B×A for zero latency overhead.
```

### QLoRA — LoRA on Quantized Models

**QLoRA paper: "QLoRA: Efficient Finetuning of Quantized LLMs" (Dettmers et al., 2023)**

```
QLoRA = Quantize the base model to 4-bit + apply LoRA on top

Memory savings:
  Full fine-tune 7B: ~80GB VRAM  (impossible on RTX 5060)
  LoRA fine-tune 7B: ~24GB VRAM  (still too much)
  QLoRA 7B:          ~6GB VRAM   ← fits on your GPU!
  QLoRA 14B:         ~10GB VRAM  ← fits if 16GB VRAM

Key components of QLoRA:
  1. NF4 quantization (4-bit NormalFloat)
  2. Double quantization (quantize the quantization constants!)
  3. Paged optimizers (handles memory spikes gracefully)
```

### ARK OS Fine-Tuning Plan

```python
# Using Unsloth (fastest QLoRA library, 2x faster than HuggingFace)

from unsloth import FastLanguageModel

# Load base model in 4-bit
model, tokenizer = FastLanguageModel.from_pretrained(
    model_name = "Qwen/Qwen2.5-7B-Instruct",
    max_seq_length = 8192,
    load_in_4bit = True,    # QLoRA
)

# Add LoRA adapters
model = FastLanguageModel.get_peft_model(
    model,
    r = 64,                 # LoRA rank
    lora_alpha = 128,       # scaling factor (usually 2×r)
    target_modules = ["q_proj", "k_proj", "v_proj",
                      "o_proj", "gate_proj", "up_proj", "down_proj"],
    lora_dropout = 0.05,
    bias = "none",
)

# Fine-tune on ARK OS interaction data
trainer = SFTTrainer(
    model = model,
    dataset = ark_os_dataset,   # your agent interaction data
    max_seq_length = 8192,
    dataset_text_field = "text",
)
trainer.train()

# Save adapter (only ~100MB, not 7GB)
model.save_pretrained("ark_research_agent_lora")
```

---

# PART 4 — THE MODELS

## 19. Model Architecture From Scratch

Let us trace a complete request through ARK OS:

```
User: "Read the latest paper on RSI trading strategies and implement one"

Step 1: Tokenization
  Text → tokens: [4910, 262, 3452, 3348, 319, 49, 14313, ...]

Step 2: Supervisor Agent (Qwen2.5-72B via Groq)
  Input tokens → 72 Transformer blocks → output logits
  Samples: RESEARCH_THEN_CODE routing decision
  Emits ARKMessage to Planner

Step 3: Planner Agent (Qwen2.5-72B via Groq)
  Input: goal + Supervisor decision
  Output: task DAG JSON
  {
    "task_1": {"agent": "research", "input": "RSI trading strategies"},
    "task_2": {"agent": "coding", "input": "implement RSI backtester",
               "depends_on": ["task_1"]}
  }

Step 4: Memory Agent injects context
  Retrieves: previous RSI-related tasks from Qdrant
  Injects: "Previously fixed yfinance MultiIndex bug: df.columns.droplevel(1)"
  This goes into memory_context of every outgoing message

Step 5: Research Agent (Gemma 4 26B-A4B, local)
  Calls arXiv API → gets 5 papers
  Embeds abstracts → stores in Qdrant
  Runs RAG to synthesize findings
  Returns: structured research summary

Step 6: Coding Agent (Qwen2.5-Coder-14B, local)
  Input: research summary + task spec + memory context
  Output: RSI backtester Python code
  Calls run_python() tool → executes → gets error
  Reads error → applies memory_context fix → reruns → success

Step 7: Evaluation Agent (Gemma 4 26B-A4B, local)
  Scores the implementation: 0.87
  Above threshold (0.75) → DONE

Step 8: Memory Agent stores outcome
  Episodic: "Task ark_001: RSI backtester implemented, score 0.87"
  Semantic: paper embeddings stored in Qdrant
  Procedural: "RSI task → use yfinance, fix MultiIndex, use plotly"

Step 9: Reflection Agent (Qwen2.5-72B via Groq)
  "The MultiIndex fix was in memory — saved 2 retry attempts"
  Updates procedural memory with reinforced yfinance pattern
```

---

## 20. Best Models in 2026 — Full Comparison

### Language Models

```
Model               Params  VRAM(Q4)  Score*   Best For
──────────────────────────────────────────────────────────────
Qwen2.5-72B         72B     40GB      ★★★★★   Orchestration (API)
Llama 3.3-70B       70B     38GB      ★★★★★   General (API)
Gemma 4 26B-A4B     26B/4B  5GB       ★★★★☆   Local general (MoE)
Qwen2.5-14B         14B     8GB       ★★★★☆   Local fallback
Qwen2.5-7B          7B      5GB       ★★★☆☆   Local fast tasks
Mistral-7B-v0.3     7B      5GB       ★★★☆☆   General local
Phi-4               14B     8GB       ★★★★☆   Reasoning focused
DeepSeek-R1-7B      7B      5GB       ★★★★☆   Math/reasoning

*Score = general capability, not domain-specific
```

### Code Models

```
Model                    Params  VRAM(Q4)  Score    Best For
────────────────────────────────────────────────────────────────────
Qwen2.5-Coder-32B        32B     18GB      ★★★★★   Best code (API)
Qwen2.5-Coder-14B        14B     8GB       ★★★★★   Best local code
Qwen2.5-Coder-7B         7B      5GB       ★★★★☆   Fast code (8GB VRAM)
DeepSeek-Coder-V2-16B    16B     9GB       ★★★★☆   Strong code
CodeLlama-34B            34B     20GB      ★★★☆☆   Older but solid
```

### Vision-Language Models

```
Model             Params  VRAM   Vision Encoder  Score    Notes
────────────────────────────────────────────────────────────────────
GPT-4V            ~1T     API    Unknown         ★★★★★   Best, costly
Claude 3.5 Sonnet ~70B    API    Unknown         ★★★★★   Best, costly
Qwen2-VL-72B      72B     40GB   ViT-G 600M      ★★★★★   Best open VLM
InternVL-2-26B    26B     14GB   InternViT-6B    ★★★★☆   Best local VLM
Qwen2-VL-7B       7B      5GB    ViT-G 600M      ★★★★☆   ARK OS choice
LLaVA-1.6-34B    34B     20GB   CLIP ViT-L      ★★★★☆   Solid open
PaliGemma-3B      3B      2GB    SigLIP-400M     ★★★☆☆   Tiny but capable
LLaVA-1.5-7B     7B      5GB    CLIP ViT-L      ★★★☆☆   Lightweight
```

---

## 21. Which Models ARK OS Uses and Why

```
Role              Model                   Why This Specific Model
────────────────────────────────────────────────────────────────────────
Orchestration     Qwen2.5-72B (Groq)     Strongest reasoning at 500 tok/s
                                          free tier. Groq's hardware makes
                                          72B feel like a local model.

General Local     Gemma 4 26B-A4B        MoE: 26B knowledge at 4B compute.
                  QAT Q4_K_XL (Ollama)  Fits in 5GB VRAM. Better than
                                          Qwen2.5-7B at same VRAM cost.

Code Local        Qwen2.5-Coder-14B      Best open-source code model in
                  Q4_K_M (Ollama)        2025-2026. Beats CodeLlama 34B.
                                          9GB VRAM. Justified for code quality.

Vision            Qwen2-VL-7B            ViT-G encoder (same as 72B).
                  Q4_K_M (Ollama)        5GB VRAM. Strong OCR + figure
                                          understanding for SAGE Mode.

Fallback          Qwen2.5-14B            If Groq is unavailable.
                  Q4_K_M (Ollama)        Same architecture as 72B, smaller.
```

---

## 22. How to Train / Fine-Tune on Your GPU

### What Your RTX 5060 Can Handle

```
Task                                 Your GPU         Time
──────────────────────────────────────────────────────────
Inference (7B Q4)                   RTX 5060 8GB     Instant
Inference (14B Q4)                  RTX 5060 8GB     ~2s/tok
QLoRA fine-tune 7B model            RTX 5060 8GB     ~2 days
QLoRA fine-tune 14B model           RTX 5060 8GB*    ~4 days
VLM projection training (Stage 1)   RTX 5060 8GB     ~3 days
Full pre-training (any model)       Not feasible     N/A

*14B QLoRA needs careful gradient checkpointing + 8-bit optimizer
```

### Step-by-Step: Fine-Tune Research Agent on arXiv Data

```bash
# Step 1: Install Unsloth (fastest fine-tuning library)
pip install unsloth

# Step 2: Prepare dataset (instruction → response format)
# Format: {"instruction": "Summarize this paper", "response": "..."}

# Step 3: Run fine-tuning
python finetune_research_agent.py \
  --model Qwen/Qwen2.5-7B-Instruct \
  --lora_r 64 \
  --lora_alpha 128 \
  --batch_size 2 \
  --gradient_accumulation 8 \  # effective batch = 16
  --learning_rate 2e-4 \
  --epochs 3 \
  --dataset ark_research_data.jsonl

# Step 4: Merge LoRA adapter with base model
python merge_lora.py --base Qwen2.5-7B --adapter research_lora/

# Step 5: Convert to GGUF for Ollama
./llama.cpp/convert_hf_to_gguf.py merged_model/ --outtype q4_k_m
ollama create ark-research-agent -f Modelfile
```

### GPU Memory Management for ARK OS

```python
# When running multiple agents:

# Strategy 1: Load on demand (Ollama handles this)
# Ollama auto-loads/unloads models between calls
# Config: OLLAMA_MAX_LOADED_MODELS=1 (for 8GB VRAM)

# Strategy 2: Shared model server (vLLM for Phase 3)
# All agents call one vLLM server
# vLLM batches requests from multiple agents
# More efficient than one Ollama instance per agent

# Strategy 3: Model tiering
# Fast tasks → Gemma 4 26B (always loaded, 5GB)
# Code tasks → Swap to Qwen-Coder-14B (9GB) when needed
# Vision tasks → Swap to Qwen2-VL-7B when needed
```

---

# PART 5 — THE RESEARCH

## 23. Original Papers (Foundations)

These are the papers ARK OS is built upon. Read them in this order:

### Tier 1 — Must Read (Foundation)

```
1. "Attention Is All You Need" (Vaswani et al., 2017)
   → The Transformer architecture. Everything starts here.
   → https://arxiv.org/abs/1706.03762

2. "Language Models are Few-Shot Learners" (Brown et al., 2020)
   → GPT-3: showed LLMs can follow instructions from examples
   → https://arxiv.org/abs/2005.14165

3. "Training language models to follow instructions" (Ouyang et al., 2022)
   → InstructGPT: RLHF alignment — how to make LLMs helpful
   → https://arxiv.org/abs/2203.02155

4. "LLaMA: Open and Efficient Foundation Language Models" (Touvron et al., 2023)
   → Open-source foundation that made everything else possible
   → https://arxiv.org/abs/2302.13971
```

### Tier 2 — Agent Papers (Core of ARK OS)

```
5. "ReAct: Synergizing Reasoning and Acting in Language Models" (Yao et al., 2022)
   → The ReAct loop: every ARK OS agent uses this pattern
   → https://arxiv.org/abs/2210.03629

6. "Toolformer: Language Models Can Teach Themselves to Use Tools" (Schick et al., 2023)
   → How LLMs learn to call external tools
   → https://arxiv.org/abs/2302.04761

7. "Chain-of-Thought Prompting Elicits Reasoning" (Wei et al., 2022)
   → CoT: how to make LLMs reason step by step
   → https://arxiv.org/abs/2201.11903

8. "Generative Agents: Interactive Simulacra of Human Behavior" (Park et al., 2023)
   → First paper to implement persistent memory + reflection for agents
   → Memory architecture ARK OS is based on
   → https://arxiv.org/abs/2304.03442

9. "AgentBench: Evaluating LLMs as Agents" (Liu et al., 2023)
   → The benchmark standard for agent evaluation
   → Directly relevant to ARKBench design
   → https://arxiv.org/abs/2308.03688
```

### Tier 3 — Efficiency Papers (Making It Run Locally)

```
10. "LoRA: Low-Rank Adaptation of Large Language Models" (Hu et al., 2021)
    → Fine-tuning on consumer hardware
    → https://arxiv.org/abs/2106.09685

11. "QLoRA: Efficient Finetuning of Quantized LLMs" (Dettmers et al., 2023)
    → 4-bit fine-tuning: makes fine-tuning possible on RTX 5060
    → https://arxiv.org/abs/2305.14314

12. "Mixtral of Experts" (Jiang et al., 2024)
    → The MoE architecture behind Gemma 4 and modern efficient models
    → https://arxiv.org/abs/2401.04088
```

### Tier 4 — RAG and Memory Papers

```
13. "Retrieval-Augmented Generation for Knowledge-Intensive NLP Tasks" (Lewis et al., 2020)
    → Original RAG paper: Research Agent is based on this
    → https://arxiv.org/abs/2005.11401

14. "Self-RAG: Learning to Retrieve, Generate, and Critique" (Asai et al., 2023)
    → RAG that knows when to retrieve and when not to
    → https://arxiv.org/abs/2310.11511

15. "HippoRAG: Neurobiologically Inspired Long-Term Memory for LLMs" (Gutierrez et al., 2024)
    → Most advanced RAG memory architecture: inspiration for ARK Memory
    → https://arxiv.org/abs/2405.14831
```

### Tier 5 — Vision Papers (For SAGE Mode)

```
16. "Learning Transferable Visual Models From Natural Language Supervision"
    CLIP (Radford et al., 2021)
    → Foundation of all VLMs. Visual encoders start here.
    → https://arxiv.org/abs/2103.00020

17. "Visual Instruction Tuning" LLaVA (Liu et al., 2023)
    → How to connect vision encoder to LLM cheaply
    → ARK Vision Agent is based on this paradigm
    → https://arxiv.org/abs/2304.08485

18. "Sigmoid Loss for Language Image Pre-Training" SigLIP (Zhai et al., 2023)
    → Best vision encoder for VLMs (what ARK-VLM will use)
    → https://arxiv.org/abs/2303.15343
```

### Tier 6 — Multi-Agent Papers (Direct Comparison)

```
19. "MetaGPT: Meta Programming for A Multi-Agent Collaborative Framework" (Hong et al., 2023)
    → Closest existing work to ARK OS (software-focused)
    → https://arxiv.org/abs/2308.00352

20. "AutoGen: Enabling Next-Gen LLM Applications via Multi-Agent Conversation" (Wu et al., 2023)
    → Microsoft's multi-agent framework
    → https://arxiv.org/abs/2308.08155

21. "CAMEL: Communicative Agents for 'Mind' Exploration of Large Language Models" (Li et al., 2023)
    → Role-playing multi-agent framework
    → https://arxiv.org/abs/2303.17760
```

---

## 24. Evaluation Methods

### How to Evaluate ARK OS

```
Level 1 — Component Evaluation (per agent)

  Research Agent:
    ROUGE-L    → overlap between generated summary and reference
    BERTScore  → semantic similarity (better than ROUGE)
    Citation accuracy → % of cited papers that actually say what agent claims
    Novelty score → cosine distance of hypothesis from existing literature

  Coding Agent:
    Pass@k     → % of problems where at least 1 of k solutions passes all tests
    HumanEval  → 164 Python programming problems (standard benchmark)
    SWE-bench  → real GitHub issues from open-source repos
    Bug fix rate → % of injected bugs caught and fixed by Debugging Agent

  Security Agent:
    Precision  → % of flagged vulnerabilities that are real
    Recall     → % of real vulnerabilities that were flagged
    F1 score   → harmonic mean of precision and recall
    OWASP coverage → which OWASP Top 10 categories are detected

Level 2 — System Evaluation (multi-agent)

  Task completion rate → % of submitted goals that reach DONE status
  End-to-end latency → wall clock time from goal to result
  Retry rate → avg retries per task (lower = smarter agents)
  Memory hit rate → % of tasks where relevant prior knowledge was found

Level 3 — Longitudinal Evaluation (self-improvement)

  ARK OS Month 1 score on ARKBench → baseline
  ARK OS Month 6 score on ARKBench → post-improvement
  Delta → evidence that self-improvement loop works
  This is Paper #3's core contribution.
```

### Standard Benchmarks ARK OS Targets

```
Benchmark      Domain    What It Tests              Our Agent
─────────────────────────────────────────────────────────────
HumanEval      Code      Python function generation  Coding Agent
MBPP           Code      Basic programming problems  Coding Agent
SWE-bench      Code      Real GitHub bug fixes       Coding + Debug
MMLU           General   Multi-domain knowledge      All agents
MATH           Math      Mathematical reasoning      ML Engineer
HotpotQA       RAG       Multi-hop reasoning + RAG   Research Agent
TriviaQA       RAG       Factual knowledge           Research Agent
AgentBench     Agents    Multi-step agent tasks      All agents
SciFact        Science   Scientific claim verification Research + SAGE
```

---

## 25. Datasets

### For Fine-Tuning ARK OS Agents

```
Agent           Dataset                        Source                   Size
──────────────────────────────────────────────────────────────────────────────
Research        S2ORC                          Semantic Scholar          81M papers
                arXiv dataset                  Kaggle / arXiv API        2.3M papers
                PubMed abstracts               HuggingFace               30M abstracts

Coding          The Stack v2                   HuggingFace / BigCode     3TB code
                CodeAlpaca-20K                 HuggingFace               20K samples
                Evol-Instruct-Code             HuggingFace               110K samples
                SWE-bench train split          GitHub                    2.3K issues

Security        CVE data (NVD)                 nvd.nist.gov              200K+ CVEs
                CWE examples                   cwe.mitre.org             900+ patterns
                OWASP WebGoat                  OWASP GitHub              Vulnerable app
                SecurityEval                   HuggingFace               130 tasks

Vision          LLaVA-Pretrain-558K            HuggingFace               558K img-txt
                LLaVA-Instruct-150K            HuggingFace               150K pairs
                ShareGPT4V                     HuggingFace               100K pairs
                ChartQA                        HuggingFace               32K charts
                DocVQA                         HuggingFace               50K doc pages

Scientific      SciQ                           HuggingFace               13.7K
                SciFact                        HuggingFace               1.4K claims
                AI2D                           HuggingFace               15K diagrams
                PaperswithCode datasets        paperswithcode.com        Varies
```

### For ARKBench (Evaluation)

```
ARKBench assembles tasks from:
  SWE-bench-lite (coding: 300 real bugs)
  HumanEval (coding: 164 functions)
  AgentBench (agent: 8 environments)
  SciFact (science: 1.4K claims)
  CVE test set (security: custom 100 CVEs)
  + ARK-original tasks (created during build)
```

---

# PART 6 — THE LANDSCAPE

## 26. Existing Similar Projects

### Direct Competitors (Single-Agent Systems)

```
Project         Domain      Open?   Memory   Protocol   Status
──────────────────────────────────────────────────────────────────
AutoGPT         General     ✅      ❌        ❌         Active
BabyAGI         General     ✅      ❌        ❌         Stagnant
Devin           Code        ❌      ❌        ❌         Commercial
SWE-agent       Code        ✅      ❌        ❌         Active (research)
OpenInterpreter Code+Tools  ✅      ❌        ❌         Active
OpenHands       Code        ✅      ❌        ❌         Active (fka OpenDevin)
```

### Multi-Agent Frameworks (Closer to ARK OS)

```
Project         Domain      Open?   Memory   Multi?     Notes
──────────────────────────────────────────────────────────────────
MetaGPT         Code        ✅      ❌        ✅         Role-playing agents
AutoGen         General     ✅      ⚠️        ✅         Microsoft, good framework
CrewAI          General     ✅      ⚠️        ✅         Role-based crews
CAMEL           Research    ✅      ❌        ✅         Academic, role-playing
SuperAGI        General     ✅      ⚠️        ✅         UI-heavy, startup
LangChain/LangGraph General ✅     ⚠️        ✅         Framework, not system
```

### Research Systems (Academic)

```
Project              Paper                           Contribution
────────────────────────────────────────────────────────────────────────
Generative Agents    Stanford (Park et al., 2023)   Memory + Reflection
Voyager              NVIDIA (Wang et al., 2023)     Continual learning agents
OpenAgents           Xu et al., 2023                Tool-augmented agents
AgentVerse           Chen et al., 2023              Multi-agent simulation
AgentBench           Liu et al., 2023               Evaluation standard
```

---

## 27. How ARK OS Differs (The Actual Gap)

```
What exists:
  Multi-agent frameworks (AutoGen, CrewAI) — good tooling, no memory, no protocol standard
  Single coding agents (Devin, SWE-agent) — deep but narrow
  Agent simulations (Generative Agents, Voyager) — academic, not production-ready
  Scientific tools (AutoRA) — single domain, no engineering integration

What ARK OS adds that none of these have simultaneously:

1. OS-Level Kernel Architecture
   Not just agents — a kernel that schedules, routes, and arbitrates
   Treating agents as OS processes (with registry, scheduler, priority)

2. Frozen Protocol (AgentSpec v1.0)
   A versioned, frozen message standard
   Anyone can add a new agent without touching existing ones
   None of the existing frameworks have this

3. Four-Tier Unified Memory
   All agents share one memory interface
   Working + Episodic + Semantic + Procedural in one system
   AutoGen has some memory but not this architecture

4. Self-Improvement Loop
   Reflection → Self-Improvement → measurable quality delta
   No existing framework proves that agents get better over time

5. SAGE Mode
   Scientific discovery built into an engineering OS
   Literature → hypothesis → experiment → paper, all automated
   No existing system spans both engineering and science

6. Fully Local Operation
   Runs on consumer hardware, no cloud dependency
   Privacy-preserving by design
   Quantized MoE makes this feasible in 2026
```

---

# PART 7 — THE STACK

## 28. Complete Tech Stack with Reasoning

```
┌──────────────────────────────────────────────────────────────────────┐
│ LAYER              │ TOOL                  │ WHY THIS TOOL           │
├──────────────────────────────────────────────────────────────────────┤
│ ORCHESTRATION      │ LangGraph             │ State machines for      │
│                    │                       │ agent workflows. Better  │
│                    │                       │ than CrewAI for complex  │
│                    │                       │ DAG execution control.   │
├──────────────────────────────────────────────────────────────────────┤
│ MODEL SERVING      │ Ollama                │ One-command model mgmt.  │
│ (Local)            │                       │ OpenAI-compatible API.   │
│                    │                       │ Hot-swap between models. │
├──────────────────────────────────────────────────────────────────────┤
│ MODEL SERVING      │ Groq API              │ 500 tok/s for free.      │
│ (Cloud, 72B only)  │                       │ Fastest LLM API on earth.│
│                    │                       │ Only for orchestration.  │
├──────────────────────────────────────────────────────────────────────┤
│ VECTOR MEMORY      │ Qdrant                │ Best performance/features│
│                    │                       │ among open vector DBs.   │
│                    │                       │ Docker, local, free.     │
├──────────────────────────────────────────────────────────────────────┤
│ WORKING MEMORY     │ Redis                 │ Sub-millisecond lookups. │
│ + MESSAGE BUS      │                       │ Pub/Sub for agent comms. │
│                    │                       │ TTL for auto-expiry.     │
├──────────────────────────────────────────────────────────────────────┤
│ EPISODIC MEMORY    │ SQLite                │ Persistent, zero config. │
│                    │                       │ SQL for time-range       │
│                    │                       │ queries. File-based.     │
├──────────────────────────────────────────────────────────────────────┤
│ EMBEDDINGS         │ BAAI/bge-large-en-v1.5│ Best quality/speed for   │
│                    │                       │ English scientific text. │
├──────────────────────────────────────────────────────────────────────┤
│ BROWSER AUTO.      │ Playwright (Python)   │ Async, fast, reliable.   │
│                    │                       │ Better than Selenium.    │
│                    │                       │ Screenshot support.      │
├──────────────────────────────────────────────────────────────────────┤
│ CODE EXECUTION     │ Docker (sandboxed)    │ Isolated execution.      │
│                    │                       │ Prevents malicious code  │
│                    │                       │ from touching host FS.   │
├──────────────────────────────────────────────────────────────────────┤
│ SECURITY SCANNING  │ Semgrep + Bandit      │ Semgrep: multi-language  │
│                    │                       │ pattern matching.        │
│                    │                       │ Bandit: Python-specific. │
├──────────────────────────────────────────────────────────────────────┤
│ ML TRACKING        │ MLflow                │ Experiment tracking for  │
│                    │                       │ SAGE Mode runs. Local.   │
├──────────────────────────────────────────────────────────────────────┤
│ HPO                │ Optuna                │ Best Bayesian HPO lib.   │
│                    │                       │ Used by ML Engineer Agent│
├──────────────────────────────────────────────────────────────────────┤
│ FINE-TUNING        │ Unsloth + HuggingFace │ Unsloth: 2x faster LoRA. │
│                    │                       │ HuggingFace: ecosystem.  │
├──────────────────────────────────────────────────────────────────────┤
│ API LAYER          │ FastAPI               │ Async, fast, auto-docs.  │
│                    │                       │ OpenAPI spec generation. │
├──────────────────────────────────────────────────────────────────────┤
│ DASHBOARD          │ React + WebSocket     │ Real-time agent monitor. │
├──────────────────────────────────────────────────────────────────────┤
│ CONTAINERIZATION   │ Docker Compose        │ One-command full stack.  │
│                    │                       │ Reproducible environments│
│                    │                       │ Required for SAGE repro. │
├──────────────────────────────────────────────────────────────────────┤
│ VCS INTEGRATION    │ GitPython             │ GitHub Agent operations. │
│                    │                       │ Pure Python, no CLI dep. │
├──────────────────────────────────────────────────────────────────────┤
│ DOCUMENT PROC.     │ pypdf + python-docx   │ PDF + Word without       │
│                    │ + openpyxl            │ LibreOffice dependency.  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Summary — ARK OS in One Table

```
Question                    Answer
──────────────────────────────────────────────────────────────────────────
What is it?                 An OS for AI agents — 20 specialists sharing
                            memory, protocol, and tools

Why does it need to exist?  No unified system handles research + code +
                            security + science with persistent memory

What does it produce?       Autonomous knowledge work + 4-5 papers

Core architecture?          Transformer LLMs + RAG + ReAct + Multi-agent
                            orchestration + 4-tier memory

How does it run locally?    MoE quantization (Gemma 4 26B-A4B) + QLoRA
                            fits everything in 8-16GB VRAM

Key papers to read?         Attention (2017), ReAct (2022), Generative
                            Agents (2023), LLaVA (2023), QLoRA (2023)

Closest existing projects?  MetaGPT (code only), AutoGen (no memory),
                            Devin (closed, code only)

How is it different?        Kernel architecture + frozen protocol +
                            4-tier memory + self-improvement + SAGE Mode

How long to build?          6 months, 20 agents, 3 phases

Publication targets?        NeurIPS, ICLR, AAMAS, Nature Machine Intel.
```

---

*ARK OS — Master Document v1.0*
*Kabilash (E0323036) · SRIHER Chennai · June 2026*
*This document is the single source of truth for the project.*
