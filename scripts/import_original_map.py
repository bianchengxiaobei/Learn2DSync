#!/usr/bin/env python3
"""Export the baked visual layer of an original NSYNC world bundle.

The original game stores each PvP world's decoration/background SpriteRenderers
in AssetBundles/worlds/<world>.  This development-only tool bakes those sprites
into a single atlas plus a small runtime JSON understood by WorldRenderer.
Sprites are sorted by Unity's sorting layer (from TagManager) followed by
m_SortingOrder, so a Background sprite cannot cover a Default-layer platform.

Requires UnityPy (tested with 1.10.18) and Pillow.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

try:
    import UnityPy
except ImportError as error:  # pragma: no cover - developer tool
    raise SystemExit("UnityPy is required: py -3 -m pip install UnityPy==1.10.18") from error

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - developer tool
    raise SystemExit("Pillow is required: py -3 -m pip install pillow") from error


DEFAULT_GAME_DIR = r"E:\Soft\Steam\steamapps\common\NSYNC\NSYNC_Data"
PADDING = 2
EXTRUDE = 1

SCENE_NAMES = {
    "world1": "World1",
    "world2": "World2",
    "world3": "World3",
    "world5": "World5",
    "world6": "World6",
    "world7": "World7",
    "world8": "World8",
    "world9": "World9",
    "world10": "World10",
    "worldflagcapture": "WorldFlagCapture",
    "tutorialworld": "TutorialWorld",
}


@dataclass
class Parallax:
    factor: float = 0.0


@dataclass
class Matrix:
    values: List[List[float]]

    @staticmethod
    def identity() -> "Matrix":
        return Matrix([[1.0, 0.0, 0.0, 0.0],
                       [0.0, 1.0, 0.0, 0.0],
                       [0.0, 0.0, 1.0, 0.0],
                       [0.0, 0.0, 0.0, 1.0]])

    def multiply(self, other: "Matrix") -> "Matrix":
        a = self.values
        b = other.values
        return Matrix([
            [sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)
        ])

    def transform(self, x: float, y: float) -> Tuple[float, float]:
        m = self.values
        return (m[0][0] * x + m[0][1] * y + m[0][3],
                m[1][0] * x + m[1][1] * y + m[1][3])


def local_matrix(transform) -> Matrix:
    q = transform.m_LocalRotation
    x, y, z, w = q.X, q.Y, q.Z, q.W
    r00 = 1.0 - 2.0 * (y * y + z * z)
    r01 = 2.0 * (x * y - z * w)
    r02 = 2.0 * (x * z + y * w)
    r10 = 2.0 * (x * y + z * w)
    r11 = 1.0 - 2.0 * (x * x + z * z)
    r12 = 2.0 * (y * z - x * w)
    scale = transform.m_LocalScale
    position = transform.m_LocalPosition
    return Matrix([
        [r00 * scale.X, r01 * scale.Y, r02 * scale.Z, position.X],
        [r10 * scale.X, r11 * scale.Y, r12 * scale.Z, position.Y],
        [0.0, 0.0, 0.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ])


def world_matrix(transform) -> Matrix:
    matrix = local_matrix(transform)
    father = transform.m_Father
    depth = 0
    while father is not None and father.path_id != 0 and depth < 128:
        parent = father.read()
        if parent is None:
            break
        matrix = local_matrix(parent).multiply(matrix)
        father = parent.m_Father
        depth += 1
    return matrix


def manifest_dependencies(bundle_path: str) -> List[str]:
    manifest_path = bundle_path + ".manifest"
    if not os.path.exists(manifest_path):
        return []
    text = open(manifest_path, encoding="utf-8", errors="replace").read()
    match = re.search(r"^Dependencies:\s*$", text, re.MULTILINE)
    if not match:
        return []
    deps: List[str] = []
    for line in text[match.end():].splitlines():
        line = line.strip()
        if not line.startswith("- "):
            if line:
                break
            continue
        path = line[2:].strip()
        marker = "/AssetBundles/"
        if marker in path:
            path = path.split(marker, 1)[1]
        deps.append(path)
    return deps


def find_dependency_file(game_dir: str, dependency: str) -> Optional[str]:
    base = os.path.join(game_dir, "AssetBundles")
    path = os.path.join(base, *dependency.replace("\\", "/").split("/"))
    if os.path.exists(path):
        return path
    # AssetRipper manifests include a build prefix; try the tail folders.
    parts = dependency.replace("\\", "/").split("/")
    for index in range(len(parts)):
        candidate = os.path.join(base, *parts[index:])
        if os.path.exists(candidate):
            return candidate
    return None


def load_environment(bundle_path: str, game_dir: str):
    dependencies = manifest_dependencies(bundle_path)
    paths = [bundle_path]
    for dependency in dependencies:
        resolved = find_dependency_file(game_dir, dependency)
        if resolved:
            paths.append(resolved)
        else:
            print(f"warning: dependency not found: {dependency}", file=sys.stderr)
    return UnityPy.load(*paths)


def is_valid_ptr(ptr) -> bool:
    return ptr is not None and getattr(ptr, "path_id", 0) != 0


def read_parallax(environment) -> Dict[int, float]:
    result: Dict[int, float] = {}
    for obj in environment.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            data = obj.read()
            script = data.m_Script.read() if is_valid_ptr(data.m_Script) else None
            class_name = getattr(script, "m_ClassName", "") if script else ""
            if class_name != "ParallaxLayer":
                continue
            game_object = data.m_GameObject
            if not is_valid_ptr(game_object):
                continue
            result[game_object.path_id] = float(data.Factor)
        except Exception:
            continue
    return result


def read_sorting_layer_orders(game_dir: str) -> Dict[int, int]:
    """Map Unity sorting-layer IDs to their order from TagManager.

    SpriteRenderer sorting is (layer order, sorting order), not just
    m_SortingOrder.  The original world art relies on Background/Ground/
    Default/Decor layers; ignoring the layer makes background sprites jump
    in front of platforms.
    """
    path = os.path.join(game_dir, "globalgamemanagers")
    if not os.path.exists(path):
        return {}
    try:
        environment = UnityPy.load(path)
    except Exception:
        return {}
    for obj in environment.objects:
        if obj.type.name != "TagManager":
            continue
        try:
            data = obj.read()
            layers = getattr(data, "m_SortingLayers", None) or []
            result: Dict[int, int] = {}
            for index, layer in enumerate(layers):
                unique_id = int(getattr(layer, "uniqueID", 0)) & 0xFFFFFFFF
                result[unique_id] = index
            return result
        except Exception:
            continue
    return {}


def ancestor_parallax(transform, by_go: Dict[int, float]) -> float:
    depth = 0
    current = transform
    while current is not None and depth < 128:
        try:
            game_object = current.m_GameObject
            if is_valid_ptr(game_object):
                factor = by_go.get(game_object.path_id)
                if factor is not None:
                    return factor
            father = current.m_Father
            if father is None or father.path_id == 0:
                break
            current = father.read()
        except Exception:
            break
        depth += 1
    return 0.0


def root_scene_name(game_object) -> str:
    """Return the scene root GameObject name for a GameObject."""
    try:
        transform = game_object.m_Transform.read()
    except Exception:
        return ""
    name = ""
    depth = 0
    while transform is not None and depth < 128:
        try:
            current = transform.m_GameObject
            if not is_valid_ptr(current):
                break
            name = current.read().m_Name or name
            father = transform.m_Father
            if father is None or father.path_id == 0:
                break
            transform = father.read()
        except Exception:
            break
        depth += 1
    return name


def active_in_hierarchy(game_object) -> bool:
    """Match Unity's activeInHierarchy, including inactive scene roots."""
    try:
        transform = game_object.m_Transform.read()
    except Exception:
        return False
    depth = 0
    while transform is not None and depth < 128:
        try:
            current = transform.m_GameObject
            if not is_valid_ptr(current):
                break
            game_object = current.read()
            if not bool(getattr(game_object, "m_IsActive", True)):
                return False
            father = transform.m_Father
            if father is None or father.path_id == 0:
                break
            transform = father.read()
        except Exception:
            return False
        depth += 1
    return True


