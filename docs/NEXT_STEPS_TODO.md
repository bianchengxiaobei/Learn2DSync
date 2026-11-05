# 剩余原版对齐 TODO

状态：**Phase 0/1.5 已完成；Phase 1.4 固定 step scheduler/RTT 闭环已完成（原版 lag 实验开关，客户端默认关闭）；Phase 2 动作事件已完成；Phase 3.1/3.2/3.3/3.4 真实双进程、10 人 150ms、多局、10 分钟 soak、WAN burst、延迟尖峰、trace 回放、late-replay ACK 归因、World1 地图/移动属性导入、原版 9 个 gadget 技能槽、v1.68 联机手感与 v1.69 预测/迟到输入稳定性修复与 v1.70 分叉诊断与 v1.71 斜坡穿地修复与 v1.72 远端快照校正与 v1.73 渲染平滑与 v1.74 远端校正性能已完成**（2026-11-02）。  
目标：在**不改变传输层**的前提下，把当前实现继续向反编译原版靠拢，形成可分步执行、可验收、可回退的任务清单。  
传输层（原版 TCP/WebSocket vs 当前 KCP/UDP）明确不在本计划范围内；如果以后要求与原版客户端/服务器互通，另开独立 Phase。

## 0. 当前基线

- Debug/Release CTest：**40/40**（新增 `nsync_map_compile_world1`、`nsync_net_map_world1`、WAN burst/spike/trace 与 ACK 回归）。
- 延迟矩阵：`nsync_net_real --loopback` 2 客户端 300 tick，客户端 KCP 模拟 RTT，0/50/150ms 均 `corrections=0`、`desyncs=0`。
- Phase 0 新增统计后，延迟矩阵显示 0ms 仍有同 tick `snapshot_mismatches`（一次实测 17/300 tick）但不产生 rollback；说明此前 `corrections=0` 确实掩盖了快照层校正。
- 50ms 一次实测：`snapshot_mismatches=64`、`snapshot_restores=64`、`event_rollbacks=60`、`rollbacks=60`、`rollback_ticks=61`、`rejected=0`。
- 150ms 一次实测：`snapshot_mismatches=72`、`snapshot_restores=72`、`event_rollbacks=116`、`rollbacks=134`、`rollback_ticks=202`、`rejected=0`；不同运行会波动，早先曾出现 0–6 次迟到拒绝。
- `nsync_net_real --client --map assets/maps/arena_02.json` 已连接同地图服务器通过；地图 fingerprint 不再因默认 `arena_01` 失败。
- 当前发布包：`dist/nsync-0.1.0-win64.zip`，SHA256 `51FF9B6F687CB562243DAF9D0274CB341B7F792944CE0D066EC396B11C6EB811`。

当前实现与原版的主要差距：

1. 客户端没有恢复原版 `serverLag` 本地拍帧偏移；`RealClient::begin_alignment` 目前直接完成，`catch_up_lag` 追到 `highest_server_tick_`，文档 Step 5 与实现不一致。
2. 协议已升 v9；`LoginResponse`/`MatchStart`/`SyncResponse` 带 `sync_time_to_step_ms`，输入消息带 `ClientActionMask`/toggle/param。客户端已把同步值换算成有效 server lag；`--fixed-step-scheduler` 用 `estimated_server_tick - effective_lag` 调度 0–4 个本地 step，STS 已能直接调整物理步进节奏。
3. 服务器收到目标早于当前 tick 的输入时，`late_input_replay` 原型走回放；否则夹到 `server_tick + 1`，仍未完整复刻原版事件缓冲语义。
4. 输入已附加原版 `ClientEvent` 动作位（v9），但状态按钮仍是权威路径；服务器事件队列、去重排序和事件驱动应用尚未完成。
5. 普通 snapshot mismatch 已改为单独计 `snapshot_mismatches`/`snapshot_restores`，不再只靠 `rollbacks` 观察；但 0ms 同 tick mismatch 仍需在 Phase 1/2 查清来源。
6. 真实多进程 10 分钟 soak、250/400ms WAN、丢包/抖动/重复短测和 10 场并发 LocalSession 已完成；真实 10 场并发进程、客户端 FPS/带宽曲线已完成。
7. `nsync_net_real --client` 已支持 `--map`；默认路径仍兼容原行为。
8. 技能已按原版 3 槽 gadget 替换，但仍是简化机制：无技能图标/动画/原版粒子/专属音效，部分范围/持续时间近似；生命/护盾与武器伤害仍未完整按原版换算。

