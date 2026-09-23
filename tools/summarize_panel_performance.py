"""Summarize Waveshare diagnostic serial records without inferring missing samples."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

FIELDS = re.compile(r"(\w+)=([^=]+?)(?=\s+\w+=|$)")


def distribution(values: list[float]) -> dict:
    ordered = sorted(values)
    return {"n": len(ordered), "median": statistics.median(ordered),
            "p95": ordered[math.ceil(len(ordered) * .95) - 1], "max": ordered[-1]} if ordered else {"n": 0}


def summarize(text: str, counter_bits: int = 32, cores: int = 2) -> dict:
    frames = defaultdict(dict)
    resources = []
    task_samples = defaultdict(list)
    snapshots = {}
    dropped = defaultdict(int)
    boot = 0
    boot_markers = 0
    malformed = 0
    duplicate = 0
    previous_resource_time = None
    for line in text.splitlines():
        if "ESP-ROM:" in line:
            boot += 1
            boot_markers += 1
            previous_resource_time = None
        match = re.search(r"\b(PERF_\w+) (.*)", line)
        if not match:
            continue
        tag, payload = match.groups()
        record = dict(FIELDS.findall(payload))
        try:
            if tag == "PERF_DISPLAY":
                identity = (boot, int(record["id"]))
                kind = record["kind"]
                if kind not in ("render", "dma"):
                    raise ValueError("unknown display record")
                item = {key: int(record[key]) for key in
                        ("start_us", "first_flush_us", "end_us", "bytes", "strips")}
                item["page"] = record.get("page", "unlabelled")
                item["forced"] = record.get("forced", "unknown")
                if kind in frames[identity]:
                    duplicate += 1
                frames[identity][kind] = item
            elif tag == "PERF_RESOURCE":
                item = {key: int(value) for key, value in record.items()}
                if previous_resource_time is not None and item["t_us"] < previous_resource_time:
                    # A clock restart without a ROM marker cannot safely join earlier frames.
                    malformed += 1
                previous_resource_time = item["t_us"]
                resources.append(item)
                dropped[boot] = max(dropped[boot], item["dropped"])
            elif tag == "PERF_TASK_SNAPSHOT":
                snapshots[boot, int(record["t_us"])] = int(record["total_us"])
                if int(record["count"]) == 0:
                    malformed += 1
            elif tag == "PERF_TASK":
                item = {key: int(record[key]) for key in
                        ("t_us", "id", "core", "runtime_us", "stack_bytes")}
                item["name"] = record["name"]
                task_samples[boot, item["id"]].append(item)
        except (KeyError, ValueError):
            malformed += 1

    grouped = defaultdict(lambda: defaultdict(list))
    unmatched = 0
    invalid_frames = 0
    for pair in frames.values():
        if set(pair) != {"render", "dma"}:
            unmatched += 1
            continue
        render, dma = pair["render"], pair["dma"]
        if (any(render[key] != dma[key] for key in ("start_us", "first_flush_us", "bytes", "strips", "page", "forced"))
                or render["end_us"] < render["start_us"]
                or dma["end_us"] < dma["first_flush_us"]
                or not dma["start_us"] <= dma["first_flush_us"]
                or dma["bytes"] <= 0 or dma["strips"] <= 0):
            invalid_frames += 1
            continue
        group = grouped[f'page={dma["page"]},forced={dma["forced"]}']
        group["render_ms"].append((render["end_us"] - render["start_us"]) / 1000)
        group["dma_complete_ms"].append((dma["end_us"] - dma["start_us"]) / 1000)
        group["dma_minus_render_ms"].append((dma["end_us"] - render["end_us"]) / 1000)
        group["bytes"].append(dma["bytes"])
        group["strips"].append(dma["strips"])

    tasks = []
    for (segment, identity), samples in task_samples.items():
        runtime = 0
        wall = 0
        windows = 0
        for previous, current in zip(samples, samples[1:]):
            before = snapshots.get((segment, int(previous["t_us"])))
            after = snapshots.get((segment, int(current["t_us"])))
            if before is None or after is None:
                continue
            elapsed = (after - before) % (1 << counter_bits)
            if elapsed == 0:
                continue
            used = (int(current["runtime_us"]) - int(previous["runtime_us"])) % (1 << counter_bits)
            # The small allowance accounts for the snapshot crossing a context switch.
            if used > elapsed * 1.05:
                malformed += 1
                continue
            runtime += used
            wall += elapsed
            windows += 1
        tasks.append({"boot": segment, "id": identity, "name": samples[-1]["name"],
                      "core_affinity": int(samples[-1]["core"]), "windows": windows,
                      "single_core_percent": 100 * runtime / wall if wall else None,
                      "system_capacity_percent": 100 * runtime / wall / cores if wall else None,
                      "stack_min_bytes": min(int(sample["stack_bytes"]) for sample in samples)})

    memory = {}
    for field in ("internal_free", "internal_largest", "internal_min", "dma_free", "dma_largest", "psram_free"):
        values = [sample[field] for sample in resources if field in sample]
        if values:
            memory[field] = {"minimum": min(values), "first": values[0], "last": values[-1]}
    return {"boot_markers": boot_markers, "malformed_records": malformed,
            "duplicate_display_records": duplicate, "unmatched_frames": unmatched,
            "invalid_frames": invalid_frames, "dropped_records": sum(dropped.values()),
            "resource_samples": len(resources),
            "display": {label: {metric: distribution(values) for metric, values in group.items()}
                        for label, group in grouped.items()},
            "memory": memory,
            "poll_gap_max_us": max((item.get("poll_gap_max_us", 0) for item in resources), default=None),
            "task_counter_bits": counter_bits, "cpu_cores": cores, "tasks": tasks}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--counter-bits", type=int, choices=(32, 64), default=32)
    args = parser.parse_args()
    result = summarize(args.log.read_text(encoding="utf-8", errors="replace"), args.counter_bits)
    print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
    incomplete = any(result[key] for key in (
        "malformed_records", "invalid_frames", "duplicate_display_records",
        "unmatched_frames", "dropped_records"))
    return 1 if incomplete or not result["display"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
