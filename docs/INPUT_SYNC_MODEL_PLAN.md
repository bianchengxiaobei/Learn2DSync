# 原版输入同步完整模型实施计划

状态：**已实现并默认启用 `original`**（2026-10-31）。`legacy` 路径保留用于回退。  
目标：把原“客户端本地 `input_delay=3`”的简化实现，替换为反编译版 `BattleMaster` 的服务器下发时间参数 + 输入帧调度模型。

实现摘要：

- 协议 v9 下发 `SyncTiming{server_lag_ticks, client_event_lag_ticks, sync_period_ticks}`、`sync_time_to_step_ms`，输入消息附带 `ClientActionMask`/toggle/param，服务器按 target tick 去重排序；非回放路径用 session toggle 状态/pulse，迟到回放路径按命令 target 套用动作帧重建按钮位；状态输入保留为 fallback。
- 普通输入 lead=`client_event_lag=3` tick；只有 sync/desync 恢复后的下一批输入使用完整公式 `server_lag + client_event_lag + client_event_lag/2 + 1 = 13` tick。
- 客户端不再开局等待 `serverLag`；使用超前快照队列和追帧快照校正。
- 输入改为事件式：只在按钮/瞄准状态变化时发送，服务器按目标 tick 持久化输入状态；远端迟到事件回滚重放，迟到服务器输入进入下一 tick 窗口。
- 服务器保留 120 个广播快照，按 client ack/sync 请求的 tick 回发。
- HUD/日志增加 `SLAG/ELAG/CELAG/SYNC/STS/LEAD/Q`；真实 UDP loopback 2 客户端 300 tick 在 0/50/150ms 客户端 KCP 模拟 RTT 下 `corrections=0`、`desyncs=0`；0ms `rejected_inputs=0`，50/150ms 允许迟到输入拒绝并记录 `rollbacks`。

> 说明：本文第 2 节及后续部分保留了最初“始终使用完整公式”的实施计划，属于历史记录；实际实现已按反编译 `BattleMaster.cs:222` 的 `frameOnDesync` 分支修正，普通路径为 `CurrentFrame + clientEventLag`。

## 1. 现状与差距

当前实现（v1.26）：

- `client.input_delay_ticks` / `--input-delay-ticks` 默认 3，客户端自己决定输入提前量。
- 登录时把 `input_delay` 发给服务器，`RealServer` 只做校验/回传，不参与排帧决策。
- `LocalClient::step` 用 `world.tick() + input_delay` 生成输入目标帧，客户端世界基本跟随服务器 tick，没有 `serverLag` 偏移。
- 协议中没有 `serverLag`、`clientEventLag`、`syncPeriod`。
- 双客户端 `corrections=0`，但行为只是“输入晚 3 tick”，不是原版模型。

原版行为（反编译）：

- `MultiPlayerGame.cs` 从 `StartGameNotification` / `ConnectToGameNotification` 取得 `serverLag`、`syncPeriod`，在线 PvP 的 `clientEventLag` 固定为 3。
- `BattleMaster.cs:222` 在线输入目标帧：
  ```text
  CurrentFrame + serverLag + clientEventLag + clientEventLag / 2 + 1
  ```
  按在线 `clientEventLag=3` 计算，实际为 `CurrentFrame + serverLag + 5`。
- `BattleMaster.cs` 用 `serverLag` 修正客户端的 `stepTime`，客户端模拟相对服务器有时间偏移。
- `syncPeriod` 控制快照哈希/同步节奏。
- 参考：`Assets.Script.Core.Scene/BattleMaster.cs`、`MultiPlayerGame.cs`、`StartGameNotification`、`ConnectToGameNotification`。

结论：当前实现**没有完整复刻**原版；本次改造要替换掉“客户端自己定 3 tick”的临时方案。

## 2. 目标模型

服务器是时间参数的唯一权威：