## 1. 执行顺序与门禁

| 阶段 | 内容 | 规模 | 进入下一阶段的门禁 |
|---|---|---|---|
| Phase 0 | 基线、统计口径、测地图 fingerprint、测试工具清理 | 小 | 统计能区分 snapshot mismatch / rollback / desync；40/40 不回归 |
| Phase 1 | 原版客户端 `serverLag` 偏移 + `syncTimeToStep` + 迟到输入策略 | 大 | 明确“精确回放”或“近似应用”方案；延迟矩阵无 corrections/desync |
| Phase 2 | 原版 `ClientEvent` 动作事件 | 中 | 动作事件与状态输入在本地/在线行为一致；协议模糊测试通过 |
| Phase 3 | 真实多进程、WAN、10 分钟 soak、10 场并发 | 中 | 无持续 desync/内存增长；性能达标 |
| Phase 4 | 文档、发布包、CI 收尾 | 小 | 全量测试、包验证、哈希更新完成 |

原则：

- 每个阶段独立提交/验证；上一阶段失败不进入下一阶段。
- 所有协议变更先升版本、补编解码/fuzz，再接客户端/服务器。
- 任何“为了通过测试而隐藏统计”的改动都必须在文档中显式说明。
- 任何原版行为近似都要有 `DECISION` 记录，避免后续误以为是逐字节复刻。

---

## Phase 0：基线、统计口径与测试工具

目标：先把“测什么、怎么算”固定下来，避免 Phase 1 优化被统计口径掩盖。

### 0.1 统计口径

Phase 0 开始时的问题：

- `LocalClient::apply_snapshot` 对非权威 snapshot mismatch 仍会 restore/replay，并计 `rollbacks`，但 `corrections/desyncs` 不增加。
- 结果可能出现“`corrections=0` 但 rollbacks 很高”的观感，无法区分 snapshot 校正和迟到事件回滚。

任务：

- [x] 在 `ClientStats`/`NetClientStats` 中拆分：
  - `snapshot_mismatches`
  - `snapshot_restores`
  - `event_rollbacks`
  - `total_rollback_ticks`
  - 既有 `desyncs` 明确为权威 mismatch；`rollbacks` 保留为总回滚次数
- [x] `corrections` 只在权威 mismatch 时增加；`desyncs` 只在权威 snapshot mismatch 时增加。
- [x] 普通 snapshot 的 mismatch/restore 单独计数，不再静默。
- [x] HUD/日志/`nsync_net_real`/`nsync_net` 输出同步更新。
- [x] 单元测试覆盖：匹配快照、非权威 mismatch、权威 mismatch、迟到事件回滚。

验收：

- [x] 0ms 无 `corrections/desyncs`；同 tick `snapshot_mismatches` 被显式记录（一次实测 17，无 rollback）。
- [x] 50/150ms 输出能区分 `snapshot_mismatches`、`event_rollbacks` 和总 `rollbacks`。
- [x] CTest 40/40 不回归。

### 0.2 测地图 fingerprint

任务：

- [x] `nsync_net_real` 增加 `--map PATH`，使用和服务器相同的地图 JSON。
- [x] `--client` 不再无条件 `Map::default_arena()`；未传 `--map` 时保持原默认。
- [x] 增加 `nsync_net_map_arena02` loopback CTest，验证自定义地图加载和运行。
- [x] 连接失败信息输出 map 路径；登录 fingerprint mismatch 输出 server/client 值。

验收：

- [x] `nsync_net_real --client --map assets/maps/arena_02.json` 连接同地图服务器通过。
- [x] `arena_02` loopback smoke 通过；`arena_01` 由原 loopback 覆盖。
- [x] 原有 golden replay/map 测试不变。

### 0.3 基线记录

- [x] 把当前 0/50/150ms 的 `rtt/corrections/desyncs/snapshot_mismatches/event_rollbacks/rollbacks/rejected` 写入本文和 `docs/PHASE5_RESULTS.md`。
- [x] 建立可重复的 Release 命令集：