def find_scene_asset_files(environment, scene_name: str) -> set[str]:
    """Find the serialized file that owns the requested scene.

    World bundles pull in other scenes as dependencies (World2, World3,
    TutorialWorld, WorldFlagCapture...).  Those dependency SpriteRenderers
    must not be baked into the target map, or their props/bridges appear as
    floating art in unrelated arenas.
    """
    target = scene_name.casefold()
    files: set[str] = set()
    for obj in environment.objects:
        if obj.type.name != "SpriteRenderer":
            continue
        try:
            renderer = obj.read()
            game_object = renderer.m_GameObject.read()
            if root_scene_name(game_object).casefold() != target:
                continue
            files.add(getattr(obj.assets_file, "name", ""))
        except Exception:
            continue
    return {name for name in files if name}


def color_bytes(color) -> Tuple[int, int, int, int]:
    def clamp(value: float) -> int:
        return max(0, min(255, int(round(value * 255.0))))

    return (clamp(color.r), clamp(color.g), clamp(color.b), clamp(color.a))


def triangulate(sprite) -> List[Tuple[int, int, int]]:
    vertices = sprite.m_RD.vertices
    indices = list(getattr(sprite.m_RD, "indices", None) or [])
    count = len(vertices)
    if count < 3:
        return []
    if indices and len(indices) % 3 == 0:
        triangles = []
        for i in range(0, len(indices), 3):
            tri = (int(indices[i]), int(indices[i + 1]), int(indices[i + 2]))
            if all(0 <= index < count for index in tri):
                triangles.append(tri)
        if triangles:
            return triangles
    return [(0, i, i + 1) for i in range(1, count - 1)]


