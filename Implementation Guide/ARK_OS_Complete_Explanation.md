# ARK OS — Complete Explanation From Scratch
### Everything: Problem → Purpose → Concepts → Architecture → Models → Papers → Datasets → Existing Projects

---

# PART 0 — START HERE: What Problem Are We Solving?

## The Real-World Problem

A researcher today does this:
```
Day 1:  Read 20 papers manually on Google Scholar
Day 2:  Write literature review in Word
Day 3:  Write Python code to test an idea
Day 4:  Debug errors manually
Day 5:  Run experiments, wait hours
Day 6:  Plot graphs manually in matplotlib
Day 7:  Write the results section of the paper
```

Every step is:
- Done by a human
- Using a different disconnected tool
- Starting from zero context every session
- Producing no shared memory between tools

**This is the problem ARK OS solves.**

## The Core Insight

Knowledge work — reading, coding, discovering, securing — follows patterns.
Those patterns can be delegated to specialized agents that:
- Share a common memory
- Hand tasks to each other automatically
- Work while you sleep
- Get smarter with every task they complete

That is ARK OS.

---

# PART 1 — WHAT IS ARK OS?

## One-Line Definition
> ARK OS is a multi-agent AI operating system where 20 specialized agents share a unified memory, communicate via a strict protocol, and autonomously complete long-horizon knowledge work tasks across research, coding, cybersecurity, and scientific discovery.

## Why "Operating System"?

A traditional OS has:
```
Kernel       → manages processes, memory, scheduling
Processes    → programs that do actual work
Memory       → shared state between processes
I/O Layer    → hardware, files, network
IPC          → inter-process communication
```

ARK OS has the exact same structure:
```
Kernel       → Supervisor + Planner + Memory + Evaluation agents
Agents       → 20 specialized workers that do actual tasks
Memory       → 4-tier shared memory (Redis, SQLite, Qdrant, JSON)
Tool Layer   → browser, terminal, files, APIs, code sandbox
Protocol     → ARKMessage (inter-agent communication standard)
```

## What Makes It Different From ChatGPT or AutoGPT?

| Feature | ChatGPT | AutoGPT | ARK OS |
|---|---|---|---|
| Memory across sessions | ❌ None | ❌ None | ✅ 4-tier persistent |
| Multiple specialized agents | ❌ One LLM | ⚠️ One agent loop | ✅ 20 specialized agents |
| Inter-agent communication | ❌ | ❌ | ✅ ARKMessage protocol |
| Self-improvement over time | ❌ | ❌ | ✅ Reflection + Self-Improvement agents |
| Scientific discovery loop | ❌ | ❌ | ✅ SAGE Mode |
| Fully local (no cloud required) | ❌ | ❌ | ✅ Phases 1-2 fully local |
| Pluggable new agents | ❌ | ❌ | ✅ BaseAgent interface |

---

# PART 2 — CORE CONCEPTS (Master These First)

## Concept 1 — What is a Large Language Model (LLM)?

### From Scratch — The Transformer Architecture

Every LLM is built on the **Transformer** architecture (2017, Google Brain — "Attention Is All You Need").

Before Transformers, we had RNNs and LSTMs — they read text word-by-word sequentially. Slow. Forgot early context. Could not parallelize.

**The Transformer solves this with Self-Attention:**

```
Input: "The cat sat on the mat because it was tired"

Self-Attention asks for every word:
"Which other words in this sentence should I pay attention to?"

For "it" → strong attention to "cat" → learns "it" refers to "cat"
For "sat" → strong attention to "cat" + "mat" → context of the action

This attention is computed for ALL words SIMULTANEOUSLY (parallel!)
```

### The Transformer Block (What's Inside)

```
Input Tokens (words converted to numbers)
        │
        ▼
[Token Embedding Layer]    ← each word → 4096-dim vector
        │
        ▼
[Positional Encoding]      ← tells model where each word is
        │
        ▼
┌─────────────────────────┐
│   TRANSFORMER BLOCK     │  ← repeated N times (e.g., 32 blocks for 7B)
│  ┌───────────────────┐  │
│  │  Multi-Head       │  │
│  │  Self-Attention   │  │  ← "which tokens matter to which?"
│  └────────┬──────────┘  │
│           │ + residual  │
│  ┌────────▼──────────┐  │
│  │  Layer Norm       │  │  ← stabilizes training
│  └────────┬──────────┘  │
│           │             │
│  ┌────────▼──────────┐  │
│  │  Feed Forward     │  │  ← two linear layers + activation (stores knowledge)
│  │  Network (FFN)    │  │
│  └────────┬──────────┘  │
│           │ + residual  │
│  ┌────────▼──────────┐  │
│  │  Layer Norm       │  │
│  └───────────────────┘  │
└──────────┬──────────────┘
           │ (repeated 32x)
           ▼
[Output Projection]        ← maps to vocabulary size
           │
           ▼
[Softmax]                  ← probability over next token
           │
           ▼
Next Token Prediction
```

### Key Numbers to Understand Scale

```
Model          Layers    Heads    Dim      Parameters    VRAM (Q4)
─────────────────────────────────────────────────────────────────
Qwen2.5-7B     28        28       4096     7B            ~5 GB
Qwen2.5-14B    40        40       5120     14B           ~9 GB
Qwen2.5-72B    80        64       8192     72B           ~42 GB  ← why we use Groq API
Gemma4-26B     46        32       4096     26B active=4B ~4 GB   ← MoE magic
```

---

## Concept 2 — How LLMs Generate Text

LLMs don't "understand" — they predict the next token given all previous tokens.

```
Training: given billions of text examples, learn P(next_token | all_previous_tokens)

Inference (autoregressive generation):
  Input:  "The capital of France is"
  Step 1: Model predicts → "Paris" (highest probability)
  Step 2: "The capital of France is Paris" → predicts → "."
  Step 3: "The capital of France is Paris." → predicts → [EOS]
  Done.
```