```powershell
.\build\vs2022\Release\nsync_net_real.exe --loopback --ticks 300 --players 2 --input-delay 16 --log-level warn
.\build\vs2022\Release\nsync_net_real.exe --loopback --ticks 300 --players 2 --input-delay 16 --rtt-ms 50 --log-level warn
.\build\vs2022\Release\nsync_net_real.exe --loopback --ticks 300 --players 2 --input-delay 16 --rtt-ms 150 --log-level warn
```

### Phase 0 实施结果

- 统计字段已接入 `src/net/local_net.*`、`src/net/real_client.*`、HUD、`nsync_net_real`、`nsync_net` 和对局结束日志。
- 新增 `test_local_snapshot_stats`；`test_local_late_input_rollback` 增加 `event_rollbacks`/`total_rollback_ticks` 断言。
- `nsync_net_real --map PATH` 支持客户端和 loopback；新增 `nsync_net_map_arena02`。
- 已验证 `--client --map arena_02.json` 与相同地图服务器连接。
- Debug/Release CTest 40/40。
- **遗留发现**：0ms 仍有少量同 tick `snapshot_mismatches`（本次 17），虽然不产生 `rollbacks`；Phase 1/2 需要解释来源并决定是否消除。

---

## Phase 1：原版客户端时间偏移（P0）

这是当前最大的行为差距，也是风险最高的阶段。必须先做 spike/决策，再改默认。

### 1.1 目标语义

对齐反编译版：

- 客户端本地模拟相对服务器落后约 `serverLag` 帧。
- 输入 target 仍按原版 `CurrentFrame + clientEventLag`（普通）或完整恢复公式计算。
- `syncTimeToStep` 用于校正本地 `stepTime` 与服务器时间的偏差。
- 客户端收到服务器同步时，快照/事件与本地 `CurrentFrame` 对齐，而不是无限追到服务器最新 tick。

### 1.2 先做决策门禁

- [x] **DECISION 1：迟到输入策略**
  - A. 近似：客户端 lag + 服务器接受迟到窗口，但 target 早于当前 tick 时仍夹到下一 tick。
  - B. 精确：服务器维护输入事件缓冲/短回滚重放，按 target 应用。
  - C. 不引入客户端 lag，只补 `syncTimeToStep`；行为更接近当前实现，但不满足原版 lag 语义。
  - **结果：A 已实测，不采用；B 已做原型。** A 在 50ms 下 `rejected_inputs=66/324`。B 开启 `late_input_replay` 后，0/50/150ms 均 `corrections=0`、`desyncs=0`、`rejected_inputs=0`，但 rollback 仍高，且客户端 lag 偏移尚未稳定到 8。后续 Phase 1.4 完成后曾改为默认开启，v1.68 起关闭。
- [x] **DECISION 2：协议版本**
  - 已升 v8：`LoginResponse`/`MatchStart`/`SyncResponse` 增加 `sync_time_to_step_ms`（`int16`）。
  - 明确旧客户端/旧服务器拒绝策略：版本不匹配直接拒绝登录。
  - **结果：v8 已落地；字段当前为诊断/对齐输入，服务器默认 0，尚未驱动 stepTime。**
- [x] **DECISION 3：对齐失败回退**
  - 超时后回退到当前“快照追帧”模式，还是失败断开？
  - **结果：当时默认关闭 `client_step_lag`；Phase 1.4 验证后曾默认开启；v1.68 起生产客户端默认关闭，保留 `--client-lag` 实验开关。**

### 1.3 客户端对齐

涉及：`src/net/real_client.h/.cpp`、`src/net/local_net.h/.cpp`。

- [x] `handle_match_start`/`handle_login_response` 记录开局快照 tick `S`。
- [x] `begin_alignment` 在 `client_step_lag=true` 时设为未完成，目标 `S + server_lag`；对齐期间继续 pump/收包，但不推进 `LocalClient`。
- [x] `alignment_wait_ticks` 写入 stats/HUD；超时日志已保留。
- [x] 对齐完成后本地目标与 `estimated_server_tick` 稳定保持约 `effective_lag`；scheduler 实测目标 lag 0–3 tick。
- [x] `catch_up_lag` 在 `client_step_lag=true` 时追到 `highest_server_tick_ - server_lag`。
- [ ] 重连/Rematch 重新对齐；Lobby→Match 清理 queue、predicted、alignment 状态尚未专项验证。
- [x] 对齐期间不发送本地输入；完成后按 normal/recovery lead 发送。

