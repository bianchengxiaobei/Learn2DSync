#!/usr/bin/env python3
"""Import original GESupplyGenerator plugins into runtime map JSON.

The original PvP worlds store supply generators under ``plugins`` with
``def == "supply_gen"``::

    {"def": "supply_gen", "initialDelaySecs": 15,
     "minIntervalSecs": 40, "maxIntervalSecs": 60,
     "supplies": [102], "entries": [[15.991, 10.039]]}

Artifact 102 is the big health box (+20% hp and health_pump), artifact 105 is
the double-damage buff box (+100% damage for 10 s).  Entries use the same
World -> game transform as the other importers (x - 2.2, 43.6 - y); original
frames run at 30 Hz while the runtime is 60 Hz, so intervals are multiplied
by 60 as seconds -> ticks.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ORIGIN_X = 2.2
ORIGIN_Y = 43.6

MAP_SOURCES = {
    "world1": "World1",
    "world3": "World3",
    "world5": "World5",
    "world6": "World6",
    "world8": "World8",
    "world9": "World9",
    "world10": "World10",
    "world_flag_capture": "WorldFlagCapture",
    "crystal_01": "World2",
    "crystal_02": "World7",
}

SUPPLY_IDS = {
    102: "health_box",
    105: "double_damage",
}


def transform(x: float, y: float) -> tuple[float, float]:
    return (round(x - ORIGIN_X, 4), round(ORIGIN_Y - y, 4))


def supply_spawners(world: dict) -> list[dict]:
    spawners: list[dict] = []
    for plugin in world.get("plugins", []):
        if plugin.get("def") != "supply_gen":
            continue
        supplies = [
            SUPPLY_IDS[item]
            for item in plugin.get("supplies", [])
            if item in SUPPLY_IDS
        ]
        if not supplies:
            continue
        entries = [
            list(transform(entry[0], entry[1]))
            for entry in plugin.get("entries", [])
        ]
        if not entries:
            continue
        spawners.append({
            "supplies": supplies,
            "entries": entries,
            "start_tick": max(1, int(round(plugin.get("initialDelaySecs", 0) * 60))),
            "min_interval": int(round(plugin.get("minIntervalSecs", 0) * 60)),
            "max_interval": int(round(plugin.get("maxIntervalSecs", 0) * 60)),
        })
    return spawners


def expand_bounds(bounds: list[float], spawners: list[dict]) -> list[float]:
    min_x, min_y = bounds[0], bounds[1]
    max_x = bounds[0] + bounds[2]
    max_y = bounds[1] + bounds[3]
    for spawner in spawners:
        for x, y in spawner["entries"]:
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
    return [
        round(min_x, 4),
        round(min_y, 4),
        round(max_x - min_x, 4),
        round(max_y - min_y, 4),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        default="reference/onraid/extracted",
        help="directory containing World*.bytes",
    )
    parser.add_argument("--output-dir", default="assets/maps")
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    for map_id, world_name in MAP_SOURCES.items():
        source = input_dir / f"{world_name}.bytes"
        if not source.exists():
            raise SystemExit(f"missing original world: {source}")
        world = json.loads(source.read_text(encoding="utf-8-sig"))
        if isinstance(world, list):
            world = world[0]

        target = output_dir / f"{map_id}.json"
        data = json.loads(target.read_text(encoding="utf-8"))
        spawners = supply_spawners(world)
        if not spawners:
            print(f"{map_id}: no supply generators")
            continue
        data["supply_spawners"] = spawners
        data["bounds"] = expand_bounds(data["bounds"], spawners)
        with target.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        total = sum(len(item["entries"]) for item in spawners)
        print(f"{map_id}: supply_spawners={len(spawners)} entries={total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