This simple next-token prediction, scaled to 70 billion parameters and trained on the entire internet, produces what we call "intelligence."

---

## Concept 3 — Mixture of Experts (MoE)

This explains why Gemma 4 26B ran on 8GB VRAM.

### Dense Model (Normal LLM)

```
Every token → activates ALL parameters → expensive

Qwen2.5-7B: 7 billion parameters fire for EVERY token
VRAM needed: ~5GB for weights + attention cache
```

### MoE Model (Gemma 4 26B-A4B)

```
The FFN layer (which stores knowledge) is split into N "experts"
Each expert is a small FFN

For every token, a Router network decides:
"Which 2 experts out of 64 should handle this token?"

Token → Router → picks Expert 3 + Expert 17 → only those 2 activate
      All other 62 experts stay idle

Result:
- 26B total parameters (lots of knowledge stored)
- Only 4B parameters active per token (cheap computation)
- VRAM needed: ~4-5GB (same as a 4B model!)
```

**Architecture diagram:**

```
Standard Transformer FFN:
Token → [Single Large FFN] → output

MoE Transformer FFN:
                    ┌─[Expert 1  ]─┐
                    ├─[Expert 2  ]─┤
Token → [Router] ──├─[Expert 3 ✓]─┼─▶ weighted sum ─▶ output
                    ├─[Expert 4  ]─┤
                    ├─...          │
                    └─[Expert N ✓]─┘
        (Router picks top-2, rest are skipped)
```

**Why this matters for ARK OS:**
Gemma 4 26B-A4B replaces Qwen2.5-7B in our stack — same VRAM cost, but 26B worth of knowledge available. Our general-purpose agents get significantly smarter at the same hardware cost.

---

## Concept 4 — Quantization

### The Problem
A 7B model in FP32 (standard precision): 7B × 4 bytes = **28 GB** — doesn't fit in any consumer GPU.

### The Solution: Reduce Precision

```
FP32  (32-bit float): 4 bytes/param  → 7B model = 28 GB  ← original
FP16  (16-bit float): 2 bytes/param  → 7B model = 14 GB
INT8  (8-bit int):    1 byte/param   → 7B model =  7 GB
INT4  (4-bit int):    0.5 bytes/param → 7B model = ~4.5 GB ← we use this
```

### Quality Loss?

Naïve INT4 loses a lot of quality. Modern quantization methods are smart:

```
GGUF Q4_K_M (llama.cpp format):
- Uses 4-bit for most weights
- Uses higher precision (6-bit, 8-bit) for critical layers
- "K" = K-quant method (smarter allocation)
- "M" = Medium calibration set
- Quality loss: ~1-2% vs FP16 ← barely noticeable

QAT (Quantization Aware Training):
- During training, simulates quantization noise
- Model learns to be robust to precision loss
- Results in INT4 model that performs like FP16
- Used in Gemma 4 26B-A4B QAT quant
```

---

## Concept 5 — RAG (Retrieval Augmented Generation)

LLMs have a knowledge cutoff — they don't know about papers published after training. RAG solves this.

```
WITHOUT RAG:
User: "What did the 2025 NeurIPS paper on ARK OS find?"
LLM:  "I don't have information about that paper." ← useless

WITH RAG:
Step 1: Convert query to vector embedding
Step 2: Search Qdrant for similar documents
Step 3: Retrieve top-3 matching paper chunks
Step 4: Inject them into LLM context
Step 5: LLM answers using retrieved content
LLM:  "According to the retrieved paper, ARK OS found..." ← useful
```

### How Vector Search Works

```
Text → Embedding Model → 1536-dim vector (a point in high-dimensional space)

"Transformer attention mechanism"  →  [0.23, -0.41, 0.18, ...]
"How BERT computes self-attention"  →  [0.21, -0.39, 0.20, ...]

These two sentences are about the same thing → their vectors are CLOSE together
Cosine similarity = 0.94  ← almost identical direction

"Stock market trading strategies"   →  [-0.51, 0.33, -0.12, ...]
This is different → vector points different direction
Cosine similarity = 0.12  ← very different
```

Qdrant stores millions of these vectors and finds the nearest neighbors in milliseconds. This is how the Research Agent "reads" thousands of papers instantly.

---

## Concept 6 — Tool Use / Function Calling

Modern LLMs can call external tools. This is what transforms an LLM from a "chat assistant" into an "agent."

```
Normal LLM:
User:  "What is the current GOOGL stock price?"
LLM:   "I don't know the current price." ← no external access

LLM with Tool Use:
User:  "What is the current GOOGL stock price?"
LLM:   [TOOL_CALL: get_stock_price("GOOGL")]
Tool:  Returns {"price": 182.34, "timestamp": "2026-06-15T10:30:00Z"}
LLM:   "The current GOOGL price is $182.34 as of 10:30 AM today."
```

The LLM doesn't run the tool — it outputs a structured call, the framework executes it, returns the result, and the LLM continues. This is how ALL ARK OS agents work — they are LLMs + tool libraries.

---

## Concept 7 — Multi-Agent Systems

### Why One Agent Is Not Enough

```
Single Agent Problem:
"Research quantum computing, implement a simulation, test it,
 scan it for security issues, and write a paper about it"

One LLM trying to do all of this:
- Context window fills up (can't hold everything)
- No specialization (generalist at everything = expert at nothing)
- Sequential only (can't research + code at the same time)
- No memory between tasks
```

### How Multi-Agent Solves This

```
Supervisor routes to parallel agents:

Research Agent   ──┐
(reads papers)     │
                   ├──▶ Memory Agent ──▶ Coding Agent ──▶ Testing Agent
Web Search Agent ──┘    (combines         (implements)     (validates)
(finds recent news)      knowledge)
                                                              │
                                                              ▼
                                                     Security Agent
                                                     (scans for vulns)
                                                              │
                                                              ▼
                                                     Document Agent
                                                     (writes paper)
```