@dataclass
class SpriteImage:
    path_id: int
    name: str
    image: Image.Image
    width: int = 0
    height: int = 0
    atlas_x: int = 0
    atlas_y: int = 0

    def __post_init__(self) -> None:
        self.width, self.height = self.image.size


@dataclass
class ArtObject:
    order: int
    layer_order: int
    sorting_order: int
    sequence: int
    parallax: float
    additive: bool
    name: str
    sprite_key: int
    vertices: List[float] = field(default_factory=list)
    triangle_count: int = 0


def collect_sprites(
    environment,
    parallax_by_go: Dict[int, float],
    sorting_layer_orders: Dict[int, int],
    scene_asset_files: set[str],
):
    sprites: Dict[int, SpriteImage] = {}
    objects: List[ArtObject] = []
    skipped = 0

    for obj in environment.objects:
        if obj.type.name != "SpriteRenderer":
            continue
        try:
            if scene_asset_files and getattr(obj.assets_file, "name", "") not in scene_asset_files:
                continue
            renderer = obj.read()
            if not renderer.m_Enabled:
                continue
            if not is_valid_ptr(renderer.m_Sprite):
                skipped += 1
                continue
            game_object = renderer.m_GameObject
            if not is_valid_ptr(game_object):
                skipped += 1
                continue
            game_object = game_object.read()
            if not active_in_hierarchy(game_object):
                continue
            transform_ptr = game_object.m_Transform
            if not is_valid_ptr(transform_ptr):
                skipped += 1
                continue
            transform = transform_ptr.read()
            if transform is None:
                skipped += 1
                continue

            sprite = renderer.m_Sprite.read()
            if sprite is None:
                skipped += 1
                continue
            try:
                image = sprite.image
            except Exception:
                skipped += 1
                continue
            image = image.convert("RGBA")
            if image.width <= 0 or image.height <= 0:
                skipped += 1
                continue

            key = renderer.m_Sprite.path_id
            if key not in sprites:
                sprites[key] = SpriteImage(key, sprite.m_Name, image)

            matrix = world_matrix(transform)
            flip_x = bool(renderer.m_FlipX)
            flip_y = bool(renderer.m_FlipY)
            tint = color_bytes(renderer.m_Color)
            additive = False
            for material_ptr in (renderer.m_Materials or []):
                try:
                    material = material_ptr.read()
                    if getattr(material, "m_Name", "") in (
                            "SpriteLightMaterial", "SpriteAdditive"):
                        additive = True
                        break
                except Exception:
                    continue

            local_points = [(float(v.pos.X), float(v.pos.Y)) for v in sprite.m_RD.vertices]
            if not local_points:
                skipped += 1
                continue
            min_x = min(p[0] for p in local_points)
            max_x = max(p[0] for p in local_points)
            min_y = min(p[1] for p in local_points)
            max_y = max(p[1] for p in local_points)
            span_x = max(1.0e-6, max_x - min_x)
            span_y = max(1.0e-6, max_y - min_y)

            sorting_layer_id = int(renderer.m_SortingLayerID) & 0xFFFFFFFF
            sorting_layer_order = sorting_layer_orders.get(
                sorting_layer_id, sorting_layer_orders.get(0, 0))
            sorting_order = int(renderer.m_SortingOrder)
            # Sorting layer dominates sorting order; 100000 is wider than the
            # signed 16-bit m_SortingOrder range.
            sort_key = sorting_layer_order * 100000 + sorting_order
            art = ArtObject(
                order=sort_key,
                layer_order=sorting_layer_order,
                sorting_order=sorting_order,
                sequence=len(objects),
                parallax=ancestor_parallax(transform, parallax_by_go),
                additive=additive,
                name=game_object.m_Name or sprite.m_Name,
                sprite_key=key,
            )

            for triangle in triangulate(sprite):
                for index in triangle:
                    local_x, local_y = local_points[index]
                    u = (local_x - min_x) / span_x
                    v = (max_y - local_y) / span_y
                    if flip_x:
                        local_x = -local_x
                    if flip_y:
                        local_y = -local_y
                    world_x, world_y = matrix.transform(local_x, local_y)
                    # World1 was authored Y-up; the runtime uses Y-down.
                    game_x = world_x - 2.2
                    game_y = 43.6 - world_y
                    art.vertices.extend([
                        game_x, game_y, u, v,
                        tint[0], tint[1], tint[2], tint[3],
                    ])
                art.triangle_count += 1
            if art.vertices:
                objects.append(art)
        except Exception as error:
            print(f"warning: skipped sprite renderer {obj.path_id}: {error}", file=sys.stderr)
            skipped += 1

    objects.sort(
        key=lambda item: (
            item.order, item.parallax, item.additive, item.sequence))
    return sprites, objects, skipped