### Phase 1 spike 结果

- 新增实验开关 `NetClientConfig::client_step_lag` / `nsync_net_real --client-lag`（库和在线客户端默认关闭，显式 `--client-lag` 开启）。
- A（仅客户端 lag，无服务器回放）：50ms `rejected_inputs=66/324`，不可用。
- B（服务器迟到输入回放，`late_input_replay`）：0/50/150ms 均 `corrections=0`、`desyncs=0`、`rejected_inputs=0`。一次 50ms 实测 `rollbacks≈146`、`snapshot_mismatches≈93`；一次 150ms 实测 `rollbacks≈138–142`。回放次数很高（约等于每个输入一次），但无权威 desync。
- 结论：Phase 1.4 `syncTimeToStep`/step scheduler 补齐后曾切换生产默认，v1.68 起默认关闭；50ms 仍保留约 2.5% rollback，继续作为后续优化项。

### 1.4 `syncTimeToStep`

状态：**已完成（原版 lag 实验开关；在线客户端默认关闭，`nsync_net_real` 仍显式开启）**。服务器默认写 0；客户端把毫秒换算成 tick：`effective_lag = server_lag - sync_ms * tick_rate / 1000`，用最后快照 tick + 接收后经过时间（上限 `snapshot_interval + RTT + 2` tick）+ `RTT/2` 估算服务器当前 tick，输出 `STS/ELAG/RTTC/EST`。`NetClientConfig::fixed_step_scheduler` / `--fixed-step-scheduler` 每 tick 以 `estimated_server_tick - effective_lag` 为目标调度 0–4 个本地 step，并记录 `steps/skips/debt/target`；50ms 下 STS +100/-100ms 实测 `ELAG=2/14`，本地目标 lag 稳定在 0–3 tick，`corrections/desync=0`。

涉及：`src/net/protocol.h/.cpp`、`src/net/real_client.h/.cpp`、`src/net/real_server.cpp`、`src/net/server_clock.*`。

- [x] 协议结构增加 `sync_time_to_step_ms` 字段并升级到 v8。
- [x] 服务器在 `LoginResponse`、`MatchStart`、`SyncResponse` 写入当前值；新增 `--sync-time-to-step-ms` 和 `server.sync_time_to_step_ms`。
- [x] 客户端用 tick_rate + `sync_time_to_step` 计算有效 server lag；`effective_server_lag_ticks` 参与 `begin_alignment`、超时和 `catch_up_lag`。
- [x] 增加 `nsync_net_sync_time_to_step` 和 `effective_server_lag_ticks` 单元断言（+100ms→lag2、-100ms→lag14）。
- [x] 当前 tick-driven 架构下实现 step 调度/accumulator，让 STS 直接校正物理步进；`nsync_net_real_process_step_scheduler` 覆盖。
- [x] 记录 `sync_time_to_step`/有效 lag 到 HUD/日志；`clock_offset_ms` 已有。
- [x] RTT/2 补偿快照传输时延：`rtt_compensation_ticks = rtt * tick_rate / 2000`，输出 `RTTC`；50/150ms 实测 `RTTC=2/5`。
- [x] `estimated_server_tick` 用最后快照 tick、接收后经过时间（有上限）和 RTT/2 估算服务器当前 tick，作为 catch-up/scheduler 目标。
- [x] `server_time` + RTT 驱动的 stepTime 误差闭环：接收时间外推有上限，避免时钟偏移导致目标漂移。

### 1.5 服务器迟到输入

状态：**已启用（生产服务器默认开启，`NetServerConfig` 库默认仍关闭）**。`NetServerConfig::late_input_replay` / `--late-input-replay` 开启后，从最近快照恢复并重放 `input_history` 到当前 tick；`--late-input-replay-window` 控制窗口。

涉及：`src/net/real_server.h/.cpp`。