```text
server_lag_ticks       服务器指定的客户端模拟落后帧数
client_event_lag_ticks 在线 PvP 默认 3
sync_period_ticks      快照哈希/同步周期
```

客户端按原版公式计算输入目标帧：

```text
target_frame =
    local_frame
  + server_lag_ticks
  + client_event_lag_ticks
  + client_event_lag_ticks / 2   // 整数除法
  + 1
```

在线 PvP、`client_event_lag=3` 时：

```text
target_frame = local_frame + server_lag_ticks + 5
```

客户端需要在首次进入对局时建立与服务器的 `server_lag` 偏移，并缓存超前快照。不能只把 `input_delay` 调大，否则只是增加延迟，不是原版模型。

## 3. 实施步骤

### 步骤 0：文档与基线（本步已完成）

- [x] 新增本计划文档。
- [x] 在 `docs/TODO.md` 插入 Phase 10。
- [x] 在 `docs/PHASE5_RESULTS.md` 标注当前为简化实现。
- [x] 在 `README.md` 网络章节标注完整模型实施状态。
- [x] 记录当前基线：双客户端 `input_delay=3`、`corrections=0`；后续对照。

退出条件：文档能明确区分“当前实现”和“目标模型”，后续改动有验收依据。

### 步骤 1：定义时间契约和特性开关

改动：

- `src/net/protocol.h`
  - 新增 `struct SyncTiming`：
    - `std::uint16_t server_lag_ticks`
    - `std::uint16_t client_event_lag_ticks`
    - `std::uint16_t sync_period_ticks`
  - `MatchStart`、`LoginResponse` 各增加 `SyncTiming` 字段。
- `src/net/real_server.h` / `src/net/real_client.h`
  - 服务端保存每场 `timing_`。
  - 客户端保存服务器下发的 `timing_`。
- 配置/CLI：
  - `server.server_lag_ticks`、`server.client_event_lag_ticks`、`server.sync_period_ticks`。
  - 服务端 CLI：`--server-lag-ticks`、`--client-event-lag-ticks`、`--sync-period-ticks`。
  - `client.input_delay_ticks` 保留为**调试覆盖**，在线时不能覆盖服务器权威值。
- 特性开关：
  - `server.input_sync_model=legacy|original`，默认 `legacy`，完成验收后切 `original`。
  - 开关只影响输入排帧和客户端偏移；不影响快照格式/世界逻辑。

测试：

- 配置解析边界：0、1、3、60、负数、溢出。
- `SyncTiming` 编解码往返。
- 旧 `input_delay` 仍能解析，避免本地工具立即失效。

退出条件：协议字段和配置可读可写；legacy 行为不变；现有 CTest 全过。

### 步骤 2：协议版本与编解码

改动：

- `kProtocolVersion` 从 6 升到 8（v8 增加 `sync_time_to_step_ms`），再升到 9（v9 增加动作事件位）。
- `encode_match_start` / `decode_match_start`、`encode_login_response` / `decode_login_response` 增加三个字段，固定顺序和宽度。
- 解码校验：
  - `server_lag_ticks <= 60`
  - `client_event_lag_ticks <= 10`
  - `sync_period_ticks` 在 1..240
  - 不允许截断/越界。
- `tests/test_net.cpp`：
  - 完整往返。
  - 截断、追加、长度字段变异、随机输入。
  - 版本 6 包被拒绝（协议不兼容）。

退出条件：协议模糊测试全部通过；服务端/客户端对旧版本明确报错。

### 步骤 3：服务器下发与输入校验

改动：

- `RealServer::start` 校验并保存 `SyncTiming`。
- `start_match` / `send_match_start` 写入 timing。
- 重连登录时，`handle_login` 的 `LoginResponse` 也写入当前 timing，保证断线重连仍能恢复。
- `handle_input`：
  - 保留 slot/button/tick/限流校验。
  - 将合法窗口改为 `server_tick .. server_tick + max_input_ahead_ticks`。
  - `server_lag + client_event_lag + client_event_lag/2 + 1` 必须落在窗口内；启动时校验配置。
  - 日志输出 `server_tick/target_tick/slot/server_lag/client_event_lag`。
