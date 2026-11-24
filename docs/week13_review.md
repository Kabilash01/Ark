# Week 13 review: SAGE and native VLM

## Implemented

- The native SAGE materials workflow has literature, hypothesis, experiment
  design, Materials Project screening, report, and optional figure-analysis
  stages. It checkpoints successful stages and can resume.
- The workflow generates a second hypothesis experiment and report without
  repeating literature retrieval or requesting the same materials data twice.
- Hypotheses cite only IDs present in retrieved literature. Reports preserve
  those references and can include captions from the native Vision Agent.
- `vision-check` queries llama.cpp `/props` before image inference. The C++
  test suite covers its capability parsing and the Vision Agent's confusion
  matrix output.

## Verification

- `cmake --build build -j 6` completed.
- `ctest --test-dir build --output-on-failure`: 33 passed, 2 skipped. Browser
  and Phase 2 tests were skipped by their service gates.
- The SAGE CLI preflights `MP_API_KEY` and safe figure paths before creating
  loop checkpoints. The current environment lacks the Materials Project key
  and the local Ollama, Qdrant, and Neo4j listeners, so the live workflow did
  not run.
- The native `llama-server` binary and RTX 5060 are present, but no model is
  installed and no server is running. Live VLM loading and image inference
  remain unverified.

## Training decision

The guide's SigLIP/Qwen projection and LoRA training stages use Python/PyTorch.
They were not started because this project is being kept C++ only. The native
path uses pretrained llama.cpp multimodal GGUF models; no training loss curve
exists to monitor, and Stage 2 training is not applicable to this inference
path.

## Remaining verification

Run `ark-cpp sage-materials` with configured local services, `MP_API_KEY`, and
two or more material IDs. Repeat with two relative figure paths to verify live
captioning and the generated `.tex` reports. PDF compilation also remains
unverified because neither `pdflatex` nor `tectonic` is installed.

The Week 14 native self-improvement implementation follows the same local-only
model direction. Its A/B gate is fixture-tested, but no live prompt candidate
has been promoted because the local inference and Redis services are not
running in this environment.
