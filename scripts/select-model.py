"""Hardware auto-detection: picks a model tier and writes config/active-models.json."""
import json
import platform
import subprocess
from pathlib import Path

import psutil

REPO_ROOT = Path(__file__).resolve().parent.parent
MODEL_LIBRARY_PATH = REPO_ROOT / "config" / "model-library.json"
ACTIVE_MODELS_PATH = REPO_ROOT / "config" / "active-models.json"


def detect_gpu_vram() -> float:
    try:
        r = subprocess.run(
            ["nvidia-smi", "--query-gpu=memory.total", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
        )
        if r.returncode == 0:
            return round(int(r.stdout.strip().split("\n")[0]) / 1024, 1)
    except FileNotFoundError:
        pass
    if platform.system() == "Darwin" and platform.machine() == "arm64":
        return round(psutil.virtual_memory().total / (1024**3), 1)
    return 0.0


def assign_tier(vram_gb: float) -> str:
    if vram_gb == 0:
        return "T5"
    if vram_gb < 6:
        return "T1"
    if vram_gb < 14:
        return "T2"
    if vram_gb < 22:
        return "T3"
    return "T4"


def main() -> None:
    vram = detect_gpu_vram()
    ram = round(psutil.virtual_memory().total / (1024**3), 1)
    tier = assign_tier(vram)

    with open(MODEL_LIBRARY_PATH) as f:
        library = json.load(f)
    models = library["tiers"][tier]["models"]

    ACTIVE_MODELS_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(ACTIVE_MODELS_PATH, "w") as f:
        json.dump({"tier": tier, "vram_gb": vram, "ram_gb": ram, "models": models}, f, indent=2)

    print(f"Detected: {vram}GB VRAM, {ram}GB RAM -> Tier {tier}")
    print(f"Models: {models}")


if __name__ == "__main__":
    main()
