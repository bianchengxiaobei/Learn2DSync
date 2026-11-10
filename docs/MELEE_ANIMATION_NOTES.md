# 女武士斧头 AxeBeats 动画移植笔记

状态：**已 1:1 对齐原版，Debug/Release 构建通过，CTest 51/51，benchmark PASS**
日期：2026-11-10
范围：原版美术 Warrioress 近战攻击（axe）动画；不改 `core::World` 数值、命中盒、快照和 golden replay。

本文记录这次移植/修复的坐标系、曲线映射、朝向和验证方法，避免下次再踩同样的坑。

## 1. 数据来源

- 动画剪辑（`resources.assets`）：
  - `3256 WarrioressAxeBeats`：0.55s，53 条 streamed curve。
  - `3257 WarrioressAxeBeats2`：0.5333s，43 条 streamed curve。
  - `3261 WarrioressIdle`（只用于对照，不参与攻击）。
- 控制器 `reference/onraid/extracted/axe_1.bytes`：
  `animation.trigger="AxeBeats"`、`layer="Axe"`、`animationCount=2`。
- 原版随机逻辑：`WeaponAnimationHelper.Random.Range(0, animationCount)`。
- 斧头 `content.json`：`fire_interval=84` tick（60Hz 下 1.4s）；原版 `fireRate=42`。
- Prefab 层级：`WarrioressPref`（`resources.assets` GameObject path_id=8452）。

## 2. 曲线绑定映射（最容易错）

Unity 的 `AnimationClip.m_ClipBindingConstant.genericBindings` 按顺序对应 streamed 曲线通道：

- `attribute=1` localPosition：每个 binding 占 3 个通道（x/y/z）。
- `attribute=2` localRotation：每个 binding 占 4 个通道（quaternion x/y/z/w）。
- `attribute=3` localScale：每个 binding 占 3 个通道。
- GameObject active（`attribute=2086281974`）：1 个通道。

**注意：常量绑定不会写进 streamed 数据**（例如 3256 的 `hairBackAnim` position、3257 的 boobs/hairBack/hands），所以通道基址不能简单用 `binding_index * 3/4` 推算，必须按“实际有曲线的 binding”依次累加。工具里用 `base < curveCount` 判断。

`GenericBinding.path` 是相对 Animator 根节点（`Hero`）的 Transform 路径哈希：

- 算法：标准 CRC32（`zlib.crc32`），输入为路径字符串的 ASCII/latin-1 字节，**大小写敏感**。
- 例：`Hero/playerPivot/hairFrontAnim` → `58066150`。
- 实现：`scripts/inspect_original_melee.py` 自动解析。

两个剪辑的绑定表（已核对）：

AxeBeats (3256)：

| base | attr | 路径 |
| --- | --- | --- |
| 0 | pos | `Hero/playerPivot/bodyAnim` |
| 3 | pos | `Hero/playerPivot/hairFrontAnim` |
| 6 | pos | `Hero/playerPivot/headAnim` |
| 9 | pos | `Hero/playerPivot/shoulderBackAnim` |
| 12 | pos | `Hero/playerPivot/shoulderFrontAnim` |
| 15 | pos | `Hero/weapon/pivot/allWeaponAnim` |
| 18 | pos | `Hero/playerPivot/bodyAnim/boobsAnim` |
| 21/25/29/33/37/41/45 | rot | body / hairFront / head / shoulderBack / shoulderFront / hairBack / allWeapon |
| 49 | scale | hairFront |
| 52 | active | grenadeAnim |

AxeBeats2 (3257)：只有 6 个 pos（0/3/6/9/12/15）和 6 个 rot（18/22/26/30/34/38），顺序同上，没有 boobs/hairBack/hands 曲线；`42` 是 grenade active。

> 曾经把 3257 按 3256 的基址（21..45）取旋转，导致斧头姿态诡异。再遇到新剪辑时先用工具打印绑定表。

## 3. 坐标系与镜像换算

AssetRipper 导出的 GLB 是 Unity 的镜像空间：

- 节点 `localPosition.x = -unity_x`，`localPosition.y = unity_y`。
- 节点 z 旋转四元数取反，即 `glb_angle = -unity_angle`。
- 验证：`bodyAnim` Unity `(0.0687, -0.1406)` → GLB `(-0.0687, -0.1406)`；`allWeaponAnim` 同理。

