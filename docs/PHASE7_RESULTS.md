# Phase 7 结果：内容管线与目标模式

日期：2026-10-30

## 目标

把英雄、武器和地图从代码常量迁到可校验的数据源，提供地图编译器和内容版本校验；并加入一个地图驱动的控制点目标模式。新增可打包的英雄图集、数据驱动动画片段和客户端动画状态机；加入开发期 content/map/atlas/animation 热重载。

## 已完成

### 内容数据库

`src/core/content.cpp` / `content.h`：

- 运行时 `ContentDatabase`，默认 3 英雄、6 武器，JSON 加载失败时保留默认数据。
- 英雄：Scout、Gunner、Assault。
- 武器：Rifle、Shotgun、Rocket、SMG、Burst、Grenade。
- JSON schema 文档：`assets/data/content.schema.json`。
- JSON 校验：内容版本、至少一个英雄/武器、ID 非空且唯一、生命/速度/弹匣/射速/伤害/弹丸/爆炸参数范围、数组上限。
- 内容 fingerprint 覆盖版本、ID 和全部数值；客户端/服务器登录时比对。
- `hero_stats`、`weapon_stats`、`next_weapon` 改为运行时内容库查询；世界生成、快照校验、大厅英雄选择、HUD 均使用动态数量。

### 地图编译与控制点

`src/core/map.cpp` / `map.h`：

- `encode_map_bin` / `decode_map_bin`：`ONRM` magic、版本 2、边界、可选 objective、solid、platform、10 个 spawn。
- `load_map_file` 支持 `.json` 和 `.bin`。
- `nsync_mapc --in ... --out ... --check`：JSON→二进制并回读校验。
- 新增 `assets/maps/arena_02.json` 和编译后的 `arena_02.bin`。
- 两张地图都带 `objective` 中心区域；默认 `arena_01` 控制点位于地图中央。
- 服务器新增 `--map` / `server.map`；客户端 `--map` 支持 JSON/bin；服务器/客户端都支持 `arena_02`。

### 控制点模式

`src/core/world.cpp` / `world.h`：

- `GameMode::Tdm` 和 `GameMode::Control`。
- 控制点规则：单队站点时 3 秒完成占领，占领后每 5 秒得 1 分；双方同时在点内时冻结进度和计分。
- 控制点模式的击杀不加分，分数来自持续占领；达到 `score_limit` 或时间结束仍按现有规则判胜。
- 机器人会向控制点移动并在点外继续接敌，避免目标模式变成纯出生点对枪。
- 快照版本升到 **4**，新增 `game_mode`、`objective_owner`、`objective_capturing_team`、`objective_progress`、`objective_score_ticks`。
- 协议版本升到 **4**；快照/地图/内容 fingerprint 任一不一致都会拒绝登录。

### 协议与工具

- `LoginRequest`/`LoginResponse` 继续携带 `content_fingerprint`。
- 服务器登录校验 content fingerprint、地图 fingerprint、协议版本和英雄索引。
- 新增 `nsync_datac`：内容 JSON 校验与 fingerprint 输出。
- 服务器 `--mode tdm|control` 与 `server.mode`；客户端直接模式 `--mode`。
- CMake 增加 5 个 CTest：`nsync_content_check`、`nsync_atlas_check`、`nsync_map_compile`、`nsync_map_compile_02`、`nsync_control_loopback`。

### 图集与动画

`src/core/json.cpp` / `atlas.cpp` / `animation.cpp`、`src/client/world_renderer.cpp`：

- 新增通用 JSON 解析器、`Atlas` 元数据和 `AnimationSet` 数据格式；支持帧矩形、pivot、fps、loop、帧名列表和严格范围/重复校验。
- 新增 `nsync_atlas`：STB 解码 PNG，shelf 布局打包 `assets/sprites/source/*.png`，输出 `heroes.png` + `heroes.atlas.json`；`--check` 做无写入校验。
- 占位美术：3 英雄 × 7 状态（idle/run/jump/fall/shoot/hurt/death）× 4 帧 = **84 帧**；`assets/data/animations.json` 包含 **21 个 clip**。
- 客户端加载图集纹理和动画数据；按存活/受击/开火/空中/速度选择动画状态，非循环 clip 按状态持续时间播放，支持朝向翻转、死亡动画和缺失资源回退矩形。
- 直接模式修复 `--mode control` 未写入 `World` 的问题；HUD 显示 `CTRL`、控制点归属和占领/计分百分比。
- 核心测试新增图集/动画解析和状态选择覆盖；CTest 总数达到 16。

### 开发期热重载

`src/shared/asset_watcher.cpp`、`src/client/bgfx_app.cpp`、`src/client/world_renderer.cpp`：

