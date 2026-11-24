# ARK-VLM paper status: native inference scope

## Status

The planned projection-plus-LoRA ARK-VLM training study has not been run. The
current C++ runtime can load and query compatible pretrained llama.cpp
multimodal GGUF models, but this checkout does not contain a supported native
multimodal trainer, a compatible trainable base model, or the guide's training
datasets. This document records a narrower inference-methods scope; it is not
a trained-model paper or a report of model-quality results.

## Evidence available

- C++ `VisionAgent` and `VisionRuntime` support image requests through local
  llama.cpp inference and a matching projector.
- `vision-check` checks the running llama-server `/props` response for
  multimodal support before inference.
- Unit tests cover server capability parsing, payload validation, image
  handling, and structured confusion-matrix output.
- A prior local fixture smoke used a small SmolVLM GGUF pair. It is not a
  training run or a benchmark result.
- No live VLM server or model is available in the current environment, so
  current live image inference is unverified.

See the implementation and test details in
[`native_cpp.md`](../native_cpp.md#day-86-native-vlm-runtime-setup) and the
training constraints in [`day94_vlm_training.md`](../day94_vlm_training.md).

## Claims not supported by current evidence

- ARK has not trained a custom vision-language model.
- There is no training loss curve, held-out score, or comparison against a
  pretrained baseline from this project.
- Unit tests do not establish image-understanding quality, latency, or
  robustness on a representative dataset.
- The current model configuration does not prove that model weights are
  installed or that the configured vision profile is compatible with the
  running inference server.

## Requirements for a defensible empirical paper

1. Select a C++-compatible multimodal training backend and establish that it
   supports the intended projection and LoRA operations.
2. Pin a compatible base model, projector, tokenizer, training recipe, and
   hardware/software environment.
3. Obtain and document dataset provenance, licenses, splits, and leakage
   controls for both training and held-out evaluation.
4. Pre-register the task set, metrics, baselines, and stopping criteria.
5. Train and retain checkpoints, logs, seeds, configuration, and resource
   measurements.
6. Evaluate the trained model and the pretrained baseline on the same held-out
   examples, then independently reproduce the scoring.

Until these conditions are met, the paper should remain an implementation
methods or feasibility report and make no custom-model capability claim.