`assets/data/hero_rigs.json` 的 quad 由 GLB 世界矩阵生成，所以**骨架位置是 Unity 的镜像**。而图集贴图是按原始 PNG 自然方向绘制的，因此初始 rig 是“位置镜像、贴图不镜像”的混合手性——这就是“人物脸朝右、斧头却往左劈”的根源。

运行期采样约定（`sample_melee_bone_keys`）：

```cpp
pose.rotation = -lerp(angle_deg) * pi / 180;
pose.offset_x = -(lerp(table_x) + 2.0f * rest_x);
pose.offset_y =   lerp(table_y);
```

其中 `table` 是生成工具按 `(unity_local - glb_rest) * scale` 存的值。推导：

```text
glb_rest.x = -unity_rest.x
需要的镜像 delta = -(unity_local - unity_rest).x
                 = -((unity_local - glb_rest.x) + 2 * glb_rest.x)
                 = -(table_x + 2 * rest_x)
```

`rest_x` 常量（= `glb_rest_x * scale`）：

| 骨骼 | rest_x |
| --- | --- |
| body | -0.082333 |
| head | -0.011984 |
| shoulder_back | 0.221702 |
| shoulder_front | -0.378693 |
| weapon (allWeapon) | 0.294805 |

典型错误：

- `offset_x = table + 2*rest_x`：保留 Unity 手性，抬手时斧头/双手大幅脱离身体。
- 只翻转 angle、不改 x：变成垂直镜像（用户反馈“上下反了、不协调”）。

如果把修正后的值直接烘焙进表，记得**同时移除运行期的 `-( ... + 2*rest_x)` 和 `-angle`**，否则会二次取反。

## 4. 骨骼 pivot 与 group transform

每个部件绕自己的 **bone origin**（归一化后的世界位置）旋转，sprite 的 pivot 不参与：

| 骨骼 | pivot（归一化 rig 空间） |
| --- | --- |
| body | (-0.133276, -0.135098) |
| head | (-0.062931, 0.334193) |
| shoulder_back | (0.170756, 0.086125) |
| shoulder_front | (-0.429640, 0.056166) |
| allWeapon | (0.888194, -0.436611) |

关键层级：`allWeaponAnim` 是 `handBackAnim` / `handFrontAnim` / `axeAnim` 的父节点。双手和斧头必须作为一个 group：

- 绕 `allWeapon` origin 旋转；
- 加上 group offset；
- 不能给手和斧头各自独立加同一份 offset。

注意 `hero_rigs.json` 里两只手的 part role 也是 `weapon`（z=21/23），走的是 parts 循环；
它们通过 `sample_melee_bone_pose("weapon", ...)` 拿到同一份 allWeapon group pose。
如果按角色名做特判，记得区分“斧头 sprite”和“手部 part”。

`AxeBeats t0` 是起手/蓄力姿势，不是 idle rest，不要用 idle 数据去覆盖 t0。

## 5. 贴图手性修复

`draw_rig_piece` 增加了 `mirror_uv`：

- 角色部件（body/head/肩/手/脚）：`mirror_uv=true`。
- 近战武器（axe）：`mirror_uv=true`。
- 枪械：`mirror_uv=false`。枪口位置和瞄准角是基于 quad 的 min-x 端推导的，翻 UV 会让枪口/瞄准错位。

这样 rig 变成完整的 M(U)（位置+贴图一致），再配合下面的 flip 得到 U / M(U) 两种状态。

## 6. 朝向 flip

- 原版 prefab 的基础朝向是**左**（头/身体 PNG 面向左）。
- 原版需要朝右时对 `Hero` 做 `rotation.y=180`，即整组水平镜像。
- GLB 骨架本身已经是镜像（M(U)），所以客户端 renderer 的 flip 语义与原版相反：
  - 朝右瞄准（`facing=+1`）→ `flip=false`；
  - 朝左瞄准（`facing=-1`）→ `flip=true`。
  - 即 `flip_original = player.facing < 0`（与占位图路径 `flip_placeholder` 一致）。