- [x] 明确窗口 `late_input_replay_window_ticks`（默认 120）。
- [x] 统计 `late_replays`、`late_replay_batches`、`late_replay_failures`、`late_inputs`（clamp）、`rejected_inputs`。
- [x] 服务器按 tick 保存 `input_events` / `input_history`，支持从快照恢复并重放。
- [x] 回放后重建 snapshot history，避免 SyncResponse 命中旧状态。
- [x] 同一 tick 前到达的迟到输入先入队，下一次 `simulate_tick` **一次 restore + 重放多个事件**；50ms 实测 212 个输入 → 148 个回放批次（约 -30%）。
- [x] 回放失败时恢复 `input_events`、所有 session 的 `input_history`/`input_state` 和 world 快照。
- [x] `LocalClient` 新增远端输入排队：`RealClient` 在 pump 内先 `queue_input_broadcast`，结束时 `flush_pending_rollbacks`，同一批输入只回滚到最早 target 一次。
- [x] 丢弃 target `<= last_server_tick_` 的远端输入（`CIN`）：该状态已被最新权威快照覆盖，不再回滚；50ms 实测 `CIN=98`、rollbacks=0，150ms `CIN=94`、rollbacks=0。
- [x] 10 玩家真实压力回归：新增 `nsync_lobby_5v5_client_lag`；50ms 10 真人实测 `rejected_inputs=0`、`late_replay_failures=0`、`post_match=20`、`corrections=0`、`desyncs=0`、rollbacks=3。

### 1.6 Phase 1 验收

状态：**Phase 1 历史显式配置矩阵**。10 人 600 tick、`client-lag` + `fixed-step-scheduler` + 服务器回放：0/50/150ms 的 `abs(server-local-lag)` 分别为 1/0/2 tick，`rejected_inputs=0`、`late_replay_failures=0`、`corrections=0`、`desyncs=0`；rollbacks 分别为 0/15/1，snapshot mismatch 仍存在。

- [x] 对齐完成后 `abs(server_tick - local_tick - server_lag) <= 2`：0/50/150ms 实测 1/0/2 tick。
- [x] 0/50/150ms 模拟 RTT：`corrections=0`、`desyncs=0`。
- [x] 目标 `rejected_inputs=0`；2 客户端矩阵已满足，若保留 clamp 近似仍需记录阈值。
- [x] rollbacks 相比当前基线明显下降；B+CIN 在 50/150ms 实测为 0。
- [x] reconnect、rematch、lobby→match 对齐正确；`nsync_lobby_reconnect/rematch` 通过。
- [x] golden replay、快照版本、content fingerprint 不变。
- [x] 20+ CTest 全过（35/35）。

### 1.6.1 原版 lag 实验开关

- 在线客户端 `ClientOptions` 的 `client_lag` / `fixed_step_scheduler` 默认保持关闭；需要原版 lag 实验时显式加 `--client-lag --fixed-step-scheduler`，配置项 `client.client_step_lag`、`client.fixed_step_scheduler` 默认 false。
- `nsync_server` 的 `server.late_input_replay` 默认改为 true，新增 `--no-late-input-replay`；`NetServerConfig` 库默认仍为 false，避免测试工具隐式改变。
- 评估矩阵：10 人 600 tick、服务器回放开启，0/50/150ms 均 `rejected_inputs=0`、`late_replay_failures=0`、`corrections=0`、`desyncs=0`；rollbacks 0/15/1。
- 风险：50ms 仍有约 2.5% tick 发生 rollback；周期 snapshot mismatch 已归因为 input lead（smm=0、sma=0），late-input replay 的历史快照追溯修正/ack mismatch 仍待办；真实公网丢包 trace/延迟尖峰仍未覆盖。
### 1.7 风险与回退

- 客户端 lag 会让输入 target 早于服务器 tick；没有服务器回放时，近似方案的预测误差可能上升。
- `syncTimeToStep` 与当前固定 60Hz step 调度可能冲突；先 spike，不直接改默认。
- 如果 Phase 1 导致手感/回滚恶化，保留 `original` 开关并回退到当前快照追帧路径，不阻断发布。

---

## Phase 2：原版 `ClientEvent` 动作事件（P1）

目标：把当前“按钮/瞄准状态变化事件”升级为原版动作事件。协议 v9 的 wire 字段、客户端边沿采集、服务器队列和事件驱动应用（含迟到回放）已完成；原始状态保留用于瞄准和 fallback。

原版枚举（反编译 `com.pragmatix.df.game.ClientEvent`）：