Each agent specializes. Tasks run in parallel. Memory is shared. The output is better than any single agent could produce.

---

## Concept 8 — The ReAct Framework (How Each Agent Thinks)

**ReAct = Reasoning + Acting** — the standard paradigm for autonomous agents.

```
User: "Find recent papers on transformer attention efficiency"

Agent Loop:
──────────────────────────────────────────────────────
THOUGHT:  "I need to search arXiv for recent papers 
           on attention efficiency"

ACTION:   search_arxiv(query="transformer attention 
                        efficiency", 
                        date_range="2024-2026")

OBSERVATION: [Returns 15 papers with titles/abstracts]

THOUGHT:  "I found papers. Now I need to read the 
           most cited ones in detail"

ACTION:   fetch_paper(arxiv_id="2401.12345")

OBSERVATION: [Returns full paper text]

THOUGHT:  "I have enough context. I'll synthesize 
           the key findings now."

ACTION:   generate_summary(papers=[...])

FINAL ANSWER: "Recent advances in attention efficiency 
               include FlashAttention-3, MLA (Multi-head 
               Latent Attention in DeepSeek-V2), and..."
──────────────────────────────────────────────────────
```

Every ARK OS agent runs this loop until the task is complete or retries are exhausted.

---

## Concept 9 — LangGraph (How ARK OS Orchestrates Agents)

LangGraph builds **stateful agent workflows** as directed graphs. Each node is an agent or tool call. Edges are conditions.

```python
# ARK OS kernel as a LangGraph state machine (simplified)

from langgraph.graph import StateGraph

workflow = StateGraph(ARKState)

workflow.add_node("supervisor", supervisor_agent)
workflow.add_node("planner", planner_agent)
workflow.add_node("research", research_agent)
workflow.add_node("coding", coding_agent)
workflow.add_node("evaluation", evaluation_agent)
workflow.add_node("memory_update", memory_agent)

# Supervisor routes to the right next agent
workflow.add_conditional_edges(
    "supervisor",
    route_task,                          # function that decides next agent
    {
        "needs_research": "research",
        "needs_code": "coding",
        "needs_planning": "planner"
    }
)

# After evaluation: retry if score low, finish if score high
workflow.add_conditional_edges(
    "evaluation",
    check_quality,
    {
        "retry": "coding",               # score < threshold
        "accept": "memory_update"        # score >= threshold
    }
)

app = workflow.compile()
```

---

# PART 3 — VISION AGENTS vs TEXT AGENTS

## Text-Only Agent

```
Input:  tokens (text converted to numbers)
Model:  Transformer with token embeddings
Output: tokens (text)

Example: "Explain quantum entanglement" → text explanation
```

## Vision-Language Agent

```
Input:  image + text (multimodal)
Model:  Vision Encoder + Projection + Language Model
Output: tokens (text)

Example: [screenshot of error] + "what's wrong?" → diagnosis
```

## How Vision Encoding Works (ViT — Vision Transformer)

The key innovation: treat an image like a sequence of text tokens.

```
Step 1 — Patch Splitting:
Image (384×384 pixels) → split into 14×14 pixel patches
→ produces 27×27 = 729 patches

Step 2 — Linear Projection:
Each patch (14×14×3 = 588 pixel values) → linear layer → 1152-dim vector
(just like how words map to embedding vectors)

Step 3 — Position Encoding:
Add position info so model knows patch 1 is top-left, patch 729 is bottom-right

Step 4 — Transformer:
Run 729 patch tokens through transformer blocks
→ patches attend to each other → model "sees" the whole image
→ output: 729 × 1152 visual feature vectors

Step 5 — Projection to LLM Space:
MLP: 1152 → 4096 (maps from SigLIP space to Qwen2.5 space)
→ now image is represented as "visual tokens" the LLM can read
```

## Side-by-Side Comparison

```
TEXT AGENT:                        VISION AGENT:
─────────────────────────────────────────────────────────────────────
Input:   text tokens only          Input: image patches + text tokens
Encoder: token embedding matrix    Encoder: ViT + MLP projection
Context: text sequence             Context: visual tokens + text tokens
Memory:  text-based                Memory: text + image embeddings
Eval:    ROUGE, BERTScore          Eval:   VQAv2, MMBench, ScienceQA
Use in   Research, Coding,         Use in  Vision Agent, Screenshot,
ARK OS:  Security agents           ARK OS: OCR, Figure analysis (SAGE)
```

## Which Vision Encoders Are Best (2025-2026)

```
Encoder          Params    Resolution    Used In              Strength
──────────────────────────────────────────────────────────────────────
SigLIP-400M      400M      384×384      PaliGemma, LLaVA-1.6  Text-image alignment
InternViT-6B     6B        448×448      InternVL-2            High-res, best accuracy
CLIP ViT-L/14    307M      224×224      LLaVA, BLIP           Battle-tested, fast
EVA-CLIP-18B     18B       448×448      InternVL family       Most powerful
DINOv2-giant     307M      518×518      Many hybrid models    Spatial understanding
```

**Our pick: SigLIP-400M** — best text-image alignment, 400M params fits in VRAM, used in the most modern VLMs, excellent PyTorch support.

---

# PART 4 — COMPLETE TECH STACK EXPLAINED

## Why Each Technology Was Chosen

### Inference Layer
```
Ollama
  → Why: One command to pull + serve any GGUF model
  → Why: Auto manages VRAM (load on demand, unload when idle)
  → Why: Serves OpenAI-compatible API → any LangChain tool works
  → Alternative: llama.cpp direct (more control, less convenience)

Groq API
  → Why: 500 tokens/second (10x faster than any other API)
  → Why: Runs on custom GroqChip (LPU — Language Processing Unit)
  → Why: Free tier is generous enough for orchestration
  → Used for: Supervisor + Planner only (need best reasoning)
```

