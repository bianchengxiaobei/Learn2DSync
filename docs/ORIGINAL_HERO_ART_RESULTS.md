# 原版角色美术接入结果（方案 A）

状态：**基础版已实现，A6.1 性能已实测，Debug/Release CTest 16/16**
日期：2026-10-30
范围：客户端战斗表现；未改 `core::World`、命中盒、武器数值、快照、协议、content fingerprint 或 golden replay。

## 1. 完成内容

### 1.1 导入管线

新增 `scripts/import_original_heroes.py`（开发期 Python，不进入 CMake/运行时）：

- 读取用户提供的 AssetRipper 导出：
  `Resources\heroprefabs\characters\scout\ScoutPref.glb`、
  `machinegunner\MachineGunnerPref.glb`、
  `assault\AssaultPref.glb`。
- 读取 `Resources\{Hero}Body1.json` 等 Sprite 元数据的 `m_RD.m_Vertices` 与 `m_PixelsToUnits`。
- 读取 `reference/nsync/extracted/sprites/*.png` 的已裁基础皮肤部件。
- 输出：
  - `assets/generated/original_heroes/heroes.png`（512×342，27 帧）
  - `assets/generated/original_heroes/heroes.atlas.json`
  - `assets/data/hero_rigs.json`
- 坐标处理：
  - prefab 层级/2D 变换 → 世界空间；
  - 三英雄按整体包围盒归一化到 1.8 世界单位高，底部对齐 `-0.9`；
  - 保留原版漂浮靴子间距；
  - 脚部按 prefab 路径处理：`footBackAnim/footFrontSprite` 与 `footFrontAnim/footBackSprite` 的名字交换不能用节点名推断；
  - z-order：脚 0、后肩 2、身体 4、前肩 10、头 11、后手 21、武器 22、前手 23。

命令：

```powershell
py -3 scripts\import_original_heroes.py `
  --assets-root "E:\迅雷下载\AssetRipper_win_x64\t4t4t\AssetRipper_export_20261030_112341\Assets" `
  --sprite-root reference\nsync\extracted\sprites `
  --out-dir assets\generated\original_heroes `
  --rig-out assets\data\hero_rigs.json
```

### 1.2 数据层

新增 `src/core/hero_rig.h` / `hero_rig.cpp`：

- `HeroPoint`、`HeroPart`、`HeroWeapon`、`HeroRig`、`HeroRigSet`。
- `HeroPoseTrack`、`HeroPoseState`、`HeroPoseRecoil`、`HeroPoseSet`。
- JSON 版本/数量/坐标/UV/重复 id 校验；加载失败返回错误，不抛异常。
- 启动时验证所有 part/weapon sprite 都能在 atlas 中找到。
- `CMakeLists.txt` 将 `hero_rig.cpp` 编入 `nsync_core`。
- `tests/test_main.cpp` 增加 rig/pose/atlas 一致性测试；缺失生成物时按本地资源策略跳过原版图集部分。

### 1.3 渲染与动作

扩展 `WorldRenderer`：

- 多部件旋转 quad、每件 UV、z-order、整体朝向翻转。
- Unity Y-up rig → NSYNC Y-down 世界/屏幕映射：
  在部件变换完成后执行 `screen_y = origin_y - local_y`。
- 原版武器 sprite 按 `player.weapon` 的 content id 选择：
  `rifle`、`shotgun`、`rocket`、`smg`、`burst`、`grenade`。
- 程序化 pose：`idle`、`run`、`jump`、`fall`、`shoot`、`hurt`、`death`。
- 开火后坐力脉冲、受击 additive 闪白、死亡淡出。
- 队伍脚下椭圆环；保留 HP/护盾条和本地玩家头顶标记（标记修正到头顶）。
- 原版与占位图集分开批次提交，同屏部件仍为单批次纹理绘制。
- 原版缺失/解析失败时逐英雄回退 84 帧占位图集；`client.hero_art=placeholder` 强制旧图集。

### 1.4 配置与热重载

- `client.hero_art=auto|placeholder|original`，默认 `auto`。
- `--hero-art auto|placeholder|original`。
- `auto`/`original` 加载失败只 warning 并 fallback，不阻止启动。
- watcher 增加 `data/hero_rigs.json`、`data/hero_poses.json`、原版 atlas/image。
- F5 已验证会重新加载 content/map/visuals，日志：
  `manual asset reload complete: content=yes visuals=yes`。