```text
GO_LEFT, GO_RIGHT, GO_UP, GO_DOWN,
WEAPON_ENABLE, WEAPON_FIRE, WEAPON_RELOAD,
ACTIVATE_GADGET, EXIT, DROP_FLAG, WEAPON_CHARGE,
RESPAWN, WEAPON_REDIRECT, DISCONNECT, RECONNECT
```

Phase 2.1 状态（已完成）：协议 v9 的输入消息携带 `actions/action_toggles/action_param`，客户端发送按钮边沿事件，服务器统计 `action_packets/action_events/action_toggle_events` 并随 `InputBroadcast` 转发。

Phase 2.2/2.3 状态（已完成）：服务器 `ActionEventQueue` 按 `(target_tick, sequence, action_mask)` 去重、按 target tick 排序合并；`simulate_tick` 消费当前 tick frame，校验动作事件与状态按钮一致，记录 `action_mismatches`。非回放路径用 session toggle 状态和一帧 pulse 重建按钮位；迟到回放路径在 `replay_late_inputs` 中按 target tick 套用动作帧；原始状态保留为缺少动作帧/回退路径。

任务：

- [x] 定义 wire 事件结构：`InputMessage.actions`（15 位 `ClientActionMask`）、`action_toggles`、`action_param`；沿用输入 `sequence` 和 `tick` 作为去重/目标 tick。
- [x] 客户端从输入采集边沿事件：移动/跳跃/下蹲/开火 toggle，换弹/ gadget 按下事件；当前仍附带状态按钮。
- [x] 服务器事件队列：按 slot/sequence/target_tick 去重、排序；非回放和迟到回放路径均已应用。
- [x] 与现有 `InputCommand` 的适配层；状态输入保留为 fallback，本地 bots/legacy 不变。
- [x] 队列单元测试覆盖重复、乱序、清理；KCP 重传实测 `duplicates>0` 且被去重。
- [x] 开火/换弹/跳跃/移动/技能行为与当前状态模型对照；0ms 2p `mismatches=0`、`driven>0`；client-lag 回放路径 `late_driven>0`。
- [x] 协议模糊测试通过；重连/Rematch 回归仍由现有 CTest 覆盖。
- [x] 保留状态输入 fallback；协议 v9 同时携带状态与动作事件。

验收：

- [x] 动作事件在本地和在线表现一致（0ms/50ms client-lag 回归）。
- [x] 不改变 golden replay/物理确定性（`nsync_replay_golden` 通过）。
- [x] 35 项 CTest 全过；队列/协议/同步专项测试通过。

风险：

- 当前核心是状态驱动，动作事件需要适配层；直接改核心风险高。
- `toggle`/按住类动作的释放语义容易出错，必须有专项测试。

---

## Phase 3：真实环境验证（P3）

目标：把 loopback 结论扩展到真实进程和网络。

Phase 3.1/3.2/3.3/3.4 状态（已完成）：`nsync_net_real_process` 覆盖 2 client/50ms；`nsync_net_real_process_10p_150` 覆盖 10 client/150ms；`nsync_net_real_process_multi_match` 覆盖 0ms + 2 场 Rematch；`nsync_soak_10m` 覆盖 10 分钟模拟时间；`scripts/real_process_soak.ps1` 完成真实 10 client/0ms/612s soak，16 场 Rematch，无 corrections/desync，内存/句柄在启动后保持平台；新增 2 client/250ms、2 client/400ms WAN（400ms 修复重复登录泄漏 slot）和 250ms + jitter/loss/duplicate 测试；10 人 250ms/612s soak 稳态无增长、17 场 Rematch、无 corrections/desync；10 场并发 LocalSession 10 分钟 Release 手动通过，CTest 使用 1 分钟回归；10 个独立 server + 20 个真实 client 的 150ms 并发对局通过；真实客户端 1280x720/1920x1080 no-vsync 平均约 1685/1849 FPS，对局内上下行约 7–16/38–111 kbps；Intel UHD 630 下 720p 约 190–224 FPS、1080p 约 164 FPS；250ms + 30ms jitter + 3% loss + 4 包 burst + 5% duplicate 的 10 人/314s soak 完成 9 场 Rematch、无 corrections/desync；同配置叠加每 2s 300ms 延迟尖峰仍 9 场 Rematch、无 corrections/desync，spike≈8.8–9.3k；300s WAN 时间轴 trace 的 10 人/316s soak 完成 8 场 Rematch、trace≈72k 包、无 corrections/desync；World1 原始地图已转换为 `world1.json`（546 border AABB、15 平台、9 jump pad、10 出生点）并设为默认，移动属性按 `environment.bytes` 的表格换算，jump pad 已接入物理；late replay 重写历史后的旧 ACK 单独计 `rewritten_ack_mismatches`，50/250ms 实测 `ack_mismatches=0`。Debug/Release 40/40。