### Memory Layer
```
Redis
  → Why: In-memory key-value, microsecond latency
  → Role: Working memory (current task context) + message bus
  → Also: Task queue (FIFO) for agent scheduling

Qdrant
  → Why: Purpose-built vector database, fastest similarity search
  → Role: Semantic memory (paper embeddings, code patterns)
  → Also: Procedural memory (skill library)
  → Alternative: ChromaDB (easier), Pinecone (cloud only)

SQLite
  → Why: Zero-config, file-based, SQL queries
  → Role: Episodic memory (task history, agent logs, outcomes)
  → Why not PostgreSQL: overkill for single-machine setup
```

### Orchestration Layer
```
LangGraph
  → Why: Stateful agent workflows (unlike LangChain which is stateless)
  → Why: Native support for loops, retries, conditional routing
  → Why: Compiles to a runnable graph — easy to visualize + debug

LangChain
  → Why: Tool abstractions (browser, file, API tools) ready-made
  → Why: Document loaders for PDF, DOCX, HTML
  → Why: Not for orchestration (use LangGraph for that)
```

### Tool Layer
```
Playwright (Browser Agent)
  → Why: Full browser automation with JavaScript execution
  → Why: Works on headless servers (no display needed)
  → Why not Selenium: slower, older, less reliable with modern JS

Docker (Code Sandbox)
  → Why: Isolated environment for Coding Agent to run untrusted code
  → Why: Prevents agent from breaking host system
  → Critical: Never run agent-generated code outside sandbox

MLflow (ML Engineer Agent)
  → Why: Tracks experiments (params, metrics, artifacts) automatically
  → Why: Web UI to compare runs visually
  → Why: Integrates with Optuna for HPO

Optuna (Hyperparameter Optimization)
  → Why: Bayesian optimization (smarter than grid search)
  → Why: Pruning (stops bad trials early)
  → Why: Simple API, integrates with PyTorch natively
```

---

# PART 5 — MODEL STRATEGY (COMPLETE)

## The Full Local Stack for ARK OS

### Gemma 4 27B-A4B (General Reasoning)

```
Architecture: Transformer with MoE FFN layers
Release:      Google DeepMind, 2025
Total params: 27 billion
Active params: ~4 billion per token (MoE)
Context:      128K tokens
VRAM (Q4_K_XL QAT): ~4-5 GB

Key innovations:
- QAT quantization: trained to be robust to INT4 precision
- MoE routing: 64 experts, top-2 active per token
- Interleaved attention: global + local attention alternating
- Natively multimodal: can handle images too

Why for ARK OS general agents:
- Memory Agent, Evaluation, Web Search = frequent calls
- Need fast, cheap, smart model
- 26B knowledge at 4B cost = perfect fit

Base paper: "Gemma: Open Models Based on Gemini Research and Technology"
            arxiv.org/abs/2403.08295 (Gemma 1, architecture reference)
```

### Qwen2.5-Coder-14B (Code Tasks)

```
Architecture: Transformer, dense, decoder-only (no MoE)
Release:      Alibaba DAMO Academy, 2024
Params:       14B
Context:      128K tokens
VRAM (Q4_K_M): ~8.5-9 GB

Key training:
- Trained on 5.5 trillion tokens of code (vs 3T for base)
- Fill-in-the-Middle (FIM) training: can complete code in middle of file
- Trained on 92 programming languages
- Strong on: Python, C++, Java, SQL, Bash, JavaScript

Why for ARK OS code agents:
- Coding, Debugging, GitHub, ML Engineer, DevOps agents
- Best open-source code model (beats CodeLlama-70B at 14B)
- FIM capability = can insert code into existing files

Base paper: "Qwen2.5-Coder Technical Report"
            arxiv.org/abs/2409.12186
```

### Qwen2-VL-7B (Vision Tasks)

```
Architecture: Transformer + ViT vision encoder (Naive Dynamic Resolution)
Release:      Alibaba DAMO Academy, 2024
LLM params:   7B
Vision:       ViT-based, handles any image resolution natively
Context:      32K tokens + unlimited image resolution
VRAM (Q4_K_M): ~5 GB

Key innovations:
- Naive Dynamic Resolution: processes images at their NATIVE resolution
  (most VLMs downsample to fixed 224×224 — Qwen2-VL doesn't)
- Temporal ViT: also understands videos (sequence of frames)
- M-RoPE: multimodal position encoding for text + image positions

Why for ARK OS vision agent:
- Screenshot analysis, scientific figure interpretation
- OCR on paper images, chart reading for Data Analysis Agent
- SAGE Mode: reading experimental output figures

Base paper: "Qwen2-VL: Enhancing Vision-Language Model's Perception 
            of the World at Any Resolution"
            arxiv.org/abs/2409.12191
```

### Qwen2.5-72B (Orchestration, via Groq)

```
Architecture: Transformer, Grouped Query Attention (GQA), dense
Release:      Alibaba DAMO Academy, 2024
Params:       72B
Context:      128K tokens
VRAM:         ~42 GB (why we use Groq API — too big for local)

Why for orchestration:
- Supervisor needs best reasoning (routes wrong = entire task fails)
- Planner needs best planning (bad DAG = all agents do wrong work)
- Reflection needs best analysis (learns wrong = system degrades)
- 72B scale = strongest open-source reasoning available

Groq advantage:
- GroqChip LPU (Language Processing Unit) vs GPU
- GPU: matrix multiplications are sequential (memory bandwidth bound)
- LPU: purpose-built for transformer inference (compute bound)
- Result: 500 tok/s vs ~50 tok/s on best cloud GPUs

Base paper: "Qwen2.5 Technical Report"
            arxiv.org/abs/2412.15115
```

