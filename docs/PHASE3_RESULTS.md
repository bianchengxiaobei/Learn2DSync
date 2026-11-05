# Phase 3 结果：单机玩法垂直切片

日期：2026-10-29  
状态：通过

## 目标

验证一张地图、三把武器、TDM、2v2 机器人可以在本地完整跑完一局，并为后续网络同步保留确定性。

## 已实现

- 地图：`assets/maps/arena_01.json` → `core::Map`，支持 bounds、实心 AABB、单向平台、10 个队伍出生点。
- 角色：移动、跳跃、重力、地面检测、单向平台从下穿过、地图边界限制。
- 相机：本地玩家跟随，世界坐标 y 向下转换为屏幕坐标。
- 英雄：Scout（100 HP、快）、Gunner（140 HP、慢）；2v2 中双方各有一名 Scout/Gunner。
- 武器：
  - 步枪：单发、中等伤害、短冷却。
  - 霰弹：6 弹丸、强散射、近距离。
  - 火箭：慢速、爆炸范围伤害、可自伤。
  - 弹丸与地图实心/单向平台碰撞；`Q` 循环切换武器。
- TDM：默认 5 分钟、30 分上限；比分、倒计时、Team 1/Team 2/平局结算。
- 机器人：确定性巡逻/追击/瞄准/射击/跳跃/换弹；客户端启动 2v2（本地 + 3 bot）。
- 表现：地图/平台/出生点/玩家/血量条/弹丸/HUD/相机；音效使用日志 cue 占位。
- 快照升级到 v3：加入 match state、weapon、hero、bot、弹丸爆炸字段。

## 验证

```powershell
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

结果：Debug/Release 均 **4/4** 通过。

`nsync_tests` 覆盖：

- 地图 JSON 解析、默认地图/资源一致性、单向平台跳跃测试。
- 英雄属性差异（Scout 100 HP / Gunner 140 HP、速度）。
- 霰弹 6 弹丸与弹药扣减。
- 火箭爆炸 + 1 分上限胜负。
- TDM 30 tick 超时平局。
- 2v2 机器人逐 tick 双世界哈希一致，且 18000 tick 内结束比赛。
- 快照/replay/golden、配置、资源挂载。

Golden replay（120 tick）：

```text
tick 0 hash de0af763027dfb7a
tick 120 hash 91e235cb870325bf
deterministic: yes
```

5 分钟 headless 双跑：

```powershell
.\nsync_replay.exe --ticks 18000 --hash-every 18000 --verify
```

结果：`deterministic: yes`。

客户端 smoke：

```powershell
.\nsync_client.exe --no-vsync --frames 600 --ticks 1800 --log-level warn
```

退出码 0。

## 当前边界

- 英雄选择 UI 未实现；对局按槽位自动分配 Scout/Gunner。
- 音效只有 `src/client/audio_stub.cpp` 日志占位，没有实际音频资源（Phase 8 已替换为 `src/client/audio.*` 的 SDL3 程序化音效）。
- 机器人没有导航网格，只有水平追击/周期跳跃；复杂地形会卡住。
- 一局结束后客户端冻结，尚未接“重开/返回大厅”。
- `nsync_server` 仍未接入 `World::step`；Phase 4 已完成本地网络测试台，真实服务器/网络在 Phase 5。
- 快照仍为全量未压缩格式。

## 关键文件

- `src/core/map.h/.cpp`、`src/core/heroes.h`、`src/core/weapons.h`
- `src/core/world.h/.cpp`
- `src/core/sim_types.h`、`src/core/snapshot.h/.cpp`
- `src/client/world_renderer.h/.cpp`、`src/client/audio_stub.h/.cpp`（Phase 8 替换为 `audio.h/.cpp`）
- `src/client/bgfx_app.cpp`
- `assets/maps/arena_01.json`、`assets/ui/spike.rml`
- `tests/test_determinism.cpp`、`tests/golden_replay_expected.h`
