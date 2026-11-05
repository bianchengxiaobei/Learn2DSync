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
    platform_rects: list[list[float]] = []
    platform_hatchway: list[bool] = []
    jump_pads: list[dict] = []
    for platform in platforms:
        rect, hatchway, jump_velocity = platform_rect(platform)
        platform_rects.append(rect)
        platform_hatchway.append(hatchway)
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

    objective = None
    drop_spawns: list[dict] = []
    drop_depots: list[dict] = []
    for plugin in world["plugins"]:
        if plugin.get("def") == "cp_gen":
            point = plugin["points"][0]
            entry = plugin["entries"][0]
            center_x, center_y = transform(entry[0], entry[1])
            width, height = point["dimensions"]
            objective = [
                round(center_x - width * 0.5, 4),
                round(center_y - height * 0.5, 4),
                round(width, 4),
                round(height, 4),
            ]
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
    if objective is not None:
        all_x += [objective[0], objective[0] + objective[2]]
        all_y += [objective[1], objective[1] + objective[3]]
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
        "solids": solids,
        "platforms": platform_rects,
        "platform_hatchway": platform_hatchway,
        "jump_pads": jump_pads,
        "spawns": spawns,
        "drop_spawns": drop_spawns,
        "drop_depots": drop_depots,
    }
    if objective is not None:
        data["objective"] = objective
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