---

# PART 6 — FINE-TUNING AND TRAINING

## Why Fine-Tune at All?

Pre-trained models are general. ARK OS needs agents that:
- Consistently output valid ARKMessage JSON (not sometimes)
- Follow the ReAct format exactly (not approximately)
- Know ARK OS domain-specific knowledge (your architecture)
- Produce code that passes ARKBench criteria

Fine-tuning teaches these behaviors efficiently.

## LoRA — The Key Concept

### The Problem with Full Fine-Tuning

```
Full fine-tune of Qwen2.5-7B:
- Update all 7B parameters
- Needs: 4 × 7B × 4 bytes = 112 GB VRAM minimum
- Your GPU: 8-16 GB
- Verdict: impossible on consumer hardware
```

### LoRA Solution

**Low-Rank Adaptation** — instead of changing the whole model, add tiny trainable matrices:

```
Original weight matrix W (4096 × 4096 = 16M parameters):
  W is FROZEN during fine-tuning

LoRA adds two small matrices:
  A: (4096 × r)   where r = rank (e.g., 64)
  B: (r × 4096)   
  
  A has 4096×64 = 262K parameters
  B has 64×4096 = 262K parameters
  Total LoRA params: 524K  ←  vs 16M original
  
At inference: W_effective = W + (A × B) × scale

You're only training A and B (3% of original size)
```

### QLoRA — LoRA + Quantization

```
QLoRA = train LoRA adapters ON TOP of a quantized (4-bit) model

Step 1: Load Qwen2.5-7B in NF4 (4-bit normal float)
Step 2: Freeze all quantized weights
Step 3: Add LoRA adapters (A, B matrices) in BF16
Step 4: Train only the LoRA adapters
Step 5: Save only the adapters (few hundred MB vs 4.5 GB)

VRAM needed: ~6 GB for 7B model
             ← fits on your RTX 5060!
```

### Practical Training Pipeline for ARK OS

```python
# QLoRA fine-tune for Research Agent behavior

from transformers import AutoModelForCausalLM, BitsAndBytesConfig
from peft import LoraConfig, get_peft_model
from trl import SFTTrainer

# 1. Load model in 4-bit
bnb_config = BitsAndBytesConfig(
    load_in_4bit=True,
    bnb_4bit_quant_type="nf4",         # NormalFloat4 quantization
    bnb_4bit_compute_dtype=torch.bfloat16,
    bnb_4bit_use_double_quant=True      # nested quantization
)
model = AutoModelForCausalLM.from_pretrained(
    "Qwen/Qwen2.5-7B-Instruct",
    quantization_config=bnb_config
)

# 2. Configure LoRA
lora_config = LoraConfig(
    r=64,              # rank (higher = more capacity, more VRAM)
    lora_alpha=128,    # scaling factor (usually 2×r)
    target_modules=["q_proj", "v_proj", "k_proj", "o_proj"],  # which layers
    lora_dropout=0.05,
    bias="none",
    task_type="CAUSAL_LM"
)
model = get_peft_model(model, lora_config)

# 3. Dataset format (for Research Agent behavior)
dataset = [
    {
        "instruction": "You are the ARK OS Research Agent. Given a topic, "
                       "search arXiv and produce a structured literature review.",
        "input": "topic: transformer attention efficiency mechanisms",
        "output": '{"thought": "I need to search for recent papers on attention '
                  'efficiency", "action": "search_arxiv", "action_input": '
                  '{"query": "attention efficiency transformer", "max_results": 10}}'
    },
    # ... thousands more examples
]

# 4. Train
trainer = SFTTrainer(
    model=model,
    train_dataset=dataset,
    max_seq_length=4096,
    num_train_epochs=3,
    per_device_train_batch_size=4,
    gradient_accumulation_steps=4,     # effective batch = 16
    learning_rate=2e-4,
    fp16=True
)
trainer.train()
```

## What Data to Train On

```
Agent             Training Data Source
──────────────────────────────────────────────────────────────
Research Agent    arXiv papers + literature review pairs (500K)
Coding Agent      GitHub issues + fixes (SWE-bench, 10K)
Security Agent    CVE descriptions + fix recommendations (50K)
Vision Agent      Scientific figures + captions (LLaVA-Sci, 100K)
General behavior  ARKBench task demonstrations (you generate these)
```

## Custom VLM Architecture (Your Original Idea)

### Building SigLIP + Qwen2.5 from Scratch

```python
import torch
import torch.nn as nn
from transformers import SiglipVisionModel, Qwen2ForCausalLM

class ARKVisionModel(nn.Module):
    def __init__(self):
        super().__init__()
        
        # ── Vision Encoder (FROZEN — already well-trained) ──────
        self.vision_encoder = SiglipVisionModel.from_pretrained(
            "google/siglip-large-patch16-384"
        )
        for param in self.vision_encoder.parameters():
            param.requires_grad = False    # freeze — don't update
        
        # ── Language Model (FROZEN or LoRA) ─────────────────────
        self.language_model = Qwen2ForCausalLM.from_pretrained(
            "Qwen/Qwen2.5-7B-Instruct"
        )
        
        # ── Projection Bridge (TRAINED — this is the key) ────────
        # SigLIP outputs 1152-dim vectors
        # Qwen2.5 expects 4096-dim vectors
        # Bridge maps one to the other
        self.projection = nn.Sequential(
            nn.Linear(1152, 4096),
            nn.GELU(),
            nn.Linear(4096, 4096),
            nn.LayerNorm(4096)
        )
    
    def encode_image(self, pixel_values):
        # Get patch features from SigLIP
        vision_outputs = self.vision_encoder(pixel_values)
        image_features = vision_outputs.last_hidden_state  # (B, 729, 1152)
        
        # Project to Qwen2.5 token space
        visual_tokens = self.projection(image_features)    # (B, 729, 4096)
        return visual_tokens
    
    def forward(self, pixel_values, input_ids, attention_mask):
        # Encode image to visual tokens
        visual_tokens = self.encode_image(pixel_values)    # (B, 729, 4096)
        
        # Get text token embeddings
        text_embeds = self.language_model.model.embed_tokens(input_ids)  # (B, T, 4096)
        
        # Concatenate: [visual tokens] + [text tokens]
        combined = torch.cat([visual_tokens, text_embeds], dim=1)  # (B, 729+T, 4096)
        
        # Run through language model
        outputs = self.language_model(inputs_embeds=combined,
                                       attention_mask=attention_mask)
        return outputs
```