- `NetServerConfig` 增加配置默认值；先建议：
  ```text
  server_lag_ticks = 8
  client_event_lag_ticks = 3
  sync_period_ticks = 30
  ```
  这是可调基线；反编译只能确认 `clientEventLag=3`（在线 PvP），`serverLag` 的实际服务器值无法从现有反编译资源确定。

退出条件：服务器日志能看到同一组 timing；输入目标帧在窗口内；reconnect 后 timing 不变。

### 步骤 4：客户端使用服务器时间参数

改动：

- `RealClient` 登录/开局时保存 `SyncTiming`。
- 在线输入目标帧改由公式计算，不再使用客户端默认 3：
  ```text
  lead = server_lag + client_event_lag + client_event_lag / 2 + 1
  target = local_tick + lead
  ```
- `LocalClient` 增加 `input_lead_ticks` 概念（可复用 `input_delay_ticks`，但语义改为“由服务器 timing 计算出的 lead”）。
- `--input-delay-ticks` 只作为调试覆盖；日志明确 `source=server|override`。
- HUD/日志显示：
  - `SLAG`、`ELAG`、`CELAG`、`SYNC`
  - `LEAD`
  - `LOCAL/SERVER/ACK`

测试：

- 纯函数测试：`current=100, serverLag=8, clientEventLag=3 -> lead=13, target=113`。
- 奇偶 `clientEventLag` 的整数除法测试。
- 真实 UDP 双客户端：日志 timing 相同；`input_delay` 不再固定 3。

退出条件：客户端不再自己决定在线 lead；服务端 timing 变化能反映到 target。

### 步骤 5：实现 `serverLag` 客户端时间偏移

这是完整模型的关键；只做步骤 4 会得到错误的额外延迟。

目标：

- 客户端本地模拟相对服务器落后 `server_lag_ticks`。
- 输入公式仍使用原版完整形式；因为 `local_frame` 已落后，target 相对服务器才是 `server_tick + client_event_lag + ...`。

实施：

- `RealClient::handle_match_start`：
  - 记录开局快照 tick `S`。
  - 进入 `aligning` 状态，客户端保持渲染/网络接收，但 `LocalClient` 暂不推进。
  - 等待 `server_lag_ticks` 个服务器 tick（由 `ServerClock` 或服务器 tick 估计），再开始本地 step。
- `LocalClient`：
  - 增加 `start_delay_ticks` / `paused` 支持，避免开局立即推进。
- `RealClient` 快照处理：
  - 对 `snapshot.tick > local_tick` 的快照进入 `future_snapshots_` 队列，不直接 `world.restore`。
  - 本地 tick 追到该 tick 时再应用/校验。
  - 队列按 tick 排序、去重、限制长度（建议 `server_lag + 8` 个快照），超限丢弃最旧并记录。
- `LocalClient::apply_snapshot`：
  - 保留“服务器领先时”兼容路径，但 original 模型使用队列路径。
- 输入：
  - 对齐期间不接受/发送本地输入（或缓存到对齐结束）；避免 target 早于服务器 tick。
  - 对齐结束后按公式发送。
- 重连：
  - 重连登录拿到当前快照后，按同样 `server_lag` 重新对齐；不能直接“服务器领先就 restore”。

测试/验收：

- 0ms、50ms、150ms 模拟 RTT 双客户端：
  - `corrections=0`、`desyncs=0`。
  - 客户端 `local_tick` 比服务器 tick 小约 `server_lag`。
  - 输入 target 落在服务器可接受窗口。
- 开局快照不出现“直接跳到服务器当前 tick”的情况。
- 快照队列不无限增长。

风险：这是最容易引入卡顿/不同步的一步；未通过双客户端测试前不切默认模型。

### 步骤 6：回滚、Ack、Sync 适配

