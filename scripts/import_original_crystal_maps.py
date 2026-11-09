#!/usr/bin/env python3
"""Convert the original NSYNC crystal worlds into runtime map JSON.

The original PvP worlds share a 32x16 quadrant coordinate space.  World1 was
already imported with the same transform used by import_original_map.py:

    game_x = world_x - 2.2
    game_y = 43.6 - world_y

World2 and World7 are the two maps registered under "pvp/crystals" in the
original index.  This tool emits their borders, one-way platforms, jump pads,
crystal spawn points, depots and player entries without needing UnityPy.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

ORIGIN_X = 2.2
ORIGIN_Y = 43.6
THICKNESS = 0.06
DIAGONAL_STEP = 0.5


def transform(x: float, y: float) -> tuple[float, float]:
    return (round(x - ORIGIN_X, 4), round(ORIGIN_Y - y, 4))


def border_boxes(borders: list[dict]) -> list[list[float]]:
    boxes: list[list[float]] = []
    for border in borders:
        ax, ay = transform(border["v1"][0], border["v1"][1])
        bx, by = transform(border["v2"][0], border["v2"][1])
        dx = bx - ax
        dy = by - ay
        length = math.hypot(dx, dy)
        if length < 1e-5:
            continue
        axis_aligned = abs(dx) < 1e-4 or abs(dy) < 1e-4
        steps = 1 if axis_aligned else max(1, int(math.ceil(length / DIAGONAL_STEP)))
        for index in range(steps):
            t0 = index / steps
            t1 = (index + 1) / steps
            x0, y0 = ax + dx * t0, ay + dy * t0
            x1, y1 = ax + dx * t1, ay + dy * t1
            min_x = min(x0, x1) - THICKNESS
            max_x = max(x0, x1) + THICKNESS
            min_y = min(y0, y1) - THICKNESS
            max_y = max(y0, y1) + THICKNESS
            if max_x - min_x < 0.12:
                center = (min_x + max_x) * 0.5
                min_x, max_x = center - 0.06, center + 0.06
            if max_y - min_y < 0.12:
                center = (min_y + max_y) * 0.5
                min_y, max_y = center - 0.06, center + 0.06
            boxes.append([
                round(min_x, 4),
                round(min_y, 4),
                round(max_x - min_x, 4),
                round(max_y - min_y, 4),
            ])
    return boxes


def platform_rect(platform: dict) -> tuple[list[float], bool, float]:
    points = [transform(x, y) for x, y in platform["shape"]["vertices"]]
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    rect = [
        round(min(xs), 4),
        round(min(ys), 4),
        round(max(xs) - min(xs), 4),
        round(max(ys) - min(ys), 4),
    ]
    hatchway = bool(platform.get("hatchway", False))
    jump_velocity = float(platform.get("jumpVelocity", 0.0))
    return rect, hatchway, jump_velocity


def build_map(world: dict, map_id: str) -> dict:
    borders = [item for item in world["items"] if item.get("def") == "border"]
    platforms = [item for item in world["items"] if item.get("def") == "platform"]

    solids = border_boxes(borders)
    border_lines: list[list[float]] = []
    for border in borders:
        ax, ay = transform(border["v1"][0], border["v1"][1])
        bx, by = transform(border["v2"][0], border["v2"][1])
        if math.hypot(bx - ax, by - ay) < 1e-5:
            continue
        border_lines.append([
            round(ax, 4),
            round(ay, 4),
            round(bx, 4),
            round(by, 4),
        ])
    platform_rects: list[list[float]] = []
    platform_shapes: list[list[list[float]]] = []
    platform_hatchway: list[bool] = []
    jump_pads: list[dict] = []
    for platform in platforms:
        rect, hatchway, jump_velocity = platform_rect(platform)
        platform_rects.append(rect)
        platform_hatchway.append(hatchway)
        points = [
            list(transform(x, y))
            for x, y in platform["shape"]["vertices"]
        ]
        if len(points) != 4:
            raise SystemExit(
                f"{map_id}: expected 4 platform vertices, got {len(points)}")
        platform_shapes.append(points)
        if jump_velocity > 0.0:
            jump_pads.append({
                "rect": rect,
                "velocity": round(jump_velocity / 60.0, 6),
            })

    spawns = []
    for entry in world["playerEntries"]:
        x, y = transform(entry[0], entry[1])
        spawns.append([x, y])
    if len(spawns) != 10:
        raise SystemExit(f"{map_id}: expected 10 player entries, got {len(spawns)}")

    control_points = None
    drop_spawns: list[dict] = []
    drop_depots: list[dict] = []
    supply_spawners: list[dict] = []
    supply_ids = {102: "health_box", 105: "double_damage"}
    for plugin in world["plugins"]:
        if plugin.get("def") == "supply_gen":
            supplies = [
                supply_ids[item]
                for item in plugin.get("supplies", [])
                if item in supply_ids
            ]
            entries = [
                list(transform(entry[0], entry[1]))
                for entry in plugin.get("entries", [])
            ]
            if supplies and entries:
                supply_spawners.append({
                    "supplies": supplies,
                    "entries": entries,
                    "start_tick": max(
                        1, int(round(plugin.get("initialDelaySecs", 0) * 60))),
                    "min_interval": int(round(
                        plugin.get("minIntervalSecs", 0) * 60)),
                    "max_interval": int(round(
                        plugin.get("maxIntervalSecs", 0) * 60)),
                })
        elif plugin.get("def") == "cp_gen":
            frames_per_tick = int(plugin["framesPerTick"])
            point = plugin["points"][0]
            width, height = point["dimensions"]
            entries = [
                list(transform(entry[0], entry[1]))
                for entry in plugin["entries"]
            ]
            control_points = {
                # Original frames run at 30 Hz; the runtime is 60 Hz.
                "update_interval": max(1, frames_per_tick * 2),
                "start_tick": int(round(plugin["initialDelaySecs"] * 60)),
                "min_interval": int(round(plugin["minIntervalSecs"] * 60)),
                "max_interval": int(round(plugin["maxIntervalSecs"] * 60)),
                "instance_limit": int(plugin["instanceLimit"]),
                "capture_group_size": int(plugin["captureGroupSize"]),
                "points": [{
                    "dimensions": [round(width, 4), round(height, 4)],
                    # Capture is counted in generator updates.
                    "capture_ticks": max(
                        1, int(round(point["captureSecs"] * 30 / frames_per_tick))),
                    # Hold scoring is counted in 60 Hz engine ticks.
                    "score_ticks": int(round(point.get("holdScoreSecs", 0) * 60)),
                    "capture_score": int(point.get("captureScore", 0)),
                    "hold_score": int(point.get("holdScore", 0)),
                }],
                "entries": entries,
            }
        elif plugin.get("def") == "drop_gen":
            if 116 not in plugin.get("drops", []):
                continue
            for entry in plugin.get("entries", []):
                x, y = transform(entry[0], entry[1])
                drop_spawns.append({"drop": "crystal", "at": [x, y]})
        elif plugin.get("def") == "drop_depot":
            if 116 not in plugin.get("acceptedDrops", []):
                continue
            x0, y0 = transform(plugin["bounds"][0], plugin["bounds"][1])
            x1, y1 = transform(plugin["bounds"][2], plugin["bounds"][3])
            rect = [
                round(min(x0, x1), 4),
                round(min(y0, y1), 4),
                round(abs(x1 - x0), 4),
                round(abs(y1 - y0), 4),
            ]
            drop_depots.append({
                "team": 0,
                "rect": rect,
                "accepts": ["crystal"],
                "deliver_ticks": int(round(plugin.get("framesToDrop", 120) * 2)),
            })

    all_x = [box[0] for box in solids] + [box[0] + box[2] for box in solids]
    all_y = [box[1] for box in solids] + [box[1] + box[3] for box in solids]
    all_x += [rect[0] for rect in platform_rects]
    all_x += [rect[0] + rect[2] for rect in platform_rects]
    all_y += [rect[1] for rect in platform_rects]
    all_y += [rect[1] + rect[3] for rect in platform_rects]
    all_x += [spawn[0] for spawn in spawns]
    all_y += [spawn[1] for spawn in spawns]
    all_x += [spawn["at"][0] for spawn in drop_spawns]
    all_y += [spawn["at"][1] for spawn in drop_spawns]
    for depot in drop_depots:
        rect = depot["rect"]
        all_x += [rect[0], rect[0] + rect[2]]
        all_y += [rect[1], rect[1] + rect[3]]
    if control_points is not None:
        half_w = control_points["points"][0]["dimensions"][0] * 0.5
        half_h = control_points["points"][0]["dimensions"][1] * 0.5
        for entry in control_points["entries"]:
            all_x += [entry[0] - half_w, entry[0] + half_w]
            all_y += [entry[1] - half_h, entry[1] + half_h]
    min_x, min_y = min(all_x), min(all_y)
    max_x, max_y = max(all_x), max(all_y)
    bounds = [
        round(min_x, 4),
        round(min_y, 4),
        round(max_x - min_x, 4),
        round(max_y - min_y, 4),
    ]

    data = {
        "id": map_id,
        "bounds": bounds,
        "borders": border_lines,
        "solids": solids,
        "platforms": platform_rects,
        "platform_shapes": platform_shapes,
        "platform_hatchway": platform_hatchway,
        "jump_pads": jump_pads,
        "spawns": spawns,
        "drop_spawns": drop_spawns,
        "drop_depots": drop_depots,
        "supply_spawners": supply_spawners,
    }
    if control_points is not None:
        data["control_points"] = control_points
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        default="reference/nsync/extracted",
        help="directory containing World*.bytes",
    )
    parser.add_argument("--output-dir", default="assets/maps")
    parser.add_argument(
        "--maps",
        default="crystal_01=World2,crystal_02=World7",
        help="comma-separated map_id=WorldName entries",
    )
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for item in args.maps.split(","):
        map_id, world_name = item.split("=", 1)
        source = input_dir / f"{world_name}.bytes"
        world = json.loads(source.read_text(encoding="utf-8-sig"))
        if isinstance(world, list):
            world = world[0]
        data = build_map(world, map_id)
        output = output_dir / f"{map_id}.json"
        with output.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        print(
            f"{map_id}: borders={len(data['solids'])} "
            f"platforms={len(data['platforms'])} "
            f"jump_pads={len(data['jump_pads'])} "
            f"crystals={len(data['drop_spawns'])} "
            f"depots={len(data['drop_depots'])} "
            f"bounds={data['bounds']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