**Training stages:**
```
Stage 1 — Alignment (2-3 days on RTX 5060):
  What: Train only the projection bridge
  Data: 558K image-caption pairs (LLaVA-CC3M-Pretrain-595K)
  Goal: Bridge learns to map SigLIP visual space → Qwen2.5 text space
  
Stage 2 — Instruction Tuning (3-4 days on RTX 5060):
  What: Train projection + Qwen2.5 with QLoRA
  Data: 150K visual instruction pairs (LLaVA-Instruct-150K)
        + your custom ARK OS scientific figure dataset
  Goal: Model follows visual instructions correctly
```

---

# PART 7 — INPUTS AND EVALUATION

## What Each ARK OS Agent Takes as Input

```
Agent               Input Type              Example
──────────────────────────────────────────────────────────────────────
Supervisor          ARKMessage              {task_id, intent, payload}
Planner             Natural language goal   "Research X and implement it"
Memory              Query + memory_type     "retrieve semantic top_k=5 about attention"
Research            Topic string            "transformer attention efficiency 2024-2026"
Coding              Code spec + context     "Write FastAPI endpoint given models.py"
Evaluation          Agent output dict       {code: "...", tests: "...", score: null}
Web Search          Query string            "latest NeurIPS 2025 multi-agent papers"
Browser             URL + instruction       "navigate to arxiv.org, search for..."
Terminal            Bash command string     "pip install torch --index-url..."
File System         File path + operation   "read /data/results.csv"
GitHub              Repo + action           "create PR for branch feature/memory-agent"
Document            File path               "/papers/attention_is_all_you_need.pdf"
Security            Target path/URL         "/home/claude/ark-os/" (local only)
Vision              Image + question        [screenshot.png] + "what error is shown?"
ML Engineer         Experiment config       {model: "resnet18", lr: 0.001, epochs: 10}
Data Analysis       DataFrame or CSV path   "analyze results.csv, plot loss curves"
```

## Evaluation Methods

### For ARK OS Agents (ARKBench)

```
Research Agent:
  ROUGE-L         → overlap between generated and reference summaries
  BERTScore       → semantic similarity using BERT embeddings
  Citation accuracy → did it correctly cite the right papers?
  Novelty score   → are hypotheses genuinely novel vs known work?

Coding Agent:
  Pass@k          → does code pass tests on k attempts?
  SWE-bench score → standard software engineering benchmark
  Coverage delta  → did it improve test coverage?
  Bug fix rate    → fraction of injected bugs correctly fixed

Security Agent:
  Recall          → fraction of known CVEs detected
  Precision       → fraction of findings that are true positives
  F1 score        → harmonic mean of recall + precision

Vision Agent:
  VQAv2           → visual question answering accuracy
  ScienceQA       → scientific figure understanding benchmark
  OCR accuracy    → character-level accuracy on text extraction

Overall System:
  Task completion rate → end-to-end success on ARKBench tasks
  Latency             → time to complete per task type
  Improvement rate    → Month 1 score vs Month 6 score (shows learning)
```

### For the Custom VLM (ARK-VLM)

```
MMBench        → comprehensive multimodal benchmark (200 tasks)
LLaVA-Bench    → visual instruction following quality
ScienceQA      → science figure understanding (critical for SAGE)
MMMU           → college-level multimodal reasoning
TextVQA        → text recognition in images (OCR quality)
ChartQA        → chart and plot understanding (Data Analysis Agent)
```

---

# PART 8 — KEY RESEARCH PAPERS (Read These)

## Foundation Papers (Read First)

```
1. "Attention Is All You Need" (2017)
   Authors: Vaswani et al., Google Brain
   arxiv.org/abs/1706.03762
   Why read: The Transformer architecture everything is built on.
             Understanding this paper = understanding all modern AI.

2. "Language Models are Few-Shot Learners" (2020)
   Authors: Brown et al., OpenAI (GPT-3 paper)
   arxiv.org/abs/2005.14165
   Why read: Demonstrated that scale + pretraining = emergent intelligence.
             Foundation for why LLMs work as agents.

3. "LLaMA: Open and Efficient Foundation Language Models" (2023)
   Authors: Touvron et al., Meta
   arxiv.org/abs/2302.13971
   Why read: The model architecture ARK OS local models descend from.
             Explains RoPE, grouped query attention, SwiGLU.
```

## Agent & Reasoning Papers

```
4. "ReAct: Synergizing Reasoning and Acting in Language Models" (2023)
   Authors: Yao et al., Princeton + Google
   arxiv.org/abs/2210.03629
   Why read: The exact framework (Thought → Action → Observation loop)
             every ARK OS agent uses internally.

5. "Toolformer: Language Models Can Teach Themselves to Use Tools" (2023)
   Authors: Schick et al., Meta
   arxiv.org/abs/2302.04761
   Why read: How LLMs learn to call external tools during generation.

6. "AutoGPT: An Autonomous GPT-4 Experiment" (2023)
   GitHub: github.com/Significant-Gravitas/AutoGPT
   Why read: First popular autonomous agent. Study its failures —
             ARK OS is designed to fix all of them.

7. "MetaGPT: Meta Programming for a Multi-Agent Collaborative Framework" (2023)
   Authors: Hong et al.
   arxiv.org/abs/2308.00352
   Why read: Multi-agent for software engineering. Direct competitor
             to ARK OS coding pipeline. Study their evaluation method.
```

