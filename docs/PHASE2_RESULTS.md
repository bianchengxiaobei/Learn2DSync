# Phase 2 结果：确定性核心

日期：2026-10-29  
状态：通过（Phase 2 历史结果；Phase 3 已将快照升级到 v3，当前 golden 见 `docs/PHASE3_RESULTS.md`）

## 目标

在不接入画面的前提下，证明同一份初始状态和输入日志可以按 tick 得到完全一致的哈希；同时提供快照恢复和 headless replay 工具，为 Phase 4 的预测/回滚打基础。

## 已实现

- 定点几何：`Fix32`、`Vec2`、`AABB`、线段/AABB 相交。
- 确定性随机：PCG32；完整保存 `state/increment`；`bounded()` 使用确定性拒绝采样。
- 实体：20 位 index + 12 位 generation；`EntityAllocator` 维护 generation/free-list；`ComponentPool<T>` 按 index 升序遍历。
- 输入：`InputCommand` 包含 tick、buttons、量化 aim_x/aim_y、sequence；`InputFrame` 可乱序传入，世界内部按实体/序列排序。
- 世界：60Hz tick；最小平台/重力/移动/跳跃；射击、弹丸扫掠碰撞、伤害、死亡、复活、比分。
- 快照：全量、小端序、长度前缀、版本号；包含 tick、RNG、比分、实体 allocator 状态、玩家和弹丸；按实体 ID 排序输出。
- 哈希：XXH64（seed 0）对快照 payload 计算哈希。
- Replay：带长度前缀的输入日志；seed + player_count + 每 tick 命令；支持文件保存/读取。
- 工具：`nsync_replay`，输出每 tick 哈希，支持双跑校验。
- 测试：定点、几何、PCG32、实体 generation/free-list、字节流、快照往返/恢复续跑、replay 往返、golden replay。

## 验证命令

```powershell
cmake --build --preset debug
ctest --preset debug --output-on-failure

cmake --build --preset release
ctest --preset release --output-on-failure
```

结果：Debug/Release 均 **4/4** 通过。

Headless replay：

```powershell
cd build\vs2022\Debug
.\nsync_replay.exe --ticks 120 --hash-every 10 --verify
```

输出：

```text
tick 0 hash 3d63d55fc23b6795
tick 10 hash f3dc92f038fe832b
tick 20 hash 4e5e2ca0ae2db5d6
tick 30 hash 973e0618b2ddf7e8
tick 40 hash 46f3bb2d9c230018
tick 50 hash e8150c8ada91bbb8
tick 60 hash b862f6a87c651968
tick 70 hash d3b144b8c6f6e0d9
tick 80 hash b8ab9b209412fd95
tick 90 hash a57377df427b89ae
tick 100 hash fbc739a90a0c7fd6
tick 110 hash 1d3ccbb66df56a18
tick 120 hash 1797ce22439e5094
deterministic: yes
ticks=120 final_tick=120 final_hash=1797ce22439e5094
```

Debug 与 Release 的这组哈希完全相同，并被 `tests/golden_replay_expected.h` 固定为 golden 测试。

## 快照格式

`encode_snapshot()` 输出：

```text
u32 payload_size
payload:
  u32 version
  u32 tick
  u64 rng.state
  u64 rng.increment
  i32 scores[2]
  u32 next_index
  u32 generation_count + u16 generations[]
  u32 free_count + u32 free_list[]
  u32 player_count + player[]
  u32 projectile_count + projectile[]
```

所有多字节值小端序。实体、玩家、弹丸在编码前按 `EntityId` 的 index/generation 排序；RNG 和 allocator free-list 状态进入快照，恢复后继续创建弹丸也会得到相同 ID。

## 验收结果

- 同一 replay 跑两遍：逐 tick 哈希完全一致（`--verify`）。
- 快照恢复：tick 30 保存/编码/解码，恢复后继续跑 tick 30–59，每个 tick 与原始世界哈希一致。
- Debug/Release：同一 golden replay 哈希一致。
- 截断快照/replay、非法版本、非法 RNG、非法 allocator 状态均被拒绝。
- `nsync_replay --write/--read` 文件往返通过。

## 当前边界

- 世界是 Phase 2 的最小确定性场景，不代表最终玩法；地图、角色、武器和机器人仍在 Phase 3。
- 快照未压缩；回滚缓冲、快照插值、预测和网络 latest-wins 尚未实现。
- `nsync_server` 仍只跑固定 tick 空壳；Phase 3 再把 `World::step` 接入服务器对局循环。
- 客户端目前只验证 SDL3/bgfx/RmlUi 和资源挂载，尚不消费世界快照。
- `FixedStepClock` 的 chrono/double 只用于驱动 tick，不属于 world/snapshot/replay 的确定性计算。

## 关键文件

- `src/core/fix32.h`、`src/core/geometry.h/.cpp`
- `src/core/rng.h/.cpp`
- `src/core/entity.h/.cpp`
- `src/core/input_command.h`
- `src/core/sim_types.h`
- `src/core/world.h/.cpp`
- `src/core/byte_io.h/.cpp`
- `src/core/snapshot.h/.cpp`
- `src/core/hash.h/.cpp`
- `src/core/replay.h/.cpp`
- `src/tools/replay_main.cpp`
- `tests/test_determinism.cpp`
- `tests/golden_replay_expected.h`
