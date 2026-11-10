"""Warrioress AxeBeats 动画参考工具（开发期，不进入构建）。

两种用途：
1. dump-keys：从 resources.assets 的 AnimationClip 提取 Unity local 曲线，
   按 NSYNC 的 (unity_local - glb_rest) * scale 约定输出 C++ MeleeBoneKey 表。
2. render：用真实 Unity prefab 层级 + 原始 Sprite 图渲染参考帧，
   用于和客户端截图做 1:1 对比（可输出水平镜像版本）。

依赖：UnityPy、numpy、Pillow，以及仓库内 scripts/import_original_heroes.py。

示例：
  py -3 scripts/inspect_original_melee.py dump --clip 3256
  py -3 scripts/inspect_original_melee.py dump --clip 3257
  py -3 scripts/inspect_original_melee.py render --clip 3256 --time 0.0 0.15 0.3
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import zlib
from pathlib import Path

import numpy as np
import UnityPy
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
import import_original_heroes as importer  # noqa: E402

RESOURCES = Path(r"E:\Soft\Steam\steamapps\common\ONRAID\ONRAID_Data\resources.assets")
ASSETS = next(
    Path("E:/").glob("*/AssetRipper_win_x64/t4t4t/AssetRipper_export_*/Assets")
)
SPRITE_DIR = REPO / "reference" / "onraid" / "extracted" / "sprites"
PREFAB_GO = 8452  # WarrioressPref

# 角色部件与贴图；z 用于渲染排序。
SPRITES = {
    "bodySprite": ("WarrioressBody1", 4),
    "boobsSprite": ("WarrioressBoobs1", 5),
    "headSprite": ("WarrioressHead1", 11),
    "shoulderBackSprite": ("WarrioressShoulderBack1", 2),
    "shoulderFrontSprite": ("WarrioressShoulderFront1", 10),
    "hairBackSprite": ("WarrioressHairBack1", 3),
    "handBackSprite": ("WarrioressHandBack1", 21),
    "handFrontSprite": ("WarrioressHandFront1", 23),
    "footBackSprite": ("WarrioressFoot1", 0),
    "footFrontSprite": ("WarrioressFoot1", 0),
    "pirateAxe": ("axe7", 22),
}

ROLE_SUFFIX = {
    "body": "bodyAnim",
    "head": "headAnim",
    "shoulder_back": "shoulderBackAnim",
    "shoulder_front": "shoulderFrontAnim",
    "weapon": "allWeaponAnim",
}

# glb rest x * 归一化 scale，运行期镜像修正用：
# offset_x = -(stored + 2 * REST_X)
REST_X = {
    "body": -0.082333,
    "head": -0.011984,
    "shoulder_back": 0.221702,
    "shoulder_front": -0.378693,
    "weapon": 0.294805,
}
PIVOT = {
    "body": (-0.133276, -0.135098),
    "head": (-0.062931, 0.334193),
    "shoulder_back": (0.170756, 0.086125),
    "shoulder_front": (-0.429640, 0.056166),
    "weapon": (0.888194, -0.436611),
}


def load_unity_hierarchy():
    env = UnityPy.load(str(RESOURCES))
    objs = {obj.path_id: obj for obj in env.objects}
    transform = objs[PREFAB_GO].read().m_Transform.read()
    names, parents, children, local = {}, {}, {}, {}

    def walk(t, parent_id):
        game_object = t.m_GameObject.read() if t.m_GameObject else None
        name = game_object.m_Name if game_object else "?"
        lp, lr, ls = t.m_LocalPosition, t.m_LocalRotation, t.m_LocalScale
        local[t.path_id] = (
            float(ls.X),
            float(ls.Y),
            float(lp.X),
            float(lp.Y),
            float(lr.Z),
            float(lr.W),
        )
        names[t.path_id] = name
        parents[t.path_id] = parent_id
        children[t.path_id] = []
        if parent_id is not None:
            children[parent_id].append(t.path_id)
        for child in t.m_Children:
            walk(child.read(), t.path_id)

    walk(transform, None)
    by_name = {names[tid]: tid for tid in names}
    return objs, transform.path_id, names, children, local, by_name


def build_path_hashes(root_id, names, children):
    """Unity GenericBinding.path = CRC32(相对 Animator root 的 Transform 路径)。"""

    hashes = {}

    def walk(tid, path):
        full = f"{path}/{names[tid]}" if path else names[tid]
        relative = "/".join(full.split("/")[1:])  # 去掉 WarrioressPref
        hashes[zlib.crc32(relative.encode("latin-1")) & 0xFFFFFFFF] = relative
        for child in children[tid]:
            walk(child, full)

    walk(root_id, "")
    return hashes


def load_clip(objs, path_id):
    clip = objs[path_id].read()
    frames = clip.m_MuscleClip.m_Clip.m_StreamedClip.ReadData()
    curves = {}
    for frame in frames:
        if not math.isfinite(frame.time):
            continue
        for key in frame.keyList:
            curves.setdefault(key.index, []).append((frame.time, key.value))
    for samples in curves.values():
        samples.sort()
    return clip, curves


def curve_value(curves, index, time, default):
    samples = curves.get(index)
    if not samples:
        return default
    if time <= samples[0][0]:
        return samples[0][1]
    if time >= samples[-1][0]:
        return samples[-1][1]
    for i in range(len(samples) - 1):
        ta, va = samples[i]
        tb, vb = samples[i + 1]
        if ta <= time <= tb:
            t = (time - ta) / (tb - ta) if tb > ta else 0.0
            return va + (vb - va) * t
    return samples[-1][1]


def resolve_bindings(clip, curves, path_hashes):
    """genericBindings 按顺序占用曲线通道；常量尾绑定不会出现在 streamed 数据里。"""

    channel_count = {1: 3, 2: 4, 3: 3}
    base = 0
    result = []
    for binding in clip.m_ClipBindingConstant.genericBindings:
        attribute = binding.attribute
        count = channel_count.get(attribute, 1)
        path = path_hashes.get(binding.path, f"hash:{binding.path}")
        result.append(
            {
                "path": path,
                "attribute": attribute,
                "base": base,
                "count": count,
                "animated": base < clip.m_MuscleClip.m_Clip.m_StreamedClip.curveCount,
            }
        )
        if result[-1]["animated"]:
            base += count
    return result


def normalized_rest():
    """与 import_original_heroes 相同的归一化 scale / glb rest。"""

    spec = next(s for s in importer.HEROES if s.hero_id == "warrioress")
    root = ASSETS / "Resources" / "heroprefabs" / "characters" / spec.folder
    glb = importer.load_glb_json(root / f"{spec.prefab}.glb")
    matrices = importer.find_node_matrices(glb)
    parts = importer.build_hero_parts(spec, ASSETS, matrices)
    points = [point for part in parts for point in part["quad"]]
    min_y = min(point[1] for point in points)
    max_y = max(point[1] for point in points)
    scale = importer.TARGET_HEIGHT / (max_y - min_y)
    rest = {}
    for role, suffix in ROLE_SUFFIX.items():
        node = next(n for n in glb["nodes"] if n.get("name") == suffix)
        translation = node.get("translation") or (0.0, 0.0, 0.0)
        rest[role] = (translation[0], translation[1])
    return scale, rest


def role_binding(bindings, role, attribute):
    suffix = ROLE_SUFFIX[role]
    for binding in bindings:
        if binding["attribute"] == attribute and binding["path"].endswith(
            "/" + suffix
        ):
            return binding
    raise KeyError((role, attribute))


def dump_keys(clip_id):
    objs, root_id, names, children, _local, _by_name = load_unity_hierarchy()
    path_hashes = build_path_hashes(root_id, names, children)
    clip, curves = load_clip(objs, clip_id)
    bindings = resolve_bindings(clip, curves, path_hashes)
    scale, rest = normalized_rest()
    stop = clip.m_MuscleClip.m_StopTime
    frames = int(round(stop * 60.0)) + 1
    print(f"// clip {clip_id} {clip.m_Name} stop={stop:.4f}s frames={frames}")
    print("// bindings:")
    for binding in bindings:
        if binding["animated"]:
            print(
                f"//   base={binding['base']:2d} attr={binding['attribute']} "
                f"{binding['path']}"
            )
    for role in ROLE_SUFFIX:
        position = role_binding(bindings, role, 1)
        rotation = role_binding(bindings, role, 2)
        table = role.title().replace("_", "")
        print(f"\nconstexpr MeleeBoneKey k{table}Keys[] = {{")
        for frame in range(frames):
            time = frame / 60.0
            x = curve_value(curves, position["base"], time, rest[role][0])
            y = curve_value(curves, position["base"] + 1, time, rest[role][1])
            z = curve_value(curves, rotation["base"] + 2, time, 0.0)
            w = curve_value(curves, rotation["base"] + 3, time, 1.0)
            angle = 2.0 * math.atan2(z, w)
            nx = (x - rest[role][0]) * scale
            ny = (y - rest[role][1]) * scale
            print(
                "    {%.4ff, %.6ff, %.6ff, %.6ff},"
                % (time, angle * 180.0 / math.pi, nx, ny)
            )
        print("};")


def render(clip_id, times, mirror=False, out_dir=Path(".")):
    objs, root_id, names, children, local, by_name = load_unity_hierarchy()
    path_hashes = build_path_hashes(root_id, names, children)
    clip, curves = load_clip(objs, clip_id)
    bindings = resolve_bindings(clip, curves, path_hashes)
    animated = {}
    for binding in bindings:
        if binding["animated"]:
            animated[(binding["path"].split("/")[-1], binding["attribute"])] = binding

    sprite_meta = {}
    for node_name, (texture, _z) in SPRITES.items():
        path = ASSETS / "Resources" / f"{texture}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        sprite_meta[node_name] = [
            (v["m_Pos"]["m_X"], v["m_Pos"]["m_Y"])
            for v in data["m_RD"]["m_Vertices"]
        ]

    def local_at(tid, time):
        sx, sy, tx, ty, z, w = local[tid]
        name = names[tid]
        position = animated.get((name, 1))
        rotation = animated.get((name, 2))
        if position is not None:
            tx = curve_value(curves, position["base"], time, tx)
            ty = curve_value(curves, position["base"] + 1, time, ty)
        if rotation is not None:
            z = curve_value(curves, rotation["base"] + 2, time, z)
            w = curve_value(curves, rotation["base"] + 3, time, w)
        angle = 2.0 * math.atan2(z, w)
        c, s = math.cos(angle), math.sin(angle)
        return (c * sx, s * sx, -s * sy, c * sy, tx, ty)

    def matmul(a, b):
        return (
            a[0] * b[0] + a[2] * b[1],
            a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3],
            a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4],
            a[1] * b[4] + a[3] * b[5] + a[5],
        )

    scale = 170.0
    width, height = 720, 760
    origin_x, origin_y = width / 2, height - 70

    for time in times:
        world = {}

        def walk(tid, parent_matrix):
            matrix = matmul(parent_matrix, local_at(tid, time))
            world[tid] = matrix
            for child in children[tid]:
                walk(child, matrix)

        walk(root_id, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0))
        canvas = Image.new("RGBA", (width, height), (20, 24, 32, 255))
        layers = []
        for node_name, (texture, z) in SPRITES.items():
            tid = by_name.get(node_name)
            if tid is None:
                continue
            matrix = world[tid]
            points = [
                (
                    origin_x + (mx * scale),
                    origin_y - (my * scale),
                )
                for mx, my in (
                    (
                        matrix[0] * p[0] + matrix[2] * p[1] + matrix[4],
                        matrix[1] * p[0] + matrix[3] * p[1] + matrix[5],
                    )
                    for p in sprite_meta[node_name]
                )
            ]
            if mirror:
                points = [(2 * origin_x - x, y) for x, y in points]
            image = Image.open(SPRITE_DIR / f"{texture}.png").convert("RGBA")
            w, h = image.size
            source = [(0, h), (w, h), (0, 0), (w, 0)]
            a_matrix, b_vector = [], []
            for (sx, sy), (dx, dy) in zip(source, points):
                a_matrix.append([sx, sy, 1, 0, 0, 0])
                b_vector.append(dx)
                a_matrix.append([0, 0, 0, sx, sy, 1])
                b_vector.append(dy)
            coef, _, _, _ = np.linalg.lstsq(
                np.array(a_matrix, dtype=float),
                np.array(b_vector, dtype=float),
                rcond=None,
            )
            a, b, c, d, e, f = coef
            linear = np.array([[a, b], [d, e]])
            try:
                inverse = np.linalg.inv(linear)
            except np.linalg.LinAlgError:
                continue
            translation = -inverse @ np.array([c, f])
            affine = (
                inverse[0, 0],
                inverse[0, 1],
                translation[0],
                inverse[1, 0],
                inverse[1, 1],
                translation[1],
            )
            layers.append(
                (
                    z,
                    image.transform(
                        (width, height),
                        Image.Transform.AFFINE,
                        affine,
                        resample=Image.Resampling.BICUBIC,
                    ),
                )
            )
        for _z, layer in sorted(layers, key=lambda item: item[0]):
            canvas.alpha_composite(layer)
        draw = ImageDraw.Draw(canvas)
        draw.text(
            (10, 10),
            f"clip={clip_id} t={time:.4f} mirror={mirror}",
            fill=(255, 255, 255),
        )
        suffix = "_mirror" if mirror else ""
        output = out_dir / f"ref_{clip_id}_{int(round(time * 1000)):04d}{suffix}.png"
        canvas.convert("RGB").save(output)
        print("saved", output)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="mode", required=True)

    dump = sub.add_parser("dump", help="输出 C++ 关键帧表")
    dump.add_argument("--clip", type=int, default=3256)

    render_parser = sub.add_parser("render", help="渲染真实 Unity 参考帧")
    render_parser.add_argument("--clip", type=int, default=3256)
    render_parser.add_argument("--time", type=float, nargs="+", required=True)
    render_parser.add_argument("--mirror", action="store_true")
    render_parser.add_argument("--out-dir", type=Path, default=Path("."))

    args = parser.parse_args()
    if args.mode == "dump":
        dump_keys(args.clip)
    else:
        render(args.clip, args.time, args.mirror, args.out_dir)


if __name__ == "__main__":
    main()
