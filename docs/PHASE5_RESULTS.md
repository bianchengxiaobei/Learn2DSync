# Phase 5：真实网络结果

## 目标

用真实 UDP/KCP 连接两个客户端和一个权威服务器，验证登录、会话、输入、快照、时钟同步、断线和重连。

## 参考反编译源码

Phase 5 没有复制原版 TCP 序列化格式，而是保留原版的行为语义，用项目已选定的 KCP over UDP 实现传输。对应关系：

- `reference/nsync/decompiled/Assets.Script.Core.Scene/BattleMaster.cs`
  - 原版 `serverLag`、`clientEventLag`、`syncPeriod`。
  - 每个 tick 保存完整快照，定期计算 hash，并通过 `SyncGameRequest` / `SyncGameResponse` 修复不同步。
  - 我们对应实现为：客户端预测/输入延迟、服务器快照、`SnapshotAck` 和 `SyncRequest` / `SyncResponse`。
- `reference/nsync/decompiled/Assets.Script.Core.Net/ServerTimeKeeper.cs`
  - 原版使用最多 32 个 ping 样本，取低 RTT 样本做线性回归，映射服务器时间。
  - 我们实现 `net::ServerClock`：保存 16 个样本，取低 RTT 样本的偏移中值，提供 `server_to_local` / `local_to_server`。
- `reference/nsync/decompiled/com.pragmatix.df.game/PlayerAction.cs`
  - 原版输入事件包含 frame、event、toggle、params。
  - 我们使用 `InputCommand` 的 tick、buttons、aim、sequence，服务器按 slot 校验后广播。
- `reference/nsync/decompiled/com.pragmatix.df.messages/Messages.cs`
  - 保留原版协议号语义：`PVP_LOGIN_REQUEST=151`、`PVP_LOGIN_RESPONSE=1151`、`PVP_PING=153/1153`、`CLIENT_EVENT_MESSAGE=104`、`CLIENT_EVENT_BROADCAST=1104`、`SYNC_GAME_REQUEST=105`、`SYNC_GAME_RESPONSE=1105`、`SYNC_GAME_NOTIFICATION=2113`。
  - 新增 `SnapshotAck=1106` 作为本项目诊断确认。
- `reference/nsync/decompiled/Assets.Script.Core.Net/ClientConnectionBase.cs`
  - 原版客户端有心跳、重试、TCP 7 字节命令头；本项目改为 KCP 消息边界 + 20 字节应用层包头。
- `reference/nsync/decompiled/Assets.Script.Main.Controllers/ConnectionController.cs`
  - 原版重连有 5 秒倒计时和重登录流程。
  - 本项目使用 session token + KCP 重连，在服务器保留 10 秒 session 窗口。
- `reference/nsync/decompiled/Assets.Script.Core.Scene.Local/PvpLocalLauncher.cs`
  - 原版 PvP 启动和场景初始化流程；本项目服务器负责对局循环，客户端只做预测与渲染。

原版游戏连接实际是自定义 TCP；本项目按设计文档要求改用 KCP，因此线上字节流不兼容原版协议，只保留消息类型、输入/快照/同步语义。

## 实现

### 传输与会话

- `net::ITransport`、`net::KcpTransport`：客户端单会话 KCP over UDP。
- `net::KcpServerTransport`：单 UDP socket 上按 KCP `conv` 管理多个会话。
- KCP 参数：`ikcp_nodelay(1, 10, 2, 1)`、窗口 256、MTU 1200、消息分片。
- `conv` 由客户端随机生成；服务器从首个 UDP 包读取 `conv` 创建会话。
- 网络 I/O 在服务器主循环中 poll，无额外阻塞线程。

### 协议

- 包头：magic `0x4F4E5244`、version、type、session token、sequence、payload size。
- 登录：协议版本、地图 fingerprint、重连 token、期望 slot、input delay。
- 登录响应：accepted、slot、session token、服务器 tick/time、地图 fingerprint、快照 hash、快照字节、错误文本。
- 其他消息：`Input`、`InputBroadcast`、`Snapshot`、`SnapshotAck`、`Ping`、`Pong`、`SyncRequest`、`SyncResponse`、`Disconnect`。
- 所有解码都检查长度、版本、类型、字段范围；非法包被丢弃。

### 服务器