改动：

- `SnapshotAck`：
  - `client_tick` 是落后的本地 tick；服务端只接受合理范围，不把它当作服务器当前 tick。
- `SyncRequest`：
  - 在线目标的 sync tick 改为原版语义：本地 tick 向前 `server_lag + 2` 帧附近的已确认快照。
  - 保持 `sync_period_ticks` 节奏，不再只依赖 `sync_interval_ms`。
- 回滚：
  - 快照落后于本地时，恢复并重放本地输入；队列中的未来快照不能参与回滚。
  - 记录 `rollbacks`、`max_rollback_ticks`。
- 重放结束仍保持 `local_tick = server_tick - server_lag` 的相对偏移。

测试：

- 人工 desync 注入后能恢复，且偏移不被破坏。
- 高频丢包/重传下不出现永久不同步。
- `sync_period_ticks` 改变时，hash/sync 请求节奏随之变化。

### 步骤 7：重连、Rematch、Lobby 边界

- 重连：
  - `LoginResponse` 带 timing；客户端重新对齐。
  - 服务端保留 session 时保持同一 match timing。
- Rematch：
  - 新 match 重新生成/下发 timing。
  - 客户端清除上一场快照队列、对齐状态和输入命令。
- Lobby → Match：
  - 只在对局开始时启用 original 模型；房间阶段不推进本地世界。
- 旧流程：
  - `--no-lobby` 和本地 `LocalSession` 继续走 legacy，避免影响已有测试工具。

测试：

- 对局中断线、token 重连、继续跑 300 ticks。
- Rematch 连续两场，timing/queue/prediction 都重置。
- `nsync_net_real`/`nsync_lobby_*` 回归。

### 步骤 8：诊断、HUD、测试入口

- 服务器日志：
  - match start/reconnect 输出 timing。
  - 每个拒绝输入输出 tick 窗口。
- 客户端日志/HUD：
  - `SLAG/ELAG/CELAG/SYNC/LEAD`
  - `LOCAL/SERVER/ACK`
  - `corrections/rollbacks`
  - `snapshots_queued`
- 新增测试开关：
  - `--input-sync-model legacy|original`
  - 固定 timing 测试入口，不依赖手工改配置。
- 测试矩阵：
  - `nsync_core_tests`：公式、配置、协议、队列。
  - `nsync_net_loopback` + `nsync_net_latency_50/150`：0/50/150ms，2 客户端；客户端 KCP 双向延迟 RTT/2。
  - 真实 UDP 两进程：验证服务器下发值、输入接受、corrections=0。
  - 重连/Rematch。
  - 现有 40 项 CTest（含 0/50/150ms 延迟矩阵和 `arena_02` 地图 smoke）。

### 步骤 9：切换默认与文档收尾

- `server.input_sync_model` 默认从 `legacy` 切到 `original`，仅在验收全部通过后执行。
- 保留 `client.input_delay_ticks` 作为本地调试/回退，不再作为在线权威参数。
- 更新：
  - `docs/TODO.md` Phase 10 勾选状态。
  - `docs/PHASE5_RESULTS.md` 实现/验证结果。
  - `docs/INPUT_SYNC_MODEL_PLAN.md` 完成记录。
  - `README.md` 网络说明和启动参数。
- 更新版本记录；协议 v9，快照升到 v7（技能槽状态）。
- 原版 lag 实验开关：在线客户端可用 `--client-lag` + `--fixed-step-scheduler` 开启，`nsync_server` 启用 late-input replay；v1.68 起默认关闭客户端 lag/scheduler；v1.69 改为按快照目标 tick 比较预测、服务器接受当前 tick 输入、迟到远端输入顺延到下一 tick，并只补本地模拟步以避免路径重复回拉。10 人 0/50/150ms 实测 `abs(server-local-lag)=1/0/2`、`rejected=0`、`corrections/desync=0`。Debug/Release CTest、真实 UDP smoke、打包验证均通过；包 3.36 MiB，SHA256 `51FF9B6F687CB562243DAF9D0274CB341B7F792944CE0D066EC396B11C6EB811`。

