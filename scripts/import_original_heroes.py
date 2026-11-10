#!/usr/bin/env python3
"""Build the original-hero rig and part atlas from the decompiled game assets.

This is a development-time tool. It does not run inside the client. The output is
data-driven: a packed RGBA atlas, an atlas JSON compatible with core::Atlas, and a
rig JSON consumed by the client. The source assets remain outside the runtime.

Default input is the AssetRipper export provided for this project and the existing
reference/onraid/extracted/sprites directory. Override the paths when needed:

    py -3 scripts/import_original_heroes.py \
        --assets-root "E:/path/to/Assets" \
        --sprite-root "reference/onraid/extracted/sprites" \
        --out-dir "assets/generated/original_heroes" \
        --rig-out "assets/data/hero_rigs.json"
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - environment error path
    raise SystemExit("Pillow is required: py -3 -m pip install pillow") from exc


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ASSETS_ROOT = Path(
    r"E:\迅雷下载\AssetRipper_win_x64\t4t4t"
    r"\AssetRipper_export_20261030_112341\Assets"
)
DEFAULT_SPRITE_ROOT = REPO_ROOT / "reference" / "onraid" / "extracted" / "sprites"
DEFAULT_OUT_DIR = REPO_ROOT / "assets" / "generated" / "original_heroes"
DEFAULT_RIG_OUT = REPO_ROOT / "assets" / "data" / "hero_rigs.json"


@dataclass(frozen=True)
class HeroSpec:
    hero_id: str
    folder: str
    prefix: str
    prefab: str
    head_sprite: str
    body_sprite: str
    shoulder_back_sprite: str
    shoulder_front_sprite: str
    hand_back_sprite: str
    hand_front_sprite: str
    foot_sprite: str


HEROES = (
    HeroSpec(
        hero_id="scout",
        folder="scout",
        prefix="Scout",
        prefab="ScoutPref",
        head_sprite="ScoutHat1",
        body_sprite="ScoutBody1",
        shoulder_back_sprite="ScoutShoulderBack1",
        shoulder_front_sprite="ScoutShoulderFront1",
        hand_back_sprite="ScoutHandBack1",
        hand_front_sprite="ScoutHandFront1",
        foot_sprite="ScoutFoot1",
    ),
    HeroSpec(
        hero_id="gunner",
        folder="machinegunner",
        prefix="MachineGunner",
        prefab="MachineGunnerPref",
        head_sprite="MachineGunnerHat1",
        body_sprite="MachineGunnerBody1",
        shoulder_back_sprite="MachineGunnerShoulderBack1",
        shoulder_front_sprite="MachineGunnerShoulderFront1",
        hand_back_sprite="MachineGunnerHandBack1",
        hand_front_sprite="MachineGunnerHandFront1",
        foot_sprite="MachineGunnerFoot1",
    ),
    HeroSpec(
        hero_id="assault",
        folder="assault",
        prefix="Assault",
        prefab="AssaultPref",
        head_sprite="AssaultHead1",
        body_sprite="AssaultBody1",
        shoulder_back_sprite="AssaultShoulderBack1",
        shoulder_front_sprite="AssaultShoulderFront1",
        hand_back_sprite="AssaultHandBack1",
        hand_front_sprite="AssaultHandFront1",
        foot_sprite="AssaultFoot1",
    ),
    HeroSpec(
        hero_id="grenadier",
        folder="grenadier",
        prefix="Grenadier",
        prefab="GrenadierPref",
        head_sprite="GrenadierHat1",
        body_sprite="GrenadierBody1",
        shoulder_back_sprite="GrenadierShoulderBack1",
        shoulder_front_sprite="GrenadierShoulderFront1",
        hand_back_sprite="GrenadierHandBack1",
        hand_front_sprite="GrenadierHandFront1",
        foot_sprite="GrenadierFootBack1",
    ),
    HeroSpec(
        hero_id="cyborg",
        folder="cyborg",
        prefix="Cyborg",
        prefab="CyborgPref",
        head_sprite="CyborgHead1",
        body_sprite="CyborgBody1",
        shoulder_back_sprite="CyborgShoulderBack1",
        shoulder_front_sprite="CyborgShoulderFront1",
        hand_back_sprite="CyborgHandBack1",
        hand_front_sprite="CyborgHandFront1",
        foot_sprite="CyborgFoot1",
    ),
    HeroSpec(
        hero_id="hermit",
        folder="hermit",
        prefix="Hermit",
        prefab="HermitPref",
        head_sprite="HermitHead1",
        body_sprite="HermitBody1",
        shoulder_back_sprite="HermitShoulderBack1",
        shoulder_front_sprite="HermitShoulderFront1",
        hand_back_sprite="HermitHandBack1",
        hand_front_sprite="HermitHandFront1",
        foot_sprite="HermitFoot1",
    ),
    HeroSpec(
        hero_id="predator",
        folder="predator",
        prefix="Predator",
        prefab="PredatorPref",
        head_sprite="PredatorHead1",
        body_sprite="PredatorBody1",
        shoulder_back_sprite="PredatorShoulderBack2",
        shoulder_front_sprite="PredatorShoulderFront1",
        hand_back_sprite="PredatorHandBack1",
        hand_front_sprite="PredatorHandFront1",
        foot_sprite="PredatorFoot1",
    ),
    HeroSpec(
        hero_id="pyromancer",
        folder="pyromancer",
        prefix="Pyromancer",
        prefab="PyromancerPref",
        head_sprite="PyromancerHead1",
        body_sprite="PyromancerBody1",
        shoulder_back_sprite="PyromancerShoulderBack1",
        shoulder_front_sprite="PyromancerShoulderFront1",
        hand_back_sprite="PyromancerHandBack1",
        hand_front_sprite="PyromancerHandFront1",
        foot_sprite="PyromancerFoot1",
    ),
    HeroSpec(
        hero_id="soldier",
        folder="soldier",
        prefix="Soldier",
        prefab="SoldierPref",
        head_sprite="SoldierHead1",
        body_sprite="SoldierBody1",
        shoulder_back_sprite="SoldierShoulderBack1",
        shoulder_front_sprite="SoldierShoulderFront1",
        hand_back_sprite="SoldierHandBack1",
        hand_front_sprite="SoldierHandFront1",
        foot_sprite="SoldierFoot1",
    ),
    HeroSpec(
        hero_id="warrioress",
        folder="warrioress",
        prefix="Warrioress",
        prefab="WarrioressPref",
        head_sprite="WarrioressHead1",
        body_sprite="WarrioressBody1",
        shoulder_back_sprite="WarrioressShoulderBack1",
        shoulder_front_sprite="WarrioressShoulderFront1",
        hand_back_sprite="WarrioressHandBack1",
        hand_front_sprite="WarrioressHandFront1",
        foot_sprite="WarrioressFoot1",
    ),
)

WEAPON_SPRITES = {
    "rifle": "SoldierRifle1",
    "shotgun": "shotgun1",
    "rocket": "rocketLauncher1",
    "smg": "minigun1",
    "burst": "LaserGun1",
    "grenade": "grenade1",
}

# Per-hero extra weapons. Warrioress uses a melee axe by default; the extracted
# sprite set has no axe1.png, so use the closest axe variant.
HERO_WEAPON_OVERRIDES = {
    "warrioress": {"axe": "axe7"},
}

# Some weapons have their own attach node in the hero prefab (with a grip
# rotation), instead of using the generic gun attach.
WEAPON_ATTACH_SUFFIXES = {
    "axe": "allWeaponAnim/axeAnim/pirateAxe",
}

# Part role, sprite, sorting order. Sorting orders follow the original prefab's
# SpriteRenderer values: feet 0, back shoulder 2, body 4, front shoulder 10,
# head 11, back hand 21, weapon 22, front hand 23.
PART_LAYOUT = (
    ("foot_back", "foot", 0),
    ("shoulder_back", "shoulder_back", 2),
    ("body", "body", 4),
    ("shoulder_front", "shoulder_front", 10),
    ("head", "head", 11),
    ("weapon", "hand_back", 21),
    ("weapon", "hand_front", 23),
    ("foot_front", "foot", 0),
)

# SpriteRenderer node paths inside the prefab. Foot names are deliberately
# swapped in the original prefab: footBackAnim contains footFrontSprite, and
# footFrontAnim contains footBackSprite. The original prefab faces left, so the
# negative-X foot is the physical front foot.
NODE_PATHS = {
    "body": "bodyAnim/bodySprite",
    "head": "headAnim/headPivot/headSprite",
    "shoulder_back": "shoulderBackAnim/shoulderBackSprite",
    "shoulder_front": "shoulderFrontAnim/shoulderFrontSprite",
    "hand_back": "handBackAnim/handBackSprite",
    "hand_front": "handFrontAnim/handFrontSprite",
    "foot_back": "footFrontAnim/footBackSprite",
    "foot_front": "footBackAnim/footFrontSprite",
}

WEAPON_NODE_CANDIDATES = (
    "allWeaponAnim/weaponAnim",
    "allWeaponAnim/shotgunAnim",
    "allWeaponAnim/minigunAnim",
    "allWeaponAnim/weaponHandAnim",
    "pivot/allWeaponAnim",
)

TARGET_HEIGHT = 1.8
GROUND_Y = -0.9


def load_glb_json(path: Path) -> dict:
    data = path.read_bytes()
    magic, version, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError(f"not a GLB 2.0 file: {path}")
    offset = 12
    chunks: list[tuple[int, bytes]] = []
    while offset < len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunks.append((chunk_type, data[offset : offset + chunk_length]))
        offset += chunk_length
    if not chunks or chunks[0][0] != 0x4E4F534A:
        raise ValueError(f"GLB has no JSON chunk: {path}")
    return json.loads(chunks[0][1].decode("utf-8").rstrip("\x00 "))


def quaternion_angle(rotation: Iterable[float] | None) -> float:
    if rotation is None:
        return 0.0
    x, y, z, w = rotation
    # Character transforms are 2D; ignore X/Y quaternion components, but keep
    # the same convention as the Unity Z rotation.
    _ = x, y
    return 2.0 * math.atan2(z, w)


def node_matrix(node: dict) -> tuple[float, float, float, float, float, float]:
    tx, ty, _tz = (node.get("translation") or (0.0, 0.0, 0.0))[:3]
    sx, sy, _sz = (node.get("scale") or (1.0, 1.0, 1.0))[:3]
    angle = quaternion_angle(node.get("rotation"))
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return (
        cosine * sx,
        sine * sx,
        -sine * sy,
        cosine * sy,
        tx,
        ty,
    )


def matrix_mul(
    a: tuple[float, float, float, float, float, float],
    b: tuple[float, float, float, float, float, float],
) -> tuple[float, float, float, float, float, float]:
    return (
        a[0] * b[0] + a[2] * b[1],
        a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3],
        a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4],
        a[1] * b[4] + a[3] * b[5] + a[5],
    )


def transform_point(
    matrix: tuple[float, float, float, float, float, float],
    point: tuple[float, float],
) -> tuple[float, float]:
    return (
        matrix[0] * point[0] + matrix[2] * point[1] + matrix[4],
        matrix[1] * point[0] + matrix[3] * point[1] + matrix[5],
    )


def find_node_matrices(glb: dict) -> dict[str, tuple[float, float, float, float, float, float]]:
    node_matrices: dict[str, tuple[float, float, float, float, float, float]] = {}

    def walk(
        index: int,
        parent: tuple[float, float, float, float, float, float],
        path: str,
    ) -> None:
        node = glb["nodes"][index]
        matrix = matrix_mul(parent, node_matrix(node))
        name = node.get("name", "")
        node_path = f"{path}/{name}" if path else name
        if name:
            node_matrices[node_path] = matrix
        for child in node.get("children", ()):
            walk(child, matrix, node_path)

    walk(0, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0), "")
    return node_matrices


def find_suffix(
    matrices: dict[str, tuple[float, float, float, float, float, float]],
    suffix: str,
) -> tuple[float, float, float, float, float, float]:
    matches = [matrix for path, matrix in matrices.items() if path.endswith("/" + suffix)]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one node ending in {suffix!r}, got {len(matches)}")
    return matches[0]


def read_sprite_json(path: Path) -> dict:
    if not path.is_file():
        raise FileNotFoundError(f"missing Sprite metadata: {path}")
    with path.open("r", encoding="utf-8") as handle:
        sprite = json.load(handle)
    render_data = sprite.get("m_RD") or {}
    vertices = render_data.get("m_Vertices") or []
    if len(vertices) != 4:
        raise ValueError(f"{path.name} has {len(vertices)} vertices, expected 4")
    points = [
        (float(vertex["m_Pos"]["m_X"]), float(vertex["m_Pos"]["m_Y"]))
        for vertex in vertices
    ]
    return {"points": points, "ppu": float(sprite.get("m_PixelsToUnits", 100.0))}


def ordered_quad(
    points: list[tuple[float, float]],
) -> tuple[list[tuple[float, float]], list[tuple[float, float]]]:
    # Sprite vertices are axis-aligned. Reorder them TL, TR, BR, BL so the
    # renderer can emit two triangles without carrying Unity's index list.
    tl = max(points, key=lambda point: (point[1], -point[0]))
    tr = max(points, key=lambda point: (point[1], point[0]))
    br = min(points, key=lambda point: (point[1], -point[0]))
    bl = min(points, key=lambda point: (point[1], point[0]))
    corners = [tl, tr, br, bl]
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    return corners, uvs


def build_hero_parts(
    spec: HeroSpec,
    assets_root: Path,
    matrices: dict[str, tuple[float, float, float, float, float, float]],
) -> list[dict]:
    parts: list[dict] = []
    for role, sprite_slot, z_order in PART_LAYOUT:
        node_key = role if sprite_slot == "foot" else sprite_slot
        node_suffix = NODE_PATHS[node_key]
        matrix = find_suffix(matrices, node_suffix)
        sprite_name = {
            "body": spec.body_sprite,
            "head": spec.head_sprite,
            "shoulder_back": spec.shoulder_back_sprite,
            "shoulder_front": spec.shoulder_front_sprite,
            "hand_back": spec.hand_back_sprite,
            "hand_front": spec.hand_front_sprite,
            "foot": spec.foot_sprite,
        }[sprite_slot]
        sprite = read_sprite_json(assets_root / "Resources" / f"{sprite_name}.json")
        corners, uvs = ordered_quad(sprite["points"])
        transformed = [transform_point(matrix, point) for point in corners]
        parts.append(
            {
                "role": role,
                "sprite": f"{spec.hero_id}_{sprite_slot}",
                "z": z_order,
                "pivot": list(transform_point(matrix, (0.0, 0.0))),
                "quad": [list(point) for point in transformed],
                "uv": [list(uv) for uv in uvs],
                "_sprite_name": sprite_name,
            }
        )
    return parts


def build_weapons(
    assets_root: Path,
    matrices: dict[str, tuple[float, float, float, float, float, float]],
    weapon_sprites: dict[str, str],
) -> list[dict]:
    matrix = None
    for suffix in WEAPON_NODE_CANDIDATES:
        matches = [value for path, value in matrices.items() if path.endswith("/" + suffix)]
        if len(matches) == 1:
            matrix = matches[0]
            break
    if matrix is None:
        raise ValueError("no common weapon attach node found")
    weapons: list[dict] = []
    for weapon_id, sprite_name in weapon_sprites.items():
        weapon_matrix = matrix
        attach_suffix = WEAPON_ATTACH_SUFFIXES.get(weapon_id)
        if attach_suffix:
            matches = [
                value
                for path, value in matrices.items()
                if path.endswith("/" + attach_suffix)
            ]
            if len(matches) == 1:
                weapon_matrix = matches[0]
        sprite = read_sprite_json(assets_root / "Resources" / f"{sprite_name}.json")
        corners, uvs = ordered_quad(sprite["points"])
        transformed = [transform_point(weapon_matrix, point) for point in corners]
        weapons.append(
            {
                "id": weapon_id,
                "sprite": f"weapon_{weapon_id}",
                "z": 22,
                "pivot": list(transform_point(weapon_matrix, (0.0, 0.0))),
                "quad": [list(point) for point in transformed],
                "uv": [list(uv) for uv in uvs],
                "_sprite_name": sprite_name,
            }
        )
    return weapons


def normalize_rig(
    parts: list[dict],
    weapons: list[dict],
) -> tuple[list[dict], list[dict]]:
    character_points = [point for part in parts for point in part["quad"]]
    min_x = min(point[0] for point in character_points)
    max_x = max(point[0] for point in character_points)
    min_y = min(point[1] for point in character_points)
    max_y = max(point[1] for point in character_points)
    if max_y - min_y < 1e-4:
        raise ValueError("hero has no vertical extent")
    scale = TARGET_HEIGHT / (max_y - min_y)
    offset_x = -(min_x + max_x) * 0.5 * scale
    offset_y = GROUND_Y - min_y * scale

    def normalize_point(point: list[float]) -> list[float]:
        return [
            round(point[0] * scale + offset_x, 6),
            round(point[1] * scale + offset_y, 6),
        ]

    for collection in (parts, weapons):
        for item in collection:
            item["quad"] = [normalize_point(point) for point in item["quad"]]
            item["pivot"] = normalize_point(item["pivot"])
    return parts, weapons


def load_source_image(sprite_root: Path, sprite_name: str) -> Image.Image:
    path = sprite_root / f"{sprite_name}.png"
    if not path.is_file():
        raise FileNotFoundError(f"missing sprite PNG: {path}")
    image = Image.open(path).convert("RGBA")
    if image.width == 0 or image.height == 0:
        raise ValueError(f"empty sprite PNG: {path}")
    return image


def pack_atlas(
    frames: dict[str, Image.Image],
    out_png: Path,
    out_json: Path,
    image_virtual_path: str,
    max_width: int = 512,
    padding: int = 2,
) -> None:
    ordered = sorted(frames.items(), key=lambda item: (-item[1].height, item[0]))
    positions: dict[str, tuple[int, int]] = {}
    x = 0
    y = 0
    row_height = 0
    for name, image in ordered:
        if image.width > max_width:
            raise ValueError(f"{name} is wider than the atlas: {image.width} > {max_width}")
        if x + image.width > max_width:
            x = 0
            y += row_height + padding
            row_height = 0
        positions[name] = (x, y)
        x += image.width + padding
        row_height = max(row_height, image.height)
    atlas_height = y + row_height
    if atlas_height <= 0 or atlas_height > 4096:
        raise ValueError(f"invalid atlas height: {atlas_height}")

    atlas = Image.new("RGBA", (max_width, atlas_height), (0, 0, 0, 0))
    atlas_frames = []
    for name, image in frames.items():
        px, py = positions[name]
        atlas.alpha_composite(image, (px, py))
        atlas_frames.append(
            {
                "name": name,
                "rect": [px, py, image.width, image.height],
                "pivot": [0.5, 0.5],
            }
        )

    out_png.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(out_png)
    atlas_json = {
        "version": 1,
        "image": image_virtual_path,
        "width": max_width,
        "height": atlas_height,
        "frames": atlas_frames,
    }
    out_json.write_text(
        json.dumps(atlas_json, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-root", type=Path, default=DEFAULT_ASSETS_ROOT)
    parser.add_argument("--sprite-root", type=Path, default=DEFAULT_SPRITE_ROOT)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--rig-out", type=Path, default=DEFAULT_RIG_OUT)
    parser.add_argument("--max-width", type=int, default=512)
    args = parser.parse_args()

    assets_root = args.assets_root.resolve()
    sprite_root = args.sprite_root.resolve()
    out_dir = args.out_dir.resolve()
    rig_out = args.rig_out.resolve()

    if not assets_root.is_dir():
        raise SystemExit(f"assets root does not exist: {assets_root}")
    if not sprite_root.is_dir():
        raise SystemExit(f"sprite root does not exist: {sprite_root}")

    all_parts: dict[str, dict] = {}
    all_frames: dict[str, Image.Image] = {}
    heroes_payload: list[dict] = []

    for spec in HEROES:
        char_root = assets_root / "Resources" / "heroprefabs" / "characters" / spec.folder
        prefab_path = char_root / f"{spec.prefab}.glb"
        if not prefab_path.is_file():
            raise SystemExit(f"missing prefab: {prefab_path}")
        glb = load_glb_json(prefab_path)
        matrices = find_node_matrices(glb)

        parts = build_hero_parts(spec, assets_root, matrices)
        weapon_sprites = dict(WEAPON_SPRITES)
        weapon_sprites.update(HERO_WEAPON_OVERRIDES.get(spec.hero_id, {}))
        weapons = build_weapons(assets_root, matrices, weapon_sprites)
        parts, weapons = normalize_rig(parts, weapons)

        for part in parts:
            sprite_name = part.pop("_sprite_name")
            all_frames.setdefault(part["sprite"], load_source_image(sprite_root, sprite_name))
        for weapon in weapons:
            sprite_name = weapon.pop("_sprite_name")
            all_frames.setdefault(weapon["sprite"], load_source_image(sprite_root, sprite_name))

        all_parts[spec.hero_id] = {
            "hero": spec.hero_id,
            "root_pivot": [0.0, GROUND_Y],
            "parts": parts,
            "weapons": weapons,
        }
        heroes_payload.append(all_parts[spec.hero_id])

    image_virtual_path = "generated/original_heroes/heroes.png"
    atlas_virtual_path = "generated/original_heroes/heroes.atlas.json"
    pack_atlas(
        all_frames,
        out_dir / "heroes.png",
        out_dir / "heroes.atlas.json",
        image_virtual_path,
        max_width=args.max_width,
    )

    rig_payload = {
        "version": 1,
        "atlas": atlas_virtual_path,
        "image": image_virtual_path,
        "height": TARGET_HEIGHT,
        "heroes": heroes_payload,
    }
    rig_out.parent.mkdir(parents=True, exist_ok=True)
    rig_out.write_text(
        json.dumps(rig_payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    print(f"wrote {out_dir / 'heroes.png'}")
    print(f"wrote {out_dir / 'heroes.atlas.json'}")
    print(f"wrote {rig_out}")
    print(f"heroes={len(heroes_payload)} frames={len(all_frames)}")
    for hero in heroes_payload:
        print(
            f"  {hero['hero']}: parts={len(hero['parts'])} "
            f"weapons={len(hero['weapons'])}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