- `net::RealServer` 使用 `LocalServer` 做固定 60Hz 权威模拟。
- 登录分配 slot 和随机 session token；同一 token 可在重连窗口内恢复 slot。
- 校验 token、slot、tick 范围、按钮位和输入延迟；非法输入计入 `rejected_inputs`。
- 输入接受后立即广播 `InputBroadcast`，客户端据此预测远端玩家。
- 按 `snapshot_interval` 下发最新全量快照；客户端忽略旧 tick。
- `SnapshotAck` hash 不一致或 `SyncRequest` 到达时，下发当前 `SyncResponse`。
- 心跳超时/断线后保留 session 10 秒；超时清理 slot。
- 统计包含输入、拒绝、快照、同步、登录、断线、token 拒绝和传输 RTT/重传。

### 客户端

- `net::RealClient` 使用 `KcpTransport` + `LocalClient`。
- 登录成功后在服务器快照上启动预测；输入提前于本地 tick 发送。
- 服务器快照 hash 校验后应用；快照 tick 高于本地时直接校正到服务器状态，不回滚旧预测。
- 心跳 1 秒，时钟同步 1 秒；`ServerClock` 提供 RTT 和服务器时间偏移。
- 自动重连：关闭旧 KCP，生成新 `conv`，携带旧 token 重新登录。
- 可视客户端新增 `--connect`：连接到 `nsync_server --net`，HUD 显示 slot/ack/RTT/rollback/correction/clock offset。

## 验证