任务：

- [x] 真实双进程：server + 2 clients，50ms 模拟 RTT（Phase 3.1）。
- [x] 真实多进程：10 clients，150ms；0ms + 2 场 Rematch 真实进程。
- [x] 10 分钟模拟 soak（`nsync_soak_10m`）；真实进程 10 client/0ms/612s soak，稳态无内存/句柄增长。
- [x] 连续 Rematch：真实 soak 内 16 场（match id），脚本最低要求 2 场。
- [x] 250ms/400ms WAN、10 人 250ms/612s soak、250ms + 30ms jitter + 5% loss + 10% duplicate 的 10 人/612s soak；10 场真实并发叠加 WAN 模拟也通过。连续丢包 burst（3%/4 包/5% duplicate）10 人/314s soak 通过。
- [x] `nsync_net_real --client --map` 连接 assets 地图服务器。
- [x] 10 人快照/广播耗时已输出（`snap_avg`、`snap_gen_us`、`snap_us`）；真实客户端 FPS/带宽曲线已测（`scripts/client_perf.ps1`）。
- [x] 把结果写入 `docs/PHASE5_RESULTS.md`。
- [x] 10 个独立 server + 20 个真实 client、150ms、900 tick 并发对局（`nsync_net_real_process_10_matches`）。

验收：

- [x] 无持续 desync、无内存/队列增长、无输入卡死（10 分钟真实 soak）。
- [x] 0/50/150ms 达标（`abs(server-local-lag)` 1/0/2）；250/400ms 作为 WAN 观察项。
- [x] 10 分钟真实 soak + 10 场并发 LocalSession 报告可复现（CTest 并发回归为 1 分钟）。

---

## Phase 4：文档、发布与 CI

- [x] 更新 `README.md`、`docs/INPUT_SYNC_MODEL_PLAN.md`、`docs/TODO.md`、`docs/PHASE5_RESULTS.md`。
- [x] 版本记录按阶段递增，记录近似/回退方案。
- [x] Debug/Release 全量 CTest（40/40）。
- [x] 重打 `dist/nsync-0.1.0-win64.zip`，更新 SHA256 和包内 README。
- [x] 验证包内 client `--help`、server 5 tick、manifest。
- [x] CI 更新测试数量/超时；上传 artifact，并增加包内 client/server/manifest smoke。

---

## 5. 总验收矩阵

| 项目 | 目标 |
|---|---|
| 传输层 | 不纳入本计划 |
| 0/50/150ms corrections | 0 |
| 0/50/150ms desyncs | 0 |
| rejected inputs | 0（或明确记录 clamp 近似） |
| serverLag 偏移 | 稳态误差 ≤2 tick |
| syncTimeToStep | HUD/日志可见，重连恢复 |
| action events | 与状态模型行为一致 |
| 真实多进程 soak | 10 分钟无异常 |
| CTest | 全量通过，数量随阶段更新 |
| 发布包 | 可解压、可启动、哈希匹配 |

## 6. 明确不做

- 不改 TCP/WebSocket vs KCP/UDP 传输层。
- 不承诺原版二进制协议互通。
- 不在没有服务器回滚设计的情况下，强行实现“完整原版迟到输入回放”。
- 不为了通过延迟测试隐藏真实 snapshot mismatch；统计必须可解释。

## 7. 建议的第一刀

如果用户确认开始，建议按以下顺序落地：

1. Phase 0.1 统计口径 + Phase 0.2 测地图 fingerprint（低风险，先建立可信基线）。
2. Phase 1.2 决策门禁：确定迟到输入策略和 `syncTimeToStep` 的 tick-driven 映射。
3. 只做 Phase 1.3 客户端对齐 spike，先在 0/50/150ms 观察 rollback/rejected，不切默认。
4. 根据 spike 结果决定 Phase 1.4/1.5 是否进入精确服务器回放。

在收到“开始”前，不修改功能代码。