## Memory & RAG Papers

```
8. "Retrieval-Augmented Generation for Knowledge-Intensive NLP Tasks" (2020)
   Authors: Lewis et al., Meta
   arxiv.org/abs/2005.11401
   Why read: Original RAG paper. Foundation for Research Agent's 
             literature retrieval system.

9. "MemGPT: Towards LLMs as Operating Systems" (2023)
   Authors: Packer et al., UC Berkeley
   arxiv.org/abs/2310.08560
   Why read: The closest existing work to ARK OS memory architecture.
             Their hierarchical memory = inspiration for our 4-tier system.
             Direct comparison paper for your publication.
```

## Vision-Language Papers

```
10. "Learning Transferable Visual Models From Natural Language Supervision"
    (CLIP, 2021)
    Authors: Radford et al., OpenAI
    arxiv.org/abs/2103.00020
    Why read: CLIP vision encoder — trained text-image pairs to create
              universal visual features. Foundation for all VLMs.

11. "Sigmoid Loss for Language Image Pre-Training" (SigLIP, 2023)
    Authors: Zhai et al., Google
    arxiv.org/abs/2303.15343
    Why read: Our chosen vision encoder. Better than CLIP for 
              image-text alignment at same parameter count.

12. "Visual Instruction Tuning" (LLaVA, 2023)
    Authors: Liu et al., University of Wisconsin
    arxiv.org/abs/2304.08485
    Why read: The exact architecture (CLIP encoder + projection + LLM)
              you would build for ARK-VLM.
              Read both LLaVA and LLaVA-1.5 (arxiv.org/abs/2310.03744).
```

## Scientific Discovery Papers

```
13. "Towards an AI Physicist for Unsupervised Learning" (2019)
    Authors: Wu & Tegmark, MIT
    arxiv.org/abs/1810.10525
    Why read: Early work on AI-generated scientific hypotheses.
              Foundation for SAGE Mode's hypothesis engine.

14. "Can Large Language Models be Used to Provide Trustworthy 
    Automatic Evaluation of Retrieval-Augmented Generation?" (2024)
    arxiv.org/abs/2402.15485
    Why read: How to evaluate RAG pipeline quality automatically.
              Directly applicable to Research Agent evaluation.

15. "SWE-bench: Can Language Models Resolve Real-World GitHub Issues?" (2024)
    Authors: Jimenez et al., Princeton
    arxiv.org/abs/2310.06770
    Why read: The gold standard benchmark for coding agents.
              ARKBench coding tasks are modeled after SWE-bench.
```

## Fine-Tuning Papers

```
16. "LoRA: Low-Rank Adaptation of Large Language Models" (2022)
    Authors: Hu et al., Microsoft
    arxiv.org/abs/2106.09685
    Why read: The technique you'll use to fine-tune every ARK OS agent.
              Must-read before writing any training code.

17. "QLoRA: Efficient Finetuning of Quantized LLMs" (2023)
    Authors: Dettmers et al., University of Washington
    arxiv.org/abs/2305.14314
    Why read: How to fine-tune 70B models on a single GPU.
              Introduces NF4 quantization + double quantization.
              The exact technique for training on your RTX 5060.
```

---

# PART 9 — DATASETS

## For Training/Fine-Tuning ARK OS Agents

```
Task                  Dataset                         Size      Source
────────────────────────────────────────────────────────────────────────────
General instruction   OpenHermes-2.5                  1M rows   HuggingFace
                      teknium/OpenHermes-2.5-Mistral-7B

Code generation       CodeAlpaca-20K                  20K       HuggingFace
                      sahil2801/CodeAlpaca-20k

Code repair           SWE-bench                       2K tasks  github.com/princeton-nlp/SWE-bench
                      princeton-nlp/SWE-bench

Tool use / ReAct      ToolBench                       16K tools HuggingFace
                      ToolBench/ToolBench

Research synthesis    S2ORC (Semantic Scholar Open)   200M      semanticscholar.org/product/api
                      Research Corpus

RAG evaluation        BEIR Benchmark                  18 tasks  github.com/beir-cellar/beir

VLM alignment         LLaVA-CC3M-Pretrain-595K        595K      HuggingFace
                      liuhaotian/LLaVA-CC3M-Pretrain-595K

VLM instruction       LLaVA-Instruct-150K             150K      HuggingFace
                      liuhaotian/LLaVA-Instruct-150K

Scientific figures    ScienceQA                       21K Q&A   HuggingFace
                      derek-thomas/ScienceQA

Security/CVE          NVD CVE Database                200K+     nvd.nist.gov/developers
                      + ExploitDB                               exploit-db.com

Financial data        Yahoo Finance via yfinance       Real-time pip install yfinance
                      (for SAGE finance demo)
```

## For ARK OS Evaluation (ARKBench)

```
Component             Source                          Why
────────────────────────────────────────────────────────────────────────────
Coding tasks          SWE-bench-lite (300 tasks)      Gold standard, reproducible
Research tasks        Create from arXiv abstracts     Custom, ARK OS specific
Security tasks        CVE-2023/2024 sample set        Real vulnerabilities
Multimodal tasks      MMBench subset                  Standard VLM evaluation
Agent coordination    Hand-crafted (50 scenarios)     Novel contribution
```

---

# PART 10 — EXISTING SIMILAR PROJECTS (Competitors + Comparisons)

## Direct Competitors