def find_camera_bounds(
    environment,
    scene_asset_files: set[str],
) -> Optional[List[float]]:
    for obj in environment.objects:
        if obj.type.name != "BoxCollider2D":
            continue
        try:
            if scene_asset_files and getattr(obj.assets_file, "name", "") not in scene_asset_files:
                continue
            data = obj.read()
            game_object = data.m_GameObject.read()
            if game_object is None or game_object.m_Name != "WorldRect":
                continue
            if not active_in_hierarchy(game_object):
                continue
            size = data.m_Size
            offset = data.m_Offset
            transform = game_object.m_Transform.read()
            matrix = world_matrix(transform)
            half_x = size.x * 0.5
            half_y = size.y * 0.5
            corners = [
                matrix.transform(offset.x - half_x, offset.y - half_y),
                matrix.transform(offset.x + half_x, offset.y - half_y),
                matrix.transform(offset.x - half_x, offset.y + half_y),
                matrix.transform(offset.x + half_x, offset.y + half_y),
            ]
            xs = [corner[0] - 2.2 for corner in corners]
            ys = [43.6 - corner[1] for corner in corners]
            return [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]
        except Exception:
            continue
    return None


def pack_atlas(sprites: Dict[int, SpriteImage], max_width: int) -> Tuple[Image.Image, int, int]:
    ordered = sorted(sprites.values(), key=lambda s: (-s.height, -s.width, s.name))
    rows: List[Tuple[int, int, int, int]] = []
    cursor_x = PADDING
    cursor_y = PADDING
    row_height = 0
    for sprite in ordered:
        if sprite.width + PADDING * 2 > max_width:
            raise SystemExit(f"sprite too wide for atlas: {sprite.name} ({sprite.width}px)")
        if cursor_x + sprite.width + PADDING > max_width:
            cursor_x = PADDING
            cursor_y += row_height + PADDING
            row_height = 0
        sprite.atlas_x = cursor_x
        sprite.atlas_y = cursor_y
        rows.append((sprite.atlas_x, sprite.atlas_y, sprite.width, sprite.height))
        cursor_x += sprite.width + PADDING
        row_height = max(row_height, sprite.height)
    atlas_width = max_width
    atlas_height = cursor_y + row_height + PADDING
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    for sprite in ordered:
        x = sprite.atlas_x
        y = sprite.atlas_y
        atlas.paste(sprite.image, (x, y))
        if EXTRUDE and sprite.width > 0 and sprite.height > 0:
            # Bleed the edge pixels into the padding so linear filtering does
            # not pull in transparent or neighbouring pixels.
            left = sprite.image.crop((0, 0, 1, sprite.height))
            right = sprite.image.crop((sprite.width - 1, 0, sprite.width, sprite.height))
            top = sprite.image.crop((0, 0, sprite.width, 1))
            bottom = sprite.image.crop((0, sprite.height - 1, sprite.width, sprite.height))
            for offset in range(1, EXTRUDE + 1):
                atlas.paste(left, (x - offset, y))
                atlas.paste(right, (x + sprite.width - 1 + offset, y))
                atlas.paste(top, (x, y - offset))
                atlas.paste(bottom, (x, y + sprite.height - 1 + offset))
            corners = [
                (x - EXTRUDE, y - EXTRUDE),
                (x + sprite.width - 1 + EXTRUDE, y - EXTRUDE),
                (x - EXTRUDE, y + sprite.height - 1 + EXTRUDE),
                (x + sprite.width - 1 + EXTRUDE, y + sprite.height - 1 + EXTRUDE),
            ]
            for corner_x, corner_y in corners:
                atlas.putpixel((corner_x, corner_y), sprite.image.getpixel(
                    (0 if corner_x < x else sprite.width - 1,
                     0 if corner_y < y else sprite.height - 1)))
    return atlas, atlas_width, atlas_height


