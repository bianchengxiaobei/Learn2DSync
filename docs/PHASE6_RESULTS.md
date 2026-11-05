# Phase 6：大厅与匹配结果

日期：2026-10-30  
状态：核心闭环通过；主菜单/房间仍是轻量 HUD，没有完整 RmlUi 菜单。

## 目标与范围

Phase 6 把 Phase 5 的“登录后立即进入单局”改为：

```text
启动 -> 假登录 -> 大厅 -> 选择英雄 -> Ready -> 倒计时 -> 5v5 对局
     -> 结算 -> 再来一局 -> 大厅
```

实现参考原版流程，但没有复制原版的多连接架构：

- 原版主连接负责账号/组队/申请，PvP 连接负责房间和对局；本项目暂时用一个 KCP 服务器承载登录、组队和对局。
- 原版 `InitAssemblyNotification`、`StartGameNotification`、`EndGameNotification` 分别对应本项目的 `LobbyState`、`MatchStart`、`MatchEnd`。
- 原版 `AssemblyController` 的申请/确认/离开流程简化为 Ready/Rematch/Leave；原版 `GroupController` 的单人组队和邀请留到后续。
- 原版 `AssemblyStage` 的状态被压缩为 `Lobby`、`Countdown`、`Match`、`MatchEnd` 四个阶段。

## 关键参考

- `reference/nsync/decompiled/Assets.Script.Main.Controllers/AssemblyController.cs`
  - `CountDownDuration = 5f`
  - `StartGameNotification`、`EndGameNotification`、`GameAssemblyNotification`
  - 申请、确认、退出和结算阶段
- `reference/nsync/decompiled/Assets.Script.Main.Models.Lobby/AssemblyMember.cs`
  - `Preparing`、`Confirmed`、`Cancelled`
- `reference/nsync/decompiled/Assets.Script.Main.Models.Lobby/AssemblyStage.cs`
- `reference/nsync/decompiled/Assets.Script.Main.Controllers/GroupController.cs`
  - `ready`、`selected_hero_changed`、成员状态同步
- `reference/nsync/decompiled/com.pragmatix.df.messages/Messages.cs`
  - `GAME_ASSEMBLY_REQUEST=152`、`GAME_ASSEMBLY_NOTIFICATION=2152`
  - `INIT_ASSEMBLY_NOTIFICATION=2108`、`START_GAME_NOTIFICATION=2109`、`END_GAME_NOTIFICATION=2110`

## 协议改动

协议版本升级到 **2**。新增消息：

| 消息 | ID | 用途 |
|---|---:|---|
| `LobbyRequest` | 152 | Ready/Unready、选英雄、Rematch、Leave |
| `LobbyResponse` | 1152 | 请求结果/错误 |
| `LobbyState` | 2108 | 大厅快照：阶段、人数、Ready 数、倒计时、成员 |
| `MatchStart` | 2109 | 对局 ID、seed、地图 fingerprint、初始快照、slot |
| `MatchEnd` | 2110 | 胜负、比分、结算等待时间、对局 ID |

`LoginRequest` 新增：

- `display_name`：假登录用户名
- `login_token`：假令牌（当前只记录，不做真实鉴权）
- `desired_hero`、`desired_team`

`LoginResponse` 新增：

- `phase`
- `match_id`

## 服务器

`RealServer` 现在支持：

- 10 个 slot，队伍按 `slot % 2` 分配。
- 登录后进入大厅；人类数量不足时按空位自动补机器人。
- 所有已连接人类 Ready 后进入倒计时；倒计时结束重置世界并开始对局。
- 每个真人 slot 应用其选择的英雄。
- 掉线/超时后该 slot 的 `bot` 标记切换为 1，机器人立即接管；重连时恢复为真人。
- 对局结束发送最终快照和 `MatchEnd`；延迟后回到大厅。
- `Rematch` 请求保留到回到大厅，服务端自动 Ready 并开启下一局。
- 大厅默认开启；可用 `--no-lobby` 恢复 Phase 5 的登录即开局行为。

服务器参数：

```powershell
nsync_server.exe --net --lobby --players 10 --port 40100 `
  --lobby-countdown-ms 3000 --match-end-delay-ms 3000 `
  --match-duration-ticks 18000 --score-limit 30 --lobby-min-humans 1
```

## 客户端

`RealClient` 现在区分大厅和对局：

- 登录后先接收 `LoginResponse` + `LobbyState`，本地世界只作为大厅预览，不推进模拟。
- `MatchStart` 到达后重新初始化 `LocalClient`，再开始预测、输入和快照同步。
- 对局中的 `Snapshot`、`InputBroadcast`、`SyncResponse` 才应用到本地世界。
- `MatchEnd` 后冻结结算数据，等待 `LobbyState` 回到大厅。
- 自动 Ready 默认开启；`--no-auto-ready` 可改为手动。
- `--auto-rematch` 可在结算后自动请求下一局。

可视客户端大厅操作：

- `R`：Ready / Unready；结算界面按 `R` 请求 Rematch
- `1` / `2`：选择 Scout / Gunner
- 大厅 HUD 显示阶段、人数、Ready 数、倒计时、slot 和 FPS
- 对局 HUD 保持原有 TDM/网络状态

## 验证

Debug/Release CTest **11/11**：

```powershell
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

新增测试：

- `nsync_lobby_loopback`：10 slot、2 真人、8 机器人，300 tick 对局，结算。
- `nsync_lobby_reconnect`：对局中客户端断开，机器人接管；随后用 session token 重连。
- `nsync_lobby_rematch`：连续 3 局启动、2 局结束，验证自动 Rematch。
- `nsync_lobby_5v5`：10 个真人 slot 的 5v5 快速验证。

本机结果：

```text
nsync_lobby_loopback: corrections=0, desyncs=0, match ended
nsync_lobby_reconnect: bot_takeovers=1, client reconnected, exit 0
nsync_lobby_rematch: matches started=3, ended=2, corrections=0
nsync_lobby_5v5: 10 真人 slot，match ended，corrections=0
```

另用两个真实可视客户端连接 `nsync_server --net --lobby --players 4`：

- 客户端分别获得 slot 0/1；
- 大厅 Auto Ready 后同时收到 `MatchStart`；
- 对局 `corrections=0`；
- 结算后 `--auto-rematch` 成功开始下一局。

## 当前边界

- 没有完整的主菜单、RmlUi 房间页、邀请/小队 UI；目前是命令行 + 轻量 HUD。
- 没有账号系统、真实令牌校验、封禁、排位、跨服匹配。
- 仍是单进程单对局；10 个真人并发和 NAT 穿透未做。
- 机器人接管后重连会产生一次快照纠正，这是预期行为；没有做“无缝接管”。
- 10 分钟真实网络 soak、delta/压缩快照和抓包工具仍未做。