```
Project          What It Is                         Why ARK OS Is Different
──────────────────────────────────────────────────────────────────────────────
AutoGPT          Single GPT-4 agent with tools      No persistent memory,
                 (github.com/Significant-Gravitas/   no multi-agent, deprecated
                 AutoGPT)

BabyAGI          Task-chaining with GPT-4            No tool use, no memory,
                 (github.com/yoheinakajima/babyagi)  single-agent only

Devin            Autonomous coding agent             Code only, closed source,
                 (cognition.ai/devin)                no research/security/SAGE

OpenManus        General-purpose agent               No OS-level memory,
                 (github.com/manusAI/manus)          no multi-agent kernel

MetaGPT          Multi-agent for software            Software engineering only,
                 (github.com/geekan/MetaGPT)         no scientific discovery

CrewAI           Multi-agent orchestration           No persistent memory,
                 (github.com/crewAIInc/crewAI)       no kernel architecture

LangGraph        Stateful agent graphs               Framework only, not a system
                 (langchain-ai/langgraph)

SWE-Agent        Software engineering agent          Single domain, evaluation
                 (princeton-nlp/SWE-agent)           tool not a deployed system

MemGPT           LLM with hierarchical memory        Single agent, no multi-agent
                 (lm-sys/MemGPT)                     coordination, no SAGE

AI Scientist      Scientific discovery (ICLR paper)  No OS architecture, single
(Sakana AI)      (github.com/SakanaAI/AI-Scientist)  domain (ML papers only),
                                                      no agent coordination
```

## The Closest: Sakana AI's "AI Scientist"

This is your most important comparison and inspiration:

```
AI Scientist (2024):
  - Generates ML research ideas automatically
  - Writes code to test them
  - Produces full academic papers
  - Published at ICLR 2024
  - Paper: arxiv.org/abs/2408.06292

ARK OS SAGE Mode vs AI Scientist:
  AI Scientist:  single domain (ML), single agent, no memory, 
                 no coding/security/general agents, closed loop
  
  ARK OS SAGE:   multi-domain (any science), multi-agent kernel,
                 4-tier persistent memory, coding+security+research
                 all connected, self-improving over time

Your paper title should explicitly compare to AI Scientist:
"Beyond Single-Domain Discovery: ARK OS as a Multi-Agent 
Scientific Discovery Operating System"
```

---

# PART 11 — THE COMPLETE PICTURE

## ARK OS In One Diagram

```
                         ┌─────────────────────────────────────────┐
USER / RESEARCHER        │              ARK OS                      │
                         │                                          │
"Research attention      │  ┌──────────────────────────────────┐   │
 efficiency and build    │  │        KERNEL LAYER               │   │
 an implementation"  ────┼─▶│ Supervisor → Planner → Evaluator │   │
                         │  │ Memory Agent (4-tier)             │   │
                         │  │ Agent Registry                    │   │
                         │  └──────────┬───────────────────────┘   │
                         │             │ task DAG                   │
                         │  ┌──────────▼───────────────────────┐   │
                         │  │      WORKER AGENT LAYER           │   │
                         │  │                                   │   │
                         │  │  Research ──▶ Coding ──▶ Testing  │   │
                         │  │  Web Search   Debugging  Security │   │
                         │  │  Browser      GitHub     DevOps   │   │
                         │  │  Terminal     Vision     ML Eng.  │   │
                         │  │                                   │   │
                         │  └──────────┬───────────────────────┘   │
                         │             │ tool calls                 │
                         │  ┌──────────▼───────────────────────┐   │
                         │  │         TOOL ECOSYSTEM            │   │
                         │  │ Playwright · Bash · Git · Qdrant  │   │
                         │  │ arXiv API · yfinance · MLflow     │   │
                         │  │ Semgrep · Docker · FastAPI        │   │
                         │  └──────────────────────────────────┘   │
                         │                                          │
                         │  ┌──────────────────────────────────┐   │
                         │  │    SAGE MODE (scientific loop)    │   │
                         │  │ Literature → Hypothesis →         │   │
                         │  │ Experiment → Analysis → Paper     │   │
                         │  └──────────────────────────────────┘   │
                         └─────────────────────────────────────────┘
                                          │
                                          ▼
                         ┌─────────────────────────────────────────┐
                         │           MODEL LAYER                   │
                         │  Groq API: Qwen2.5-72B (orchestration) │
                         │  Ollama:   Gemma4-26B-A4B (general)    │
                         │  Ollama:   Qwen2.5-Coder-14B (code)    │
                         │  Ollama:   Qwen2-VL-7B (vision)        │
                         └─────────────────────────────────────────┘
```

## Summary of Everything

```
What is it?        Autonomous multi-agent AI operating system

Problem it solves  Knowledge work is fragmented, stateless, and manual.
                   No unified system reads, codes, discovers, and secures.

How it works       20 specialized agents share a 4-tier memory and
                   communicate via ARKMessage protocol, orchestrated
                   by a kernel (Supervisor + Planner + Memory + Evaluation)

Core tech          LangGraph, Ollama, Qdrant, Redis, SQLite, FastAPI

Core models        Gemma4-26B-A4B (general) + Qwen2.5-Coder-14B (code)
                   + Qwen2-VL-7B (vision) + Qwen2.5-72B via Groq (orchestration)

How models work    Transformer architecture: self-attention over tokens
                   MoE: 26B knowledge at 4B compute cost
                   QLoRA: fine-tune 7B models on 8GB GPU

SAGE Mode          Scientific discovery loop: literature → hypothesis
                   → experiment → analysis → paper draft

Publications       4-5 papers: architecture, conflict resolution,
                   hypothesis generation, ARKBench, custom VLM

Existing projects  AI Scientist (closest), AutoGPT (old), MetaGPT (code only)
                   ARK OS is the only system with: OS kernel + 4-tier memory
                   + multi-domain + scientific discovery + self-improvement

Timeline           6 months, 20 agents, 3 phases, 1 GPU, ₹0 extra cost
```

---

*End of ARK OS Complete Explanation — v1.0*
*For: Kabilash (E0323036), SRIHER Chennai, 2026*