构建：

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
```

本机结果：Debug/Release CTest **7/7** 通过，包括新增 `nsync_net_loopback` 和 `nsync_server_net_smoke`。

真实 UDP loopback（同进程服务器 + 2 客户端）：

```powershell
.\nsync_net_real.exe --loopback --ticks 600 --players 2 --input-delay 16
```

结果：两个客户端均连接，`corrections=0`、`desyncs=0`；服务器 tick 与客户端预测一致，输入广播正常。

两个独立进程：

```powershell
.\nsync_server.exe --net --port 40132 --players 2 --ticks 400
.\nsync_net_real.exe --client --port 40132 --ticks 200 --input-delay 16
.\nsync_net_real.exe --client --port 40132 --ticks 200 --input-delay 16
```

结果：两个客户端分别获得 slot 0/1，服务器接受输入且无非法 token；客户端 correction 为 0–1。

重连：

```powershell
.\nsync_net_real.exe --client --port 40133 --ticks 300 --reconnect-at 100 --input-delay 16
```

结果：客户端在 tick 100 主动断开，使用 token 重连成功，恢复 slot 0 并从服务器快照继续模拟。

可视客户端：

```powershell
.\nsync_server.exe --net --port 40147 --players 2
.\nsync_client.exe --connect --server-port 40147
.\nsync_client.exe --connect --server-port 40147
```

结果：两个客户端分别登录 slot 0/1，服务器 `rejected=0`，HUD 显示 online 网络状态。

## 输入同步模型（2026-10-31 已升级）

Phase 5 当时是简化模型：`RealClient` 使用本地 `input_delay` 提前发送输入，客户端世界基本跟随服务器 tick；服务器只校验/回传 `input_delay`。

Phase 10 已替换为原版输入模型：协议 v9 下发 `serverLag=8`、`clientEventLag=3`、`syncPeriod=30`。普通输入目标帧为：

```text
CurrentFrame + clientEventLag
```

只有收到 `SyncResponse`/同步校正后的下一批输入，才使用完整恢复公式：

```text
CurrentFrame + serverLag + clientEventLag + clientEventLag / 2 + 1
```

在线 PvP 普通 lead=3 tick、恢复 lead=13 tick。输入只在按钮/瞄准状态变化时发送，服务器按目标 tick 持久化输入状态；远端迟到事件会回滚重放，迟到服务器输入进入下一 tick 窗口。客户端使用超前快照队列和追帧快照校正，`legacy` 路径保留回退。实现和验收记录见 [`INPUT_SYNC_MODEL_PLAN.md`](INPUT_SYNC_MODEL_PLAN.md) 与 `docs/TODO.md` Phase 10。

2026-10-31 延迟矩阵：`nsync_net_real --loopback` 2 客户端 300 tick，客户端 KCP 双向延迟 RTT/2，0/50/150ms 均 `corrections=0`、`desyncs=0`；50/150ms 会看到预测 rollback，150ms 偶有迟到输入拒绝。Phase 0 补充统计后，0ms 一次实测 `snapshot_mismatches=17`、`snapshot_restores=17`、`rollbacks=0`；50ms 一次实测 `sm=64`、`event_rollbacks=60`、`rollbacks=60`；150ms 一次实测 `sm=72`、`event_rollbacks=116`、`rollbacks=134`、`rejected=0`。Debug/Release CTest 40/40。

## 当前边界（Phase 5 当时）

- 真实网络测试覆盖 loopback/局域网短时运行，尚未做真实网络 10 分钟 soak 和 10 场并发。
- 快照仍是全量、未压缩；还没有 delta、带宽预算和快照独立通道。
- 一个 KCP 会话承载控制、输入、快照和聊天前身；未拆 control/snapshot 通道。
- 没有加密、鉴权服务、作弊检测、速率限制和 NAT 穿透；session token 只用于会话绑定。
- 服务器单进程单对局；未实现大厅/匹配/机器人接管。
- 原版开局、结算和重连 UI 未移植；当前是命令行服务器 + HUD 状态。
- `nsync_client --connect` 仍需要和服务器加载相同地图 JSON；地图 fingerprint 不一致会拒绝登录。

## 关键文件

- `src/net/protocol.h/.cpp`
- `src/net/transport.h`
- `src/net/kcp_transport.h/.cpp`
- `src/net/real_server.h/.cpp`
- `src/net/real_client.h/.cpp`
- `src/net/server_clock.h/.cpp`
- `src/server/server_app.cpp`
- `src/tools/net_real_main.cpp`
- `src/client/bgfx_app.cpp`
- `tests/test_net.cpp`

> Phase 6 已在此基础加入大厅/匹配、机器人补位、结算和 Rematch；nsync_server 默认开启大厅，使用 --no-lobby 可复现本文的登录即开局行为。

## Phase 3.1 real-process smoke

- 新增 `scripts/real_process_smoke.ps1` / CTest `nsync_net_real_process`：独立启动 `nsync_server --net` 和 2 个 `nsync_net_real --client`，使用 `arena_01.json`、50ms 模拟 RTT。
- Debug/Release 均通过；脚本约 10-12s。客户端输出 `corrections=0`、`desyncs=0`。
- 10 clients、0/150ms、10 分钟 soak、10 场 Rematch 仍待验证。

- nsync_net_real_process_10p_150：独立 server + 10 client，150ms 模拟 RTT、180 tick；Debug/Release 均通过，客户端 corrections=0、desyncs=0。

- nsync_net_real_process_multi_match：真实 server + 2 client、0ms、120 tick 短对局、auto-rematch，客户端进入第 2 场；Debug/Release 通过。
- nsync_soak_10m：10 分钟模拟时间、10 玩家、36000 tick；Release 10.8s，Debug 约 200s；mismatches/desyncs/corrections=0。

### 真实进程 10 分钟 soak（2026-10-31）

`scripts/real_process_soak.ps1 -DurationSeconds 600 -ClientCount 10 -RttMs 0 -MatchDurationSeconds 180 -SampleSeconds 30`：独立 server + 10 client，612s，16 场 Rematch；全部客户端 `corrections=0`、`desyncs=0`。总 WorkingSet 31.4 MB（启动初期）→ 79.2 MB，启动后稳定在 74.5–80.4 MB；总句柄 489 → 919，30s 后保持 919；无持续内存/句柄/队列增长。Release 通过。

### Phase 1.4 step scheduler / STS 闭环（2026-10-31）

真实 2 client、600 tick、50ms：STS +100ms -> ELAG=2、本地/目标差 2 tick；STS -100ms -> ELAG=14、差 2 tick；steps≈544–565、skips≈34–44、debt=0–2；corrections/desync/rollback=0。estimated_server_tick 改为按快照接收时间外推并限制在 snapshot_interval + RTT + 2 tick，避免时钟偏移导致目标漂移。新增 CTest nsync_net_real_process_step_scheduler；Debug/Release 40/40。

### Phase 1 默认切换评估（2026-10-31）

10 人 600 tick loopback，生产默认组合（client lag + fixed-step scheduler + server late replay）：0/50/150ms 的 bs(server-local-lag)=1/0/2 tick，ejected_inputs=0、late_replay_failures=0、corrections=0、desyncs=0；rollbacks 0/15/1，snapshot mismatch 仍存在。生产客户端与 nsync_server 默认开启，保留 --no-client-lag、--no-fixed-step-scheduler、--no-late-input-replay 回退；Debug/Release 40/40。

### WAN 与并发短测（2026-10-31）

- 真实 server + 10 client、250ms 模拟 RTT、600 tick：`corrections=0`、`desyncs=0`，rollbacks 56–79，客户端目标 lag 误差 0–3 tick。
- 真实 2 client、400ms、600 tick：`corrections=0`、`desyncs=0`，rollbacks 91–104；发现并修复高 RTT 重复 Login 导致 slot 泄漏，新增 `nsync_net_real_process_wan_400`。
- 10 人 250ms/612s soak：17 场 Rematch，`corrections/desync=0`，总 WorkingSet 31.4→80.4 MB，30s 后稳定 72–81 MB，句柄 489→919 后保持稳定。
- 250ms + 30ms jitter + 5% loss + 10% duplicate，2 client/300 tick 与 10 client/612s soak 均 `corrections/desync=0`；10 人 soak 完成 18 场 Rematch，lost≈9.8k、dup≈18.2k、KCP retx≈1.4k，WorkingSet 31.4→80.3 MB、句柄 489→919 后保持稳定。
- 10 场并发 LocalSession、10 分钟模拟时间（Release 手动）：`mismatches=0`、`desyncs=0`、`corrections=0`，Release wall 约 3s。
- 10 个独立 server + 20 个真实 client、150ms、900 tick（`scripts/real_matches_soak.ps1`）：10 场全部开局，所有 client `corrections/desync=0`；30 进程峰值总 WorkingSet 约 187 MB，句柄峰值 2630，结束后回落。
- 同一 10 场并发配置叠加 250ms + 30ms jitter + 5% loss + 10% duplicate：10 场全部开局，`corrections/desync=0`，峰值总 WorkingSet 约 188 MB；单客户端 lost 114–124、dup 212–239、retx 32–42。
- 对应 CTest：`nsync_net_real_process_wan_250`、`nsync_net_real_process_wan_400`、`nsync_net_real_process_wan_loss`、`nsync_net_real_process_10_matches`、`nsync_soak_10_matches`；总数 34。

### 客户端 FPS / 带宽（2026-11-01）

- `scripts/client_perf.ps1` 启动真实 server + 1 个 `nsync_net_real` + 1 个图形客户端（默认大厅/对局、`--no-vsync`），每 5s 输出 FPS、上下行 kbps、RTT、rollbacks。
- 1280x720：18000 帧、10.7s，平均 **1685 FPS**；对局内采样约 1635/1733 FPS，上行 7.0/16.5 kbps、下行 38.1/110.7 kbps，RTT 16ms，`corrections/desync=0`。
- 1920x1080：18000 帧、9.7s，平均 **1849 FPS**；对局内采样 1769 FPS，上行 7.2 kbps、下行 39.1 kbps，RTT 16ms，`corrections/desync=0`。
- 低端 GPU：`--gpu-vendor intel` 选中 Intel UHD 630；1280x720 平均约 190–224 FPS，1920x1080 平均约 164 FPS，对局内上下行约 15–17/95–125 kbps，RTT 7–15ms，`corrections/desync=0`。`--gpu-vendor` 支持 intel/nvidia/amd/microsoft/software。
- 快照 mismatch/restore 仍存在，但无 authoritative correction/desync；NVIDIA 高帧率为无 vsync 上限值。
### 快照 mismatch 诊断（2026-11-01）

- 新增 `snapshot_missing_predictions`（smm）、`snapshot_periodic_mismatches`（smp）、`snapshot_authoritative_mismatches`（sma）；`nsync_net_real`、图形客户端日志和单元测试均覆盖。
- 2 人/50ms/600 tick、client lag + scheduler：client0 `sm=164 smm=0 smp=164 sma=0`，client1 `sm=169 smm=0 smp=169 sma=0`；没有“缺少预测条目”导致的假 mismatch。
- 调试对比 tick 9/12/15：本地 aim/buttons 已应用而服务器尚未收到（如本地 `buttons=2`、服务器 `buttons=0`；随后服务器 `18/17`、本地仍 `2/0`），确认周期快照 mismatch 主要来自客户端 input lead 与远端输入到达时序，由 restore 收敛。
- `last_client_hash` 在 restore 后同步为服务器 hash，避免 ack 继续携带旧预测 hash；恢复旧 tick 的 authoritative sync 仍按既有策略忽略；late-input replay 重写历史后的旧 ACK 已单独计为 `rewritten_ack_mismatches`，不再误报。
### 连续丢包/Burst（2026-11-01）

- `KcpTransport` 新增有状态 burst loss：随机触发的丢包会连续丢弃 `burst_length` 个包，收发方向独立；CLI 为 `--burst-length N`，默认 1（独立丢包）。
- 2 人/250ms/30ms jitter/3% loss/4 包 burst/5% duplicate、300 tick：`lost/dup>0`，`corrections/desync=0`，新增 CTest `nsync_net_real_process_wan_burst`。
- 10 人 314s soak（9 场 Rematch）：`lost≈9.3k`、`dup≈3.6k`、`retx≈1.1–1.4k`，`corrections/desync=0`，WorkingSet 27.3→77.5 MB（30s 后约 73–78 MB），句柄 429→919 后稳定。
- 8 包 burst/5% loss 在 250ms 下过于激进，出现客户端重连；长时验证采用 4 包/3%。
### late-replay ACK 归因（2026-11-01）

- `late_input_replay` 会重写 `snapshot_history_` 中 base 之后的权威 hash；客户端此前 ACK 的旧 tick hash 因此与修正后的 hash 不同。
- 服务器新增 `rewritten_ack_mismatches`：ACK tick 早于最近一次历史重写点时单独计数并忽略，不再为旧 tick 发送权威 `SyncResponse`；真实 `ack_mismatches` 仍用于新 tick。
- 2 人/600 tick/50ms、250ms + burst：`ack_mismatches=0`，`rewritten_ack_mismatches` 约 74–78，客户端 `corrections/desync=0`。
- 新增 CTest `nsync_net_real_ack_rewrite`，要求 `mismatches=0` 且 `rewritten>0`。
### 延迟尖峰（2026-11-01）

- `KcpTransport` 新增周期性 transient spike：在 `spike_interval_ms` 周期内，`spike_duration_ms` 窗口内的收发包额外增加 `spike_ms`；CLI 为 `--spike-ms/--spike-interval-ms/--spike-duration-ms`。
- 2 人/600 tick/250ms + 每 1.5s 300ms 窗口 +300ms：`spike>0`、`corrections/desync=0`，出现约 105 次 scheduler skip 后恢复；新增 CTest `nsync_net_real_process_wan_spike`。
- 10 人/314s、250ms + jitter 30ms + 3% loss + 4 包 burst + 5% dup + 每 2s 300ms spike：9 场 Rematch，`spike≈8.8–9.3k`、`lost≈7.8k`、`dup≈3.1k`、`retx≈1.6k`，无 corrections/desync，内存/句柄稳定。
### WAN trace 回放（2026-11-02）

- 新增 `load_wan_trace`：CSV 时间轴格式 `at_ms,latency_ms,loss_percent,duplicate_percent`，首样本必须为 0、时间严格递增，支持注释/可选表头。
- `KcpTransport::set_network_trace` 在 trace 激活时按当前时间选择最近样本，覆盖基础 latency/jitter/loss/duplicate/burst/spike；CLI 为 `--trace-file PATH`，禁止与其他 WAN 模拟参数混用。
- `nsync_net_real` 输出 `trace=` 模拟包数；样本 `assets/net/wan_trace_sample.csv` 为 300s 周期时间线（250ms 基线、550/400ms 尖峰、5%/10% loss、5% duplicate）。
- 2 人/600 tick：trace 下 `trace>0`、`corrections/desync=0`，新增 CTest `nsync_net_real_process_wan_trace`。
- 10 人/316s、8 场 Rematch：`trace≈72k`、`lost≈0.5–0.6k`、`dup≈0.24–0.26k`、`retx≈1.1–1.2k`，无 corrections/desync，WorkingSet 27→79 MB、句柄启动后稳定。
### 反编译 World1 地图与移动属性（2026-11-02）

- 从 `reference/nsync/extracted/World1.bytes` 转换：原图 32x16 quadrants、172 条 border、24 个 platform；坐标转为 y-down 并平移到原点。
- 输出 `assets/maps/world1.json`：546 个 border AABB（轴对齐段保持整段，斜边按 0.5 分块）、24 个平台（15 个可下落 hatchway）、9 个 jump pad、10 个原始 `playerEntries` 出生点；设为客户端/服务器默认地图。
- Jump pad 使用原 `jumpVelocity` 换算为每 tick 初速度，落地时替换 `velocity.y`；新增物理单元测试。
- 移动属性按 `environment.bytes`: `move`/`jump` 表 × `steamMoveMultiplier=1.3`/`steamJumpMultiplier=1.3` 除 60；重力使用 `1/142`（上升）与 `1/97`（下降）近似英雄 `up/down × steamGravityMultiplier`。Scout/Gunner/Assault 的 run/jump 值已写入 content 与内置默认值。
- 玩家碰撞半高从 0.9 改为 0.7、默认/测试地图出生点 y 相应调整；golden replay 哈希已重新生成。
- 新增 `nsync_map_compile_world1`、`nsync_net_map_world1`；Debug/Release CTest 40/40。