- 新增 `AssetWatcher`：对 content、地图、图集 JSON/PNG、动画 JSON 做 mtime/size 轮询，启动时先 `prime()`，每 0.5 秒检查一次。
- `--hot-reload` / `client.hot_reload=true` 启用 watcher；优先挂载源码 `assets/`（`paths::source_asset_root()`），避免读取构建目录中的旧副本。`F5` 可随时手动重载一次。
- content 重载先解析到临时 `ContentDatabase`，成功后才替换全局内容库；地图先解析到临时 `Map`，校验控制点后替换；失败保留旧数据。
- 直接模式重载 content/map 会重建本地 2v2 沙盒并回到出生点；视觉重载不会重置世界。联机/本地权威模式下 content/map 变更拒绝生效，只允许图集/动画视觉热重载。
- `WorldRenderer::reload_visuals` 先完整解析图集、图片和动画并创建新纹理，成功后才交换句柄；失败保留旧图集，避免半写入文件造成闪回矩形。
- HUD 显示 4 秒 `RELOAD ...` 状态；控制台记录 `hot reload: content/map ...`、`direct sandbox reset` 和 `loaded hero atlas ...`。
- 核心测试新增 watcher 变更/删除/重建/换路径检测；CTest 仍为 **16**。

反编译参考：

- `Assets.Script.Core.Net/ConfigLoaderImpl.cs` 通过 Unity `Resources.Load*` 读取 JSON，发布包不支持运行时替换；本项目把对应边界改为可重读的 `ResourceManager` + watcher，仅开发期开启。
- `HeroPrefab.cs`、`HeroProcessor.cs` 负责英雄 prefab/视觉装配；`CharacterEntity.cs` 使用 `Grounded`、`IsMove`、`MoveVelocity`、`StartFly`、`Reverse`、`Jump` 等 animator 参数。当前图集/动画状态机保留这些状态语义，图集和动画作为可热替换的客户端表现资源。

## 验证

```text
nsync_datac: heroes=3 weapons=6 fingerprint=11475475768815027103
nsync_mapc arena_01: solids=1 platforms=2 bytes=191 fingerprint=8547681275190538290
nsync_mapc arena_02: solids=2 platforms=3 bytes=223 fingerprint=11050752168854532917
nsync_atlas: frames=84 size=512x586 image=heroes.png metadata=heroes.atlas.json
```

> 注：`content.json` 在 Phase 8 增加英雄护盾/技能字段后 fingerprint 变为 `15655814154740079468`；上表保留 Phase 7 当时值。

- Debug CTest：**16/16** 通过。
- Release CTest：**16/16** 通过。
- 手动客户端：`--map maps/arena_02.bin` 加载成功。
- 手动服务器：`--map maps/arena_02.bin` 加载成功。
- 手动真实客户端 + 服务器：`--hero assault --map maps/arena_02.bin` 登录、匹配、进入对局，corrections=0。
- 手动控制点：`--mode control --map maps/arena_01.json` 直接客户端和 `--loopback --mode control` 网络测试均通过；截图确认 `CTRL` HUD、控制点区域和蓝/红英雄图集。
- 客户端加载日志：`loaded hero atlas: frames=84 clips=21 image=512x586`。
- 动画状态抽查：死亡状态依次输出 `scout_death_1`、`scout_death_2`；受击状态输出 `scout_hurt_0`。
- 热重载冒烟：`--hot-reload --mode control --map maps/arena_01.json` 启动后修改 content/`animations.json`/地图时间戳，日志依次出现 `hot reload: content ...`、`loaded hero atlas ...`、`hot reload: map ...` 和 `direct sandbox reset`，客户端无崩溃退出。
- 手动 F5：直接模式窗口按 F5，日志出现 `manual asset reload complete: content=yes visuals=yes` 和 `direct sandbox reset`；联机/`--net` 模式修改 content 时出现 `content/map changes are disabled in server-authoritative mode`。

## 尚未完成

- 服务器端内容/地图热重载（当前需重启服务器；比赛内更新会破坏客户端 fingerprint/快照一致性）。
- 完整内容二进制打包与增量加载。
- 完整 RmlUi 大厅/房间和内容选择 UI。

## 边界

- 内容 fingerprint 只保证相同文件/相同数值；不解决服务器和客户端使用不同地图路径但相同内容的显示层差异。
- 控制点仅实现一个中心区域；多控制点、占领读条 UI 的美术表现和更复杂的目标模式留后续。
- 当前英雄图集是程序生成的占位美术，仅用于验证管线；正式美术替换源帧后重新运行 `nsync_atlas` 即可。
- 图集/动画只在客户端加载，不参与模拟或快照；动画帧选择不属于网络确定性状态。
- 热重载只支持客户端源码目录下的 assets；assets.pak 不监听。直接模式 content/map 重载会重置沙盒，联机模式拒绝 content/map 变更，服务器内容/地图仍需重启进程。
- 当前仍是一局一个服务器进程；比赛中的内容热更新不应执行。