## 2. 验证结果

### 2.1 构建与回归

```powershell
cmake --build --preset debug
cmake --build --preset release
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

结果：Debug **16/16**、Release **16/16**；内容 fingerprint `15655814154740079468`、快照/协议 v6、golden replay 未变。

### 2.2 视觉验证

- 960×600/1280×720 下三英雄基础皮肤完整显示；原版躯干与靴子分离、漂浮靴子风格保留。
- 启动日志确认：
  `loaded original hero art: heroes=3 atlas=512x342 poses=7 mode=original`。
- `--hero-art placeholder` 确认旧 84 帧图集仍可用。
- 移除构建目录原版 atlas 后，`--hero-art original` 日志 warning 并回退占位，不崩溃。
- 受击 additive 闪白、死亡淡出、队伍脚下环、武器/手部层级均已接入。
- 本地验收截图生成在临时目录，未提交原版派生图；分发授权仍未确认。

### 2.3 性能验证（A6.1）

新增 `WorldRenderer::run_original_hero_benchmark` 和 `--benchmark-heroes`。基准在 1280×720、10 个英雄、每英雄 8 个身体部件 + 1 个武器（90 quad/frame）、全部开启受击闪白（几何量按最坏情况翻倍）下，对实际分层绘制路径计时；包含 pose 采样、culling、顶点生成，不包含 GPU 提交、UI 和模拟。600 帧测量前有 60 帧预热。

```powershell
.\nsync_client.exe --no-menu --no-audio --hero-art original --benchmark-heroes --log-level info
```

Release 连续 3 次：

| 次数 | avg | p95 | max |
|---|---:|---:|---:|
| 1 | 23.9 µs | 30.8 µs | 49.1 µs |
| 2 | 19.7 µs | 26.3 µs | 41.3 µs |
| 3 | 24.3 µs | 30.9 µs | 50.5 µs |

Release 平均约 **0.020–0.024 ms/帧**，为目标 0.5 ms 的约 **4–5%**，以 avg 判定为 `PASS`。Debug 单次为 avg 313.4 µs / p95 408.7 µs / max 528.5 µs，主要受未优化 STL/调试构建影响；平均仍低于预算，但 Debug max 不作为准入指标。结果为本机 Windows/MSVC x64 测量，不同 CPU 会变化；基准不替代真实 10 人网络对局 profiling。

### 2.4 未完全覆盖

- 未做逐帧原版动画对照；当前动作是程序化近似，不宣称复刻 `ScoutIdle` 等 clip。
- 未做 7 套皮肤；只接基础皮肤 1。
- 未做原版粒子/头像/技能图标/骨骼蒙皮。
- 自动 watcher 文件变更未单独做长时验收；F5 手动重载已验证，watcher 代码路径与既有 content/map 路径共用。

## 3. 已知限制

1. 当前 AssetRipper 导出没有实际 `.anim`/`.controller`；GLB `animations` 为空，`index.bytes` 只有动画名。原版逐帧动作需要重新导出 `.anim`/FBX，或单独解码 streamed clip。
2. 原版派生图集/生成物是否允许提交/发行尚未确认；默认仅本地开发/演示，CI 缺失时回退占位。
3. 队伍色使用脚下环，不做整张 tint；原版颜色保留。
4. 受击闪白为 additive 近似，不是 shader 白色遮罩。
5. 武器/手部位置按 prefab 公共 weapon attach 点归一化，个别武器可能视觉上略有差异；缺图时隐藏武器，不阻塞角色本体。

## 4. 相关文件

- `scripts/import_original_heroes.py`
- `assets/generated/original_heroes/heroes.png`
- `assets/generated/original_heroes/heroes.atlas.json`
- `assets/data/hero_rigs.json`
- `assets/data/hero_poses.json`
- `src/core/hero_rig.h` / `src/core/hero_rig.cpp`
- `src/client/world_renderer.h` / `src/client/world_renderer.cpp`
- `src/client/bgfx_app.cpp` / `src/client/main.cpp`
- `tests/test_main.cpp`
- `docs/ORIGINAL_HERO_ART_REQUIREMENTS.md`、`docs/TODO.md`、`README.md`