之前写成 `facing > 0`，表现就是“人物朝右、劈砍朝左”。

验证基准：

- U（Unity 未翻转）：脸朝左、斧头在左前方。
- M(U)（原版朝右状态）：脸朝右、斧头在右前方。
- 两种状态的劈砍都应指向角色面向的一侧。

## 7. 变体与时序

- 触发条件：`fire_cooldown == fire_interval && melee_seconds <= 0`；冷却中不重播。
- 变体哈希：
  `sim_tick*747796405 + player.id.raw()*2891336453 + player.weapon*277803737`，取 bit24 & 1。
- 时长：variant 0 = 0.55s，variant 1 = 0.5333333s。
- `melee_progress = 1 - melee_seconds / duration`；`melee_swing` 是 0..1 的进度。
- 调试预览时 `NSYNC_MELEE_PHASE` 是进度，不是秒：要看 clip t=0.3s，phase ≈ 0.545（variant 0）。
- 变体 1 的旋转通道基址是 18/22/26/30/34/38，不要沿用变体 0 的 21..45。

## 8. 验证方法（下次直接用）

离线参考渲染 + 关键帧导出工具：

```powershell
py -3 scripts\inspect_original_melee.py dump --clip 3256
py -3 scripts\inspect_original_melee.py dump --clip 3257
py -3 scripts\inspect_original_melee.py render --clip 3256 --time 0.15 0.3
py -3 scripts\inspect_original_melee.py render --clip 3256 --time 0.3 --mirror
```

- `dump` 输出 C++ `MeleeBoneKey` 表（存储约定 `(unity_local - glb_rest) * scale`）。
- `render` 用真实 Unity prefab 层级 + 原始 Sprite 渲染，`--mirror` 输出水平镜像版本，用于对比客户端。
- 依赖：UnityPy、numpy、Pillow、AssetRipper 导出目录。

客户端截图注意：

- SDL 窗口类名是 `SDL_app`；`Process.MainWindowHandle` 可能是控制台窗口，不能直接用。
- 用 `EnumWindows` + `GetClassName` 找 `SDL_app`，再 `PrintWindow(hwnd, dc, 2)`（`PW_RENDERFULLCONTENT`）截图。
- `CopyFromScreen` 在终端遮挡时会截到终端内容。

调试开关（提交前必须删除）：

- `NSYNC_MELEE_PHASE` / `NSYNC_MELEE_VARIANT`：强制攻击进度/变体。
- `NSYNC_FORCE_FLIP`：强制 flip，用于对比 U 与 M(U)。

## 9. 相关文件

- `src/client/world_renderer.cpp`
  - `sample_melee_bone_keys`：旋转/位移换算与 `rest_x`。
  - `sample_melee_bone_pose`：AxeBeats / AxeBeats2 关键帧表。
  - `draw_rig_piece`：`mirror_uv` 与 group transform。
  - `draw_original_hero`：双手/武器共享 allWeapon group、flip。
- `src/client/world_renderer.h`：`PlayerVisualState.melee_duration/melee_variant`。
- `assets/data/hero_rigs.json`：part/weapon quad、pivot、UV。
- `assets/data/content.json`：axe `fire_interval=84`、动画触发配置。
- `scripts/inspect_original_melee.py`：本次新增的提取/参考渲染工具。

## 10. 提交前检查清单

- [ ] 没有残留 `NSYNC_MELEE_*` / `NSYNC_FORCE_FLIP` 调试代码。
- [ ] `world_renderer.cpp` 保持 UTF-8 BOM（239,187,191）和纯 CRLF。
- [ ] 删除仓库根目录临时截图/日志。
- [ ] `cmake --build build\vs2022 --config Debug` / `--config Release`。
- [ ] `ctest --test-dir build\vs2022 -C Release --output-on-failure`。
- [ ] `build\vs2022\Release\nsync_client.exe --benchmark-heroes --hero-art original --no-audio`。
- [ ] 两种朝向（facing=±1）、两个变体、起手/过顶/下劈各截一帧对照。

本次最终验证：CTest 51/51 通过；benchmark `avg_us=27.4803 p95_us=56.2 max_us=123.2 budget_us=500 result=PASS checksum=712800`。
