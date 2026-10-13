#!/usr/bin/env python3
"""Reproduce DreamDSP's Synthetic Memory Drive 8-unit echo-state network.

Python 3.10+, standard library only; no downloaded audio, pretrained weights,
hardware captures, or third-party training code. The recurrent reservoir is
fixed and contractive; its readout is trained with ridge regression on the
synthetic dynamic saturation teacher below. This is deliberately an echo-state
network, not a GRU/LSTM or a claim to reproduce real tape/tube hardware.

Run from any directory: python scripts/train-neural-saturation.py
The header and JSON are deterministic to normal floating-point roundoff.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import random

SEED = 20261013
UNITS = 8
RATE = 48000
ROOT = Path(__file__).resolve().parents[1]
LEAK = [1.0, 1.0, 0.16, 0.16, 0.008, 0.008, 0.06, 0.4]
INPUT = [1.5, 3.0, 1.5, 3.0, 1.5, 3.0, 0.7, 2.1]


def reservoir_weights():
    rng = random.Random(SEED)
    # Bound each row's absolute sum below 0.1: tanh is 1-Lipschitz,
    # so the leaky reservoir has a unique fading-memory state.
    rows = []
    for unit in range(UNITS):
        row = [rng.uniform(-0.004, 0.004) for _ in range(UNITS)]
        row[unit] += 0.045
        rows.append(row)
    return rows


RECURRENT = reservoir_weights()


def forward(x, state):
    candidate = [math.tanh(INPUT[j] * x + sum(w * h for w, h in zip(RECURRENT[j], state)))
                 for j in range(UNITS)]
    state[:] = [h + leak * (y - h) for h, leak, y in zip(state, LEAK, candidate)]
    return [x] + state


def teacher(x, state):
    shaped = 0.8 * math.tanh(1.5 * x) + 0.2 * math.tanh(3.0 * x)
    state[0] += 0.16 * (shaped - state[0])
    state[1] += 0.008 * (shaped - state[1])
    return 0.82 * shaped + 0.15 * state[0] + 0.03 * state[1]


def signal(seed, frames, kind):
    rng = random.Random(seed)
    phase = rng.random() * 2.0 * math.pi
    slow = 0.0
    f1 = rng.uniform(35.0, 3500.0)
    f2 = rng.uniform(150.0, 11000.0)
    amplitude = rng.uniform(0.03, 1.85)
    for i in range(frames):
        noise = rng.uniform(-1.0, 1.0)
        slow += 0.04 * (noise - slow)
        t = i / RATE
        if kind == 0:
            x = math.sin(phase + 2.0 * math.pi * f1 * t)
        elif kind == 1:
            x = 0.62 * math.sin(2.0 * math.pi * f1 * t) + 0.38 * math.sin(phase + 2.0 * math.pi * f2 * t)
        elif kind == 2:
            x = 0.5 * noise + 2.0 * slow
        elif kind == 3:
            x = math.tanh(3.0 * math.sin(phase + 2.0 * math.pi * f1 * t))
        elif kind == 4:
            frequency = 30.0 * math.exp(math.log(18000.0 / 30.0) * i / frames)
            phase += 2.0 * math.pi * frequency / RATE
            x = math.sin(phase)
        else:
            x = noise if i % 1024 < 128 else 0.0
        # Include a decaying note envelope and a silent tail.
        envelope = 0.2 + 0.8 * math.exp(-4.0 * (i % 4096) / 4096.0)
        yield max(-2.0, min(2.0, amplitude * x * envelope)) if i < frames - 256 else 0.0


def solve(matrix, target):
    """Small pivoted Gaussian solve; all training is float64."""
    a = [row[:] + [b] for row, b in zip(matrix, target)]
    n = len(target)
    for k in range(n):
        pivot = max(range(k, n), key=lambda j: abs(a[j][k]))
        a[k], a[pivot] = a[pivot], a[k]
        divisor = a[k][k]
        if abs(divisor) < 1e-14:
            raise RuntimeError("singular training matrix")
        a[k] = [v / divisor for v in a[k]]
        for j in range(n):
            if j == k:
                continue
            factor = a[j][k]
            a[j] = [v - factor * p for v, p in zip(a[j], a[k])]
    return [row[-1] for row in a]


def train():
    size = UNITS + 1
    gram = [[0.0] * size for _ in range(size)]
    rhs = [0.0] * size
    count = 0
    for clip in range(36):
        state, truth = [0.0] * UNITS, [0.0, 0.0]
        for x in signal(SEED + clip, 6144, clip % 6):
            features = forward(x, state)
            y = teacher(x, truth)
            for j in range(size):
                rhs[j] += features[j] * y
                for k in range(j + 1):
                    gram[j][k] += features[j] * features[k]
            count += 1
    for j in range(size):
        for k in range(j):
            gram[k][j] = gram[j][k]
        gram[j][j] += count * 1e-6
    return solve(gram, rhs), count


def validate(readout):
    error = energy = 0.0
    worst = 0.0
    count = 0
    for clip in range(12):
        state, truth = [0.0] * UNITS, [0.0, 0.0]
        for x in signal(SEED + 1000 + clip, 8192, clip % 6):
            y = sum(w * h for w, h in zip(readout, forward(x, state)))
            expected = teacher(x, truth)
            error += (y - expected) ** 2
            energy += expected ** 2
            worst = max(worst, abs(y - expected))
            count += 1
    return {"samples": count, "error_to_signal_db": 10.0 * math.log10(error / energy),
            "rmse": math.sqrt(error / count), "max_absolute_error": worst}


def cpp_float(x):
    text = format(x, ".9g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def export(readout, train_samples, metrics):
    destination = ROOT / "dsp" / "models"
    destination.mkdir(parents=True, exist_ok=True)
    metadata = {
        "name": "Synthetic Memory Drive", "version": 1,
        "architecture": "8-unit leaky echo-state recurrent network; trained linear readout",
        "provenance": "Original deterministic synthetic DSP teacher and original training script; no hardware capture or third-party weights",
        "sample_rate": RATE, "input_range": [-2.0, 2.0], "seed": SEED,
        "training_samples": train_samples, "validation": metrics,
        "limitations": ["Not an emulation of measured hardware", "No oversampling; drive can generate aliasing", "48 kHz only"],
        "input_weights": INPUT, "leak": LEAK, "recurrent": RECURRENT,
        "readout": readout,
    }
    (destination / "SyntheticMemoryDrive.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    lines = ["#pragma once", "", "// Generated by scripts/train-neural-saturation.py (seed 20261013).",
             "// Original synthetic teacher; NOT measured tape/tube/amplifier hardware.",
             "// 48 kHz, eight leaky recurrent units and a trained linear readout.",
             "namespace dreamdsp::dsp::synthetic_memory_drive {", "constexpr int kUnits = 8;"]
    for name, values in [("kInput", INPUT), ("kLeak", LEAK), ("kReadout", readout)]:
        lines.append(f"constexpr float {name}[{len(values)}] = {{ " + ", ".join(map(cpp_float, values)) + " };")
    lines.append("constexpr float kRecurrent[8][8] = {")
    for row in RECURRENT:
        lines.append("    { " + ", ".join(map(cpp_float, row)) + " },")
    lines += ["};", "} // namespace dreamdsp::dsp::synthetic_memory_drive", ""]
    (destination / "SyntheticMemoryDrive.h").write_text("\n".join(lines), encoding="utf-8")
    # Independent float64 oracle for the C++ streaming/DC-blocker regression.
    inputs = [0.3 * math.sin(i * 0.23) + 0.2 * math.cos(i * 0.73) for i in range(64)]
    state, outputs = [0.0] * UNITS, []
    previous_x = previous_y = 0.0
    dc_r = 1.0 - 2.0 * math.pi * 5.0 / RATE
    for x in inputs:
        wet = sum(w * h for w, h in zip(readout, forward(x, state)))
        y = wet - previous_x + dc_r * previous_y
        previous_x, previous_y = wet, y
        outputs.append(y)
    reference = ["#pragma once", "// Generated independently in Python float64 for C++ regression tests.",
                 "namespace dreamdsp::dsp::synthetic_memory_drive_reference {"]
    for name, values in [("kInput", inputs), ("kOutput", outputs)]:
        reference.append(f"constexpr float {name}[64] = {{ " + ", ".join(map(cpp_float, values)) + " };")
    reference += ["} // namespace dreamdsp::dsp::synthetic_memory_drive_reference", ""]
    (destination / "SyntheticMemoryDriveReference.h").write_text("\n".join(reference), encoding="utf-8")


if __name__ == "__main__":
    weights, samples = train()
    validation = validate(weights)
    if validation["error_to_signal_db"] > -20.0:
        raise RuntimeError(f"model failed holdout quality floor: {validation}")
    export(weights, samples, validation)
    print(json.dumps({"training_samples": samples, "validation": validation}, indent=2), flush=True)