def build_json(
    sprites: Dict[int, SpriteImage],
    objects: List[ArtObject],
    atlas_width: int,
    atlas_height: int,
    image_path: str,
    atlas_path: str,
    camera_bounds: Optional[List[float]],
) -> dict:
    layers: List[dict] = []
    current_key: Optional[Tuple[int, float, bool]] = None
    current: Optional[dict] = None
    for art in objects:
        key = (art.order, round(art.parallax, 6), art.additive)
        if key != current_key:
            current = {
                "order": art.order,
                "layer": art.layer_order,
                "sorting_order": art.sorting_order,
                "parallax": round(art.parallax, 6),
                "additive": art.additive,
                "vertices": [],
            }
            layers.append(current)
            current_key = key
        sprite = sprites[art.sprite_key]
        # Rewrite normalized sprite UVs into atlas UVs.
        for index in range(0, len(art.vertices), 8):
            u = art.vertices[index + 2]
            v = art.vertices[index + 3]
            art.vertices[index + 2] = (sprite.atlas_x + u * sprite.width) / atlas_width
            art.vertices[index + 3] = (sprite.atlas_y + v * sprite.height) / atlas_height
        current["vertices"].extend(art.vertices)

    result = {
        "version": 1,
        "image": image_path,
        "atlas": atlas_path,
        "image_width": atlas_width,
        "image_height": atlas_height,
        "layers": layers,
    }
    if camera_bounds:
        result["camera_bounds"] = [round(value, 4) for value in camera_bounds]
    return result