## 4. 验收标准

- [x] 在线客户端不再硬编码 3/16 tick；输入 lead 由服务器下发的 `server_lag_ticks`、`client_event_lag_ticks` 计算。
- [x] 普通路径严格为 `local + client_event_lag`；仅同步/desync 恢复路径使用 `local + server_lag + client_event_lag + client_event_lag/2 + 1`。
- [x] 输入改为状态变化事件，服务器按目标 tick 持久化；迟到远端事件回滚重放，迟到服务器输入进入下一 tick 窗口。
- [x] 客户端使用快照对齐和追帧；不再强制开局等待 `server_lag`。
- [x] 2 客户端在 0/50/150ms 模拟 RTT 下 `corrections=0`、`desyncs=0`（客户端 `KcpTransport` 双向延迟 RTT/2；50/150ms 仍会出现预测 rollback，属迟到事件/快照校正）。
- [x] 真实 UDP 双客户端服务器/客户端 timing 日志一致，`rejected_inputs=0`。
- [x] 断线重连和 Rematch 后 timing/偏移正确。
- [x] 协议 v9 模糊测试通过；现有 40 项 CTest 全过。
- [x] 快照版本仍为 6，content fingerprint 不变，golden replay 不因网络改造变化。
- [x] 保留原版换弹行为：打空自动换弹 + `R` 手动换弹。

## 5. 风险与回退

| 风险 | 影响 | 应对 |
|---|---|---|
| `serverLag` 原版实际值未知 | 无法精确复刻手感 | 服务器配置化；用 8/3/30 基线测试，保留调整入口 |
| 客户端落后导致视图/snapshot 队列复杂 | 卡顿、跳帧、纠正 | 先做对齐 spike；未通过不切默认 |
| 输入 target 超过服务器窗口 | 输入被拒、角色不动 | 启动时校验 `lead < max_input_ahead`；日志明确拒绝原因 |
| 落后快照缓存增长 | 内存/延迟 | 限制队列长度；按 tick 去重 |
| 协议 v9 不兼容旧包 | 旧客户端/服务器不能混用 | 登录明确报版本不匹配；重新打包 |
| 用户觉得比 3 tick 更延迟 | 手感回退 | 这是原版模型的行为；用遥测记录 lead，不用客户端硬调掩盖 |

回退：

- 保留 `server.input_sync_model=legacy` 和 `client.input_delay_ticks`。
- 默认切换前不删除旧路径。
- 失败时仅回退网络模型，不改快照/世界/golden。

## 6. 暂不做

- 不改快照格式、不引入 delta/压缩。
- 不改 KCP/传输层。
- 不改换弹规则（当前自动 + 手动已对齐原版默认）。
- 不做原版 TCP 协议兼容。
- 真实 wall-clock soak、10 场真实 UDP 并发（含 WAN 模拟）、10 分钟丢包 soak 和客户端 FPS/带宽已补测；低端集显已用 Intel UHD 630 补测；周期 snapshot mismatch 已细分归因为 input lead（smm=0/sma=0），late-input replay 历史修正/ack mismatch 与真实公网丢包分布仍另列验收。

## 7. 待确认

1. 原版服务器实际 `serverLag` 默认值；目前只能配置化，建议先用 8。
2. 是否需要在客户端 HUD 直接显示 `SLAG/CELAG`，还是只写日志。
3. 低 RTT 本地测试是否保留 `legacy` 快速路径；建议保留，避免开发工具过慢。






- [x] Phase 1.4 --fixed-step-scheduler：按 estimated_server_tick - effective_lag 调度 0–4 个本地 step；STS +100/-100ms 实测 ELAG=2/14，目标差 0–3 tick；真实 2 client/50ms corrections/desync/rollback=0，新增 CTest 后 35/35。