def write_json(path: str, value: dict) -> None:
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, ensure_ascii=False, separators=(",", ":"))
        output.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", default=DEFAULT_GAME_DIR)
    parser.add_argument("--bundle", default="worlds/world1")
    parser.add_argument("--map-id", default="world1")
    parser.add_argument("--scene-name", default=None)
    parser.add_argument("--out-dir", default="assets/generated/original_map")
    parser.add_argument("--out-json", default=None)
    parser.add_argument("--max-width", type=int, default=4096)
    args = parser.parse_args()

    bundle_path = os.path.join(args.game_dir, "AssetBundles", *args.bundle.split("/"))
    if not os.path.exists(bundle_path):
        raise SystemExit(f"bundle not found: {bundle_path}")

    bundle_name = args.bundle.replace("\\", "/").rstrip("/").split("/")[-1].casefold()
    scene_name = args.scene_name or SCENE_NAMES.get(bundle_name)
    if not scene_name:
        raise SystemExit(
            f"unknown scene for bundle {args.bundle}; pass --scene-name")

    print(f"loading {bundle_path} scene={scene_name}")
    environment = load_environment(bundle_path, args.game_dir)
    parallax_by_go = read_parallax(environment)
    sorting_layer_orders = read_sorting_layer_orders(args.game_dir)
    if not sorting_layer_orders:
        print(
            "warning: sorting layer table not found; assuming Default layer",
            file=sys.stderr)
    scene_asset_files = find_scene_asset_files(environment, scene_name)
    if not scene_asset_files:
        raise SystemExit(f"scene {scene_name} not found in bundle")
    print(f"scene assets: {', '.join(sorted(scene_asset_files))}")
    sprites, objects, skipped = collect_sprites(
        environment, parallax_by_go, sorting_layer_orders, scene_asset_files)
    if not objects:
        raise SystemExit("no active SpriteRenderers found in scene")

    atlas, atlas_width, atlas_height = pack_atlas(sprites, args.max_width)
    os.makedirs(args.out_dir, exist_ok=True)
    image_name = f"{args.map_id}_atlas.png"
    atlas_name = f"{args.map_id}_atlas.json"
    image_path = os.path.join(args.out_dir, image_name)
    atlas_path = os.path.join(args.out_dir, atlas_name)
    atlas.save(image_path)
    image_asset_path = os.path.relpath(image_path, "assets").replace("\\", "/")
    atlas_asset_path = os.path.relpath(atlas_path, "assets").replace("\\", "/")

    frames = []
    for sprite in sorted(sprites.values(), key=lambda item: item.name):
        frames.append({
            "name": sprite.name,
            "rect": [sprite.atlas_x, sprite.atlas_y, sprite.width, sprite.height],
            "pivot": [0.5, 0.5],
        })
    write_json(atlas_path, {
        "version": 1,
        "image": image_asset_path,
        "width": atlas_width,
        "height": atlas_height,
        "frames": frames,
    })

    camera_bounds = find_camera_bounds(environment, scene_asset_files)
    out_json = args.out_json or f"assets/maps/{args.map_id}_art.json"
    data = build_json(
        sprites,
        objects,
        atlas_width,
        atlas_height,
        image_asset_path,
        atlas_asset_path,
        camera_bounds,
    )
    write_json(out_json, data)

    vertex_count = sum(len(layer["vertices"]) // 8 for layer in data["layers"])
    triangle_count = sum(art.triangle_count for art in objects)
    print(f"sprites={len(sprites)} objects={len(objects)} skipped={skipped}")
    print(f"atlas={atlas_width}x{atlas_height} layers={len(data['layers'])} "
          f"vertices={vertex_count} triangles={triangle_count}")
    print(f"wrote {image_path}")
    print(f"wrote {atlas_path}")
    print(f"wrote {out_json}")
    if camera_bounds:
        print(f"camera_bounds={camera_bounds}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
