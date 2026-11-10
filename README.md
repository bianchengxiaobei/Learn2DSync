# Learn2DSync

C++20 横版 PvP 射击游戏。

- **Phase 3 已完成**：地图、角色控制、相机、多英雄、三武器基础、TDM、2v2 机器人和初始音效占位（Phase 8 已替换为 SDL3 音频）。
- **Phase 4 已完成**：本地服务器/客户端、输入延迟、预测、回滚重放和网络模拟测试台。
- **Phase 5 已完成**：KCP over UDP、真实服务器/客户端、登录/session token、快照同步、时钟同步和断线重连。
- **Phase 6 核心闭环完成**：假登录、大厅/Ready、英雄选择、5v5（真人 + 机器人）、结算、掉线机器人接管和 Rematch。
- **Phase 7 内容管线与目标模式完成**：3 英雄、6 武器、2 地图、JSON 校验器、地图二进制编译器、内容指纹、v4 协议（当前 v9）、控制点模式、84 帧英雄图集、21 组动画状态和开发期资源热重载。
- **Phase 8 进行中**：RmlUi 主菜单、在线房间、结构化对局 HUD、Tab 计分板、暂停和结算面板；已加入确定性 K/D/DMG/BLOCK 战斗统计、护盾/回盾、三名英雄各 3 个 gadget 技能、First Blood/连杀事件，快照升级到 v15、协议 v9；战斗表现加入程序化枪口闪光、弹道拖尾、命中火花、爆炸冲击、技能粒子纹理和纯表现相机抖动/后坐力；SDL3 音频加入武器/命中/爆炸/脚步/UI 程序化音效与距离衰减/声像，技能释放/命中/爆炸改用内置 `sounds/effects` 音频；设置页加入音量、分辨率/全屏/VSync、血效和键位重绑；新增默认中文/英文切换、启动加载页和启动/运行时错误提示。
- **水晶/收集模式**：确定性水晶实体、多水晶携带（`max_capacity`/`carry_limit`）、attach/hold/deliver 三段可配置计分、死亡掉落/回收、depot 交付计时，进入快照与协议；地图 `drop_spawns`/`drop_depots`，内容 `drops`/`mode_rules.crystal`；内置水晶 sprite、拾取/交付音效、depot 脉冲光和携带方向光柱；`crystal_01`/`crystal_02` 两张水晶地图均带美术，携带水晶时头顶显示背包 sprite 和数量。
- **原版补给道具**：从原版 `supply_gen` 插件导入各地图的 `supply_spawners`，生成 `health_box`（回复 20% 最大生命 + health_pump）和 `double_damage`（+100% 伤害 10 秒）；补给为确定性世界状态，进入快照/协议，客户端绘制原版医疗箱/双倍伤害贴图并在 HUD 显示状态。
- **分层角色美术（方案 A）已完成基础版**：Scout/Gunner/Assault 使用基础皮肤分件，按 prefab z-order 分层绘制，程序化 `idle/run/jump/fall/shoot/hurt/death`，按当前武器切换六种武器 sprite；资源缺失自动回退 84 帧占位图集。逐帧动画仍需额外的 `.anim`/FBX 数据。

技术栈：SDL3、bgfx、RmlUi、KCP over UDP、自定义定点数。

设计文档：[`docs/TODO.md`](docs/TODO.md)  
Phase 0 结果：[`docs/PHASE0_RESULTS.md`](docs/PHASE0_RESULTS.md)  
Phase 1 结果：[`docs/PHASE1_RESULTS.md`](docs/PHASE1_RESULTS.md)  
Phase 2 结果：[`docs/PHASE2_RESULTS.md`](docs/PHASE2_RESULTS.md)  
Phase 3 结果：[`docs/PHASE3_RESULTS.md`](docs/PHASE3_RESULTS.md)  
Phase 4 结果：[`docs/PHASE4_RESULTS.md`](docs/PHASE4_RESULTS.md)  
Phase 5 结果：[`docs/PHASE5_RESULTS.md`](docs/PHASE5_RESULTS.md)  
Phase 6 结果：[`docs/PHASE6_RESULTS.md`](docs/PHASE6_RESULTS.md)  
Phase 7 结果：[`docs/PHASE7_RESULTS.md`](docs/PHASE7_RESULTS.md)  
Phase 8 结果：[`docs/PHASE8_RESULTS.md`](docs/PHASE8_RESULTS.md)  
分层角色美术结果：[`docs/ORIGINAL_HERO_ART_RESULTS.md`](docs/ORIGINAL_HERO_ART_RESULTS.md)

## 环境

- Windows 10/11 x64
- Visual Studio 2022（MSVC x64）
- CMake 3.24+
- vcpkg，设置 `VCPKG_ROOT`

本机 vcpkg：`E:\Dev\vcpkg`

```powershell
$env:VCPKG_ROOT = "E:\Dev\vcpkg"
```

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset debug
```

Release：

```powershell
cmake --build --preset release
```

## 运行

客户端（SDL3 + bgfx + RmlUi）：

```powershell
cd build\vs2022\Debug
.\nsync_client.exe
```

启动先显示加载过渡页，再进入 RmlUi 离线主菜单：选择 Scout/Gunner/Assault、选择 TDM、CONTROL 或 CRYSTAL，点击 `DEPLOY` 后走部署加载页并启动本地 **2v2** 对局。本地控制 Team 1，另外 3 名玩家由确定性机器人控制。当前默认配置是 `maps/crystal_01.json` + CRYSTAL；`crystal_02` 是另一张水晶地图，`arena_01/02` 保留为测试地图。`maps/world1.json` 是 PvE 布局，没有目标点/水晶点，不能用 CONTROL/CRYSTAL 开局。CRYSTAL 模式需要地图包含 `drop_spawns` 和 `drop_depots`；`team=0` 的 depot 是双方共用的中立交付点。

- 菜单：鼠标点击英雄卡/模式，或 `1/2/3` 选英雄、`Enter` 部署；`SETTINGS` 打开设置；`QUIT` 退出
- 默认 `A/D`：移动
- 默认 `W`：跳跃
- 鼠标：瞄准
- 鼠标左键：射击（固定，不可重绑）
- 默认 `R`：换弹
- 默认 `Q`：循环切换当前 6 把武器
- 默认 `E/F/G`：三个技能槽，按英雄默认/可选 gadget 配置：
  - Scout：红宝石之刃 / 能量电池 / 磁力喷嘴
  - Gunner：破片手雷 / 静电护盾 / 氮气加速
  - Assault：火箭发射 / 防护机器人 / 急救包
- 默认 `Tab`：计分板
- 默认 `Esc`：对局中暂停；菜单/房间/结算中退出或返回
- `F5`：立即重载内容/地图/图集/动画（直接模式会重置沙盒，保留键位）

音效默认启用：武器射击按 Rifle/Shotgun/Rocket/SMG/Laser/Grenade 区分，包含随机变体、距离衰减和左右声像；技能释放/命中/爆炸从 `assets/audio/gadgets/*.wav` 加载技能音效（30 个技能共 56 个音频片段），另有命中/护盾、脚步、死亡/复活、比赛结束和 UI 点击音。`--no-audio` 可静音，`--audio-volume 0..100` 调整主音量。

设置页可从主菜单或暂停面板打开，含 GAME / CONTROLS 两页：主音量、音效开关、分辨率、全屏、VSync、血效开关，以及移动/跳跃/换弹/武器/技能 1/2/3/计分板/暂停键位重绑。点击键位行后按新键；Backspace 重置，Esc 取消。运行期设置保存到 `config/client.settings.ini`，下次启动覆盖基础配置，命令行参数优先级最高。`--settings` 可直接打开设置页做冒烟，`--fullscreen` / `--windowed`、`--blood` / `--no-blood` 可覆盖对应启动项；`--language en|zh` 切换界面语言。启动时显示加载过渡页，本地部署也走加载页；致命启动错误使用系统错误对话框。

`--no-menu` 可跳过主菜单直接开始本地对局；带 `--frames` / `--ticks` 的自动化运行默认跳过主菜单，`--menu` 可强制显示。`--connect` 联机模式直接进入在线房间，不经过离线主菜单。`--gpu-vendor intel|nvidia|amd|microsoft|software` 可指定 D3D11 适配器，用于低端集显/多 GPU 性能对比。

本地玩家默认 Scout，可用 `--hero assault` 等参数选择；机器人包含 Scout/Gunner/Assault。对局 HUD 显示 TDM/CTRL 比分、剩余时间、英雄、HP/护盾、右侧三技能卡（名称/按键/冷却/激活）、弹药、当前武器、控制点归属、事件流、FPS 和网络信息；默认界面语言为中文，可用 `--language en` 切回英文。时间到或达到比分上限后显示 VICTORY/DEFEAT/DRAW 和 Rematch/Main Menu/Quit。内容数据来自 `assets/data/content.json`，可用 `nsync_datac` 校验。控制点模式用 `--mode control` 启动，地图必须包含 `objective` 区域。水晶模式用 `--mode crystal` 启动：拾取水晶后带回 `drop_depots` 区域（`team=0` 为双方共用），停留 `deliver_ticks` 后交付得分；携带者死亡会掉落水晶，水晶超时回到出生点。`crystal_01`/`crystal_02` 有 13 个水晶点、2 个中立 depot，携带时头顶显示背包和数量。

分层角色由 `client.hero_art=auto` 默认启用，也可用 `--hero-art auto|placeholder|original` 覆盖：`auto` 在 `assets/generated/original_heroes/` 与 `data/hero_rigs.json` 存在时启用，缺失时回退 84 帧占位图集；`placeholder` 强制旧图集；`original` 用于本地验收并记录回退 warning。

指定地图或跑固定帧数：

```powershell
.\nsync_client.exe --no-menu --map maps/world1.json --frames 120
.\nsync_client.exe --no-menu --map maps/arena_02.bin --frames 120
.\nsync_client.exe --no-menu --mode control --map maps/arena_01.json --frames 120
.\nsync_client.exe --no-menu --mode crystal --map maps/crystal_01.json --frames 120
.\nsync_client.exe --menu --frames 120
```

`--map` 支持 JSON 作者格式和 `nsync_mapc` 编译出的二进制地图；`--mode control|crystal` 只影响直接游玩，联机模式由服务器下发快照中的模式和 `map_id`，客户端会自动加载服务器地图。`--map` 在联机时只作为首次连接使用的本地默认地图。

### 开发期热重载

```powershell
.\nsync_client.exe --hot-reload --mode control --map maps/arena_01.json
```

`--hot-reload` 会优先挂载源码 `assets/` 并每 0.5 秒检查 `content.json`、当前地图、图集元数据/图片、`animations.json`、`hero_rigs.json`、`hero_poses.json` 和角色图集/图片。直接模式修改内容或地图会重建本地沙盒；视觉资源热重载不重置世界。联机/本地权威模式下只热重载视觉资源，内容/地图变更会拒绝，避免客户端服务器 fingerprint 或模拟状态不一致。不启用 watcher 时也可按 `F5` 手动重载一次。RmlUi 的 `game.rml` / `game.rcss` 尚未纳入 watcher，修改界面后需重启客户端。

### RmlUi 界面壳

界面源文件是 `assets/ui/game.rml` 与 `assets/ui/game.rcss`，C++ 控制器在 `src/client/game_ui.*`。当前包含：

- 离线主菜单：英雄选择、TDM/CONTROL、地图/fingerprint、Deploy/Quit。
- 在线 WAR ROOM：双方成员、OPEN SLOT、英雄、槽位、Ready 人数、倒计时、Ready/Cancel、Leave。
- 对局 HUD：比分/计时、控制点进度、HP/护盾、右侧三技能卡（名称/按键/冷却/激活）、弹药/武器、事件流、准星、受击闪屏、阵亡复活倒计时、网络/FPS。
- `Tab` 计分板：队伍、玩家、英雄、K/D/DMG/BLOCK、HP、LIVE/DOWN，本地玩家高亮；First Blood、Double/Triple/Monster Kill、Killing Spree 等事件进入事件流。
- 战斗表现：队伍色枪口闪光、弹丸轨迹拖尾、命中/护盾受击火花、按爆炸半径缩放的冲击环/火花，以及只影响世界渲染的镜头抖动和开火后坐力。
- 战斗音效：SDL3 播放程序化单发/爆炸/脚步/UI 音效，按武器族随机变体、按 17 世界单位距离衰减并按声源横向位置 pan；菜单/按钮点击、换弹、命中护盾/生命、死亡/复活、技能和比赛结束都有对应 cue。
- 设置：主菜单/暂停进入，GAME 页调整音效、主音量、分辨率、全屏、VSync 和血效；CONTROLS 页点击后按键重绑，支持冲突提示、Backspace 重置和 Esc 取消；Apply 后立即生效并持久化。
- `Esc` 暂停（仅离线直接对局）和 VICTORY/DEFEAT/DRAW 结算，含 Rematch/Main Menu/Quit。

界面更新与模拟解耦：菜单/暂停/结算时不推进本地直接模拟，但联机房间和结算仍持续接收网络包。动态用户名会先做 HTML 转义。主菜单、对局、计分板、暂停和结算按功能分层实现，不复制其他项目的代码或资源。

### 本地权威网络测试台

启用 4 个本地客户端、输入延迟/预测/回滚：

```powershell
.\nsync_client.exe --net
.\nsync_client.exe --net --net-latency-ms 150 --net-jitter-ms 30 --net-loss 10 --net-duplicate 10
```

`--net` 模式下 HUD 会显示 ack/RTT/回滚/correction 信息。

## 服务器

headless 服务器（固定 60Hz）：

```powershell
.\nsync_server.exe --ticks 300 --tick-rate 60
```

不传 `--ticks` 时一直运行，Ctrl+C 退出。

真实 UDP/KCP 服务器（大厅默认开启）：

```powershell
.\nsync_server.exe --net --lobby --port 40100 --players 10 `
  --map-rotation maps/crystal_01.json,maps/crystal_02.json --mode crystal `
  --lobby-countdown-ms 3000 --match-end-delay-ms 3000
```

`--players` 是总 slot 数（最多 10）；不足真人时由机器人补齐。`--no-lobby` 可恢复登录即开局。`--map` 指定单张固定地图；`--map-rotation` 用逗号分隔地图列表，每局按顺序轮换。`server.map_rotation` 非空时优先于 `server.map`；命令行 `--map` 会清空配置里的轮换。两者都支持 JSON 或编译后的 `.bin`；`--mode tdm|control|crystal` 选择团队死斗、控制点或水晶。服务器还会保留 slot/button mask/输入 tick 范围校验，并对每个已登录会话做基础限流：默认 600 packets/s、240 inputs/s，超限包被丢弃并计入 `rate_limited_packets`/`rate_limited_inputs`。

> **联机地图由服务器决定，客户端自动跟随（协议 v13）。**
> 服务器在登录应答、`LobbyState` 和 `MatchStart` 中下发 `map_id` 与 fingerprint。客户端先用 `client.map` 建立连接；如果服务器地图不同，客户端会按 `map_id` 从本地 `assets/maps/<id>.json` 或 `.bin` 自动加载，再重新登录/进入下一局，并同步重载地图美术。服务器不会传输地图文件，所以客户端必须本地安装对应地图。`--mode` 也由服务器决定并随快照下发，客户端联机时不需要传 `--mode`。

真实客户端：

```powershell
.\nsync_client.exe --connect --server-host 127.0.0.1 --server-port 40100 `
  --name alpha --hero scout --no-vsync
```

启动两个客户端即可测试大厅：客户端默认自动 Ready，服务端倒计时后开始；`--players` 是总 slot，两个真人占满 `--players 2` 时不会补机器人。RmlUi 房间显示双方成员、OPEN SLOT、英雄、槽位、Ready 人数和开局倒计时；对局 HUD 显示比分/时间/控制点/HP/护盾/技能冷却/弹药/事件流以及 ack/RTT/SLAG/ELAG/RTTC/EST/CELAG/STS/LEAD/REC/queued/RB/ERB/CIN/SM/SR/correction/时钟偏移。

联机输入默认使用排帧同步模型，而不是客户端固定延迟。服务器在登录和开局消息中下发 `serverLag`、`clientEventLag`、`syncPeriod`；普通输入按 `CurrentFrame + clientEventLag` 排帧，收到 sync/desync 校正后的下一批输入才使用完整恢复公式 `CurrentFrame + serverLag + clientEventLag + clientEventLag/2 + 1`。当前默认 `server_lag=8`、`client_event_lag=3`、`sync_period=30`，基础 lead 为 3 tick、恢复 lead 为 13 tick；v1.69 起普通在线客户端会按实测 RTT 自动增加 lead（`RTT/2 tick + 4`，上限 60），避免高延迟下输入到达服务器时已经过期。输入在按钮/瞄准变化时发送，并最多每 6 tick 重发一次；服务器接受目标 tick 等于当前待模拟 tick 的输入，不再把刚好踩点的输入推迟一帧。远端迟到输入不再触发本地回滚，而是排到下一本地 tick；周期快照只恢复确实偏离服务器的本地玩家，且预测哈希按快照目标 tick 比较。服务器配置：

```ini
server.input_sync_model=original
server.server_lag_ticks=8
server.client_event_lag_ticks=3
server.sync_period_ticks=30
server.sync_time_to_step_ms=0
```

也可用 `--input-sync-model original|legacy`、`--server-lag-ticks N`、`--client-event-lag-ticks N`、`--sync-period-ticks N`、`--sync-time-to-step-ms N` 覆盖。`legacy` 只用于对照和回退。客户端 `--input-delay-ticks` 现在只是服务器未提供 timing 时的 fallback，不再覆盖在线权威参数。

本地双开建议加 `--no-vsync`；两个窗口同时开启 VSync 时，部分驱动/桌面合成器会把帧率压到 30 或 16 FPS；弹匣打空后会自动换弹，`R` 仍可手动换弹。`--input-delay-ticks` 默认 3 只用于 fallback。联机默认不再在客户端模拟 `serverLag`，也不启用 fixed-step scheduler；需要复现旧的输入提前量/回放行为时再显式加 `--client-lag --fixed-step-scheduler`。默认模式落后服务器超过 2 tick 时只补本地模拟步（每帧最多 1 步），不再用未来快照整帧前跳；周期快照仅在本地玩家真实偏离时恢复。 v1.71 修复从斜坡、薄桥或略嵌入地面起跳时，竖直碰撞把脚下的薄solid误判为天花板、将角色推到其下方并掉出地图的问题；水平/竖直碰撞现在结合上一 tick 的包围盒判断进入方向。v1.72 起，周期快照发现本地玩家匹配但远端玩家不一致时，只把服务器远端玩家状态合并回来，不恢复本地玩家；HUD 新增 `RMC` 统计远端快照校正次数，避免两个客户端互相看到对方在错误位置。v1.73 起玩家渲染位置按帧时间做平滑：远端玩家的 20Hz 快照更新不再直接跳格，本地玩家也做很轻的平滑；超过 3 个单位（远端）或 1.5 个单位（本地）的权威校正仍直接吸附，避免橡皮筋。v1.74 起远端快照校正不再重建整份预测历史（避免每个快照做一次大世界保存/哈希造成瞬时掉帧），只合并远端玩家显示状态。底部联机状态行还会显示 `TICK`（客户端预测 tick）与 `POS`（本地玩家坐标）；需要定位分叉时设置环境变量 `NSYNC_NET_TRACE=1`，客户端会在周期快照恢复时向 stderr 输出目标 tick、服务器/预测/本地坐标、速度和按钮位，建议用 `2> client.log` 启动客户端收集。v1.69 的 10,000 tick World1 无延迟回归为 `corrections/rollbacks/snapshot_restores=0`，100ms RTT + 20ms jitter + 2% loss 的 6,000 tick 回归只有 1 次真实快照恢复且无事件回滚。

房间中 `R` 切换 Ready，`1`/`2`/`3` 选择 Scout/Gunner/Assault，也可直接点击 `READY` 和英雄卡；`--hero assault` 可指定初始英雄。`--no-auto-ready` 可手动 Ready，`--auto-rematch` 结算后自动再来一局。结算面板中 `REMATCH` 会发送 Rematch 请求；在线 `--no-lobby` 模式没有服务器 `MatchEnd` 时，客户端会在世界结束时显示只读结算。

## headless 工具

真实网络工具：

```powershell
.\nsync_net_real.exe --client --host 127.0.0.1 --port 40100 --ticks 300 --input-delay 16
.\nsync_net_real.exe --client --host 127.0.0.1 --port 40100 --ticks 300 --map assets/maps/arena_02.json
.\nsync_net_real.exe --loopback --players 2 --ticks 600
.\nsync_net_real.exe --loopback --players 2 --ticks 300 --rtt-ms 50
.\nsync_net_real.exe --loopback --players 2 --ticks 300 --rtt-ms 150
.\nsync_net_real.exe --loopback --players 2 --ticks 600 --trace-file assets/net/wan_trace_sample.csv
.\nsync_net_real.exe --loopback --players 2 --ticks 600 --input-sync-model legacy
.\nsync_net_real.exe --loopback --mode control --players 2 --humans 2 --ticks 360
.\nsync_net_real.exe --loopback --lobby --players 10 --humans 2 --ticks 420 --match-ticks 300
.\nsync_net_real.exe --loopback --lobby --players 4 --humans 2 --ticks 360 --match-ticks 300 --drop-at 100 --resume-at 180
.\nsync_net_real.exe --loopback --lobby --players 4 --humans 2 --ticks 760 --match-ticks 300 --auto-rematch
.\nsync_net_real.exe --client --host 127.0.0.1 --port 40100 --ticks 300 --reconnect-at 100
```

`--trace-file PATH` 用 CSV 时间轴回放 WAN：每行 `at_ms,latency_ms,loss_percent,duplicate_percent`（首行必须从 0 开始），覆盖 `--rtt-ms` 和其他 WAN 模拟参数；`--rtt-ms` 在客户端 KCP 传输层模拟双向 RTT（每个方向延迟 RTT/2），用于 0/50/150ms 延迟矩阵；实际 KCP 测得 RTT 会略高于目标值。该参数只用于测试，不影响默认客户端/服务器配置。`--map PATH` 让 `--client`/`--loopback` 使用和服务器相同的地图文件，避免地图 fingerprint 不一致。`--client-lag` 让客户端本地模拟落后 `effective_server_lag`；`--fixed-step-scheduler` 按 `estimated_server_tick - effective_server_lag` 计算本地目标 tick，每 tick 调度 0–4 步，并输出 `steps/skips/debt/target`；在线客户端默认关闭这两项，服务器默认不启用 late-input replay；需要 lag 实验时显式加 `--client-lag --fixed-step-scheduler`（需要服务器回放迟到输入时再加 `--late-input-replay`）；测试工具 `nsync_net_real` 仍通过 `--client-lag`/`--fixed-step-scheduler` 显式启用；STS +100/-100ms 实测 `ELAG=2/14`，本地目标 lag 稳定在 0–3 tick；`sync_time_to_step` 会按原始 stepTime 公式换算成 tick 偏移并参与对齐/catch-up 目标；`RTTC` 用 RTT/2 补偿快照已过时的时间，`EST` 用最后快照 tick + 接收后经过时间（上限为 snapshot interval + RTT + 2 tick）+ RTT/2 估算服务器当前 tick。配合服务器 `--late-input-replay` 时，0/50/150ms 可做到 `corrections=0`、`desyncs=0`、`rejected_inputs=0`；服务器会把同一 update 的迟到输入合批，`late_replay_batches` 低于 `late_replays`；客户端会把已被最新权威快照覆盖的远端输入计入 `CIN` 并丢弃；v1.69 起未覆盖的迟到远端输入改为排到下一本地 tick，不再触发回滚；生产默认矩阵 0/50/150ms 为 0/15/1，仍保留 snapshot mismatch。`--sync-time-to-step-ms` 可覆盖服务器下发值（loopback 测试），默认 0。协议 v9 的输入消息附带 `ClientActionMask/action_toggles/action_param`；客户端从按钮边沿采集动作事件，服务器统计并随输入广播转发；`ActionEventQueue` 按 target tick/sequence/action mask 去重排序，当前 tick 消费并校验状态按钮一致性（`action_mismatches`）。非回放和迟到回放路径均已用动作事件重建权威按钮位（toggle 状态 + 单 tick pulse）；原始状态保留用于瞄准和缺少动作帧时的 fallback。

`nsync_net_real` 和 HUD 现在区分 `snapshot_mismatches`（普通快照与预测不一致）、`snapshot_restores`（因此恢复的次数）、`event_rollbacks`（迟到远端事件触发的回滚）、`rollbacks`（总回滚次数）和 `rollback_ticks`（累计回滚距离）；权威 mismatch 才计 `corrections/desyncs`。

Soak 工具（默认 60 分钟模拟时间，尽可能快地推进 tick；加 `--realtime` 按 60Hz 实时跑）：

```powershell
.\nsync_soak.exe --minutes 60 --matches 10 --players 10 --humans 2
.\nsync_soak.exe --minutes 60 --matches 1 --players 10 --humans 2 --realtime
```

真实多进程 soak（独立 server + N 个 client，采样总 WorkingSet/句柄）：

```powershell
.\scripts\real_process_soak.ps1 -BinDir build\vs2022\Release `
    -DurationSeconds 600 -ClientCount 10 -RttMs 0 `
    -MatchDurationSeconds 180 -SampleSeconds 30 `
    -MapPath assets\maps\arena_01.json
```
确定性 replay：

```powershell
.\nsync_replay.exe --ticks 120 --hash-every 1 --verify
.\nsync_replay.exe --ticks 120 --write golden.replay
.\nsync_replay.exe --read golden.replay --verify
```

每个 tick 输出 `tick <N> hash <16位hex>`；`--verify` 会用同一份 replay 跑两遍并比对哈希。`--read` 会从文件读取输入日志。

本地网络测试台：

```powershell
.\nsync_net.exe --ticks 600 --latency-ms 150 --jitter-ms 30 --log-every 120
.\nsync_net.exe --ticks 36000 --latency-ms 150 --jitter-ms 30 --log-every 0
.\nsync_net.exe --ticks 1200 --latency-ms 150 --jitter-ms 30 --loss-percent 10 --duplicate-percent 10
```

`--latency-ms` 是 RTT，`--jitter-ms` 是单向抖动。输出包含每个客户端的 ack、实际回滚、correction/desync、NetGraph 样本数和最终预测是否与服务器一致。

## 测试

```powershell
ctest --preset debug --output-on-failure
```

日常快速回归（不跑 WAN/soak）：

```powershell
.\scripts\test_fast.ps1 -Config Debug
```

快速集带 `FAST` 标签（11 项，约 28s Debug / 1s Release）；真实进程、WAN、10 分钟 soak 带 `SLOW`，网络类带 `NETWORK`。

包含：

- `nsync_core_tests`：定点数、几何、PCG32、实体池、字节流/快照、快照恢复/战斗统计、replay、golden replay、地图 JSON/二进制解析、地图 objective、内容 JSON 校验、英雄图集/动画数据、动画状态选择、三武器/爆炸、K/D/DMG/BLOCK 归属、护盾吸收/回盾、三英雄 9 个 gadget 技能（近战/无敌/增伤/手雷/静电/冲刺/火箭/减速/回血）、技能输入边沿/冷却/快照、TDM 计时与胜负、控制点捕获/计分/快照、2v2 机器人确定性、本地网络预测/回滚/10 分钟测试、10 场并发 LocalSession、协议编解码/fuzz、服务器选图与客户端自动跟随、输入/包限流器、快照哈希/编码访问器、崩溃报告 writer/最近日志/最后快照哈希、版本比较/更新清单、地图/内容 fingerprint、服务器时钟、配置、目录/pak 资源挂载、资源文件监听/热重载变化检测
- `nsync_replay_golden`：120 tick golden replay + 双跑一致性
- `nsync_content_check`：英雄/武器 JSON 校验和默认内容一致性
- `nsync_atlas_check`：84 张英雄源帧解码、图集布局和元数据校验
- `nsync_map_compile*`：全部地图 JSON → bin 回读校验（arena、world1、crystal_01/02、world3/5/6/8/9/10、world_flag_capture）
- `nsync_net_map_world1`：world1 地图的 2 人 loopback 同步 smoke
- `nsync_crystal_loopback`：crystal_01 地图 + CRYSTAL 模式的真实 loopback 同步 smoke
- `nsync_control_world5_loopback`：world5 地图 + CONTROL 模式的真实 loopback 同步 smoke
- `nsync_client_help`：客户端参数入口
- `nsync_server_smoke`：服务器 headless 跑 5 tick
- `nsync_net_smoke`：150ms RTT/30ms 抖动的 600 tick 本地网络测试
- `nsync_net_loopback`：真实 UDP/KCP loopback，2 客户端 300 tick，输入同步模型（`serverLag=8`/`clientEventLag=3`）
- `nsync_net_map_arena02`：使用 `assets/maps/arena_02.json` 的 loopback 地图加载/运行 smoke
- `nsync_net_latency_50` / `nsync_net_latency_150`：同一 loopback 在客户端 KCP 模拟 50/150ms RTT；0ms 由 `nsync_net_loopback` 覆盖，三项均要求 `corrections=0`、`desyncs=0`
- `nsync_net_loopback_legacy`：同一 loopback 的 `legacy` 对照路径
- `nsync_net_late_input_replay`：`--client-lag --rtt-ms 50` + 服务器 `late_input_replay` 原型，检查无 corrections/desync
- `nsync_net_sync_time_to_step`：`--client-lag --sync-time-to-step-ms 100`，检查 STS 到有效 lag 的映射
- `nsync_control_loopback`：真实 UDP/KCP loopback，控制点模式 360 tick
- `nsync_lobby_loopback`：大厅、10 slot、2 真人 + 8 机器人、Ready/倒计时/结算
- `nsync_lobby_reconnect`：对局中断线、机器人接管、token 重连
- `nsync_lobby_rematch`：结算后自动 Rematch，连续多局
- `nsync_lobby_5v5`：10 个真人 slot 的 5v5 大厅/对局快速验证
- `nsync_lobby_5v5_client_lag`：10 真人 5v5 + 50ms + client-lag/late replay 的同步回归
- `nsync_server_net_smoke`：`nsync_server --net` 启动/退出
- `nsync_net_real_process`：独立 `nsync_server` + 2 个真实 `nsync_net_real --client` 进程，50ms 模拟 RTT，检查会话/metrics
- `nsync_net_real_process_10p_150`：独立 server + 10 个真实 client，150ms 模拟 RTT，检查会话/metrics
- `nsync_net_real_process_multi_match`：2 个真实 client、0ms、短对局 + `--auto-rematch`，检查进入第 2 场
- `nsync_net_real_process_step_scheduler`：真实 client + `--client-lag --fixed-step-scheduler`，50ms、STS +100ms，检查 step 调度生效且无 corrections/desync
- `nsync_net_real_process_wan_250`：真实 client、250ms 模拟 WAN、client lag + scheduler，检查会话与同步指标
- `nsync_net_real_process_wan_400`：真实 client、400ms 模拟 WAN；覆盖高 RTT 重复登录/进房回归
- `nsync_net_real_process_wan_loss`：250ms + 30ms jitter + 5% loss + 10% duplicate，检查 KCP 恢复与 `lost/dup` 统计
- `nsync_net_real_process_wan_burst`：250ms + 30ms jitter + 3% loss + 4 包 burst + 5% duplicate，验证连续丢包恢复
- `nsync_net_real_process_wan_spike`：250ms + 周期性 300ms 延迟尖峰（每 1.5s 持续 300ms），验证 transient spike 下预测恢复
- `nsync_net_real_process_wan_trace`：按时间轴 trace 回放 250ms、550ms 尖峰、5%/10% loss 与 5% duplicate，验证真实 WAN 时间线恢复
- `nsync_net_real_ack_rewrite`：late replay 重写快照历史后，早于重写点的旧 ACK 单独统计，不再误报为真实 desync
- `nsync_net_real_process_10_matches`：10 个独立 server + 20 个真实 client，150ms、900 tick，检查 10 场同时开局与 corrections/desync
- `nsync_soak_10_matches`：10 场并发 LocalSession 的 1 分钟回归；10 分钟版本 Release 手动通过
- `nsync_soak_10m`：10 分钟模拟时间、10 玩家 soak
- `scripts/real_process_soak.ps1`：真实 server + N client 长时间 soak，采样内存/句柄并检查 Rematch、corrections/desync
- `scripts/real_matches_soak.ps1`：N 个独立 server + N×clients 并发对局，采样总内存/句柄
- `scripts/client_perf.ps1`：真实客户端连接 server，输出每 5s FPS/上下行带宽曲线和最终平均值；`-GpuVendor intel` 可切到集显测试
- `nsync_soak_smoke`：1 分钟模拟时间、10 玩家的 soak 快速验证

以下为启用 client lag + scheduler + 服务器 late-input replay 的历史生产矩阵（当前默认关闭客户端 lag/scheduler）在 10 人 600 tick 的 0/50/150ms 矩阵中 `abs(server-local-lag)=1/0/2`、`rejected=0`、`late_replay_failures=0`、`corrections/desync=0`；50ms 有 15 次预测 rollback。250ms 真实 10 人 WAN 仍 `corrections/desync=0`，rollbacks 56–79；400ms 2 人 WAN 通过（修复高 RTT 重复登录泄漏 slot）。10 人 250ms/612s soak 内存/句柄稳定，17 场 Rematch 无 corrections/desync；250ms + 30ms jitter + 5% loss + 10% duplicate 的 10 人/612s soak（18 场 Rematch）同样无 corrections/desync，KCP 重传正常且内存/句柄稳定。10 个独立 server + 20 个真实 client 的 150ms、900 tick 并发对局通过；late replay 重写快照历史后，早于重写点的客户端 ACK 会与修正后的 hash 不同，服务器将其计入 `rewritten_ack_mismatches` 并忽略，50/250ms 实测 `ack_mismatches=0`、rewritten 74–78，避免用旧 tick 权威快照制造无谓 corrections；250ms + 30ms jitter + 3% loss + 4 包 burst + 5% duplicate 的 10 人/314s soak 完成 9 场 Rematch，无 corrections/desync，lost≈9.3k、dup≈3.6k、retx≈1.1–1.4k，内存/句柄稳定；叠加每 2s 持续 300ms 的 +300ms 延迟尖峰后，10 人/314s、9 场 Rematch 仍无 corrections/desync，spike≈8.8–9.3k 包，内存/句柄稳定；用 300s 时间轴 trace（250/550/400ms、5%/10% loss、5% duplicate）10 人 316s soak 完成 8 场 Rematch，trace≈72k 包，lost≈0.5–0.6k、dup≈0.24–0.26k、retx≈1.1–1.2k，无 corrections/desync，内存/句柄稳定。同配置叠加 30ms jitter + 5% loss + 10% duplicate 也通过（10 场全部开局、无 corrections/desync，峰值总内存约 188 MB）。真实客户端 1280x720/no-vsync 平均约 1685 FPS、1920x1080 约 1849 FPS（本机 CPU 上限，RTT 16ms），对局内上下行约 7–16/38–111 kbps；切到 Intel UHD 630 后 720p 约 190–224 FPS、1080p 约 164 FPS，仍无 corrections/desync。Debug/Release CTest 均为 **40/40**；CI workflow 会分别构建和测试两种配置。`nsync_net_real` 输出 `smm/smp/sma` 细分快照 mismatch：50ms 2 人实测 `smm=0`（无缺失预测）、`smp>0`（周期快照因客户端 input lead 与服务器不同）、`sma=0`。

Phase 8 界面还做了手动冒烟：启动加载页 → 主菜单（HP/护盾/技能信息）→ SETTINGS（GAME/CONTROLS、音量、键位/JUMP→F、语言 EN→中文、保存）→ 部署加载页 → 对局 HUD（护盾条、E/F/G 三技能冷却与激活）→ 战斗表现（队伍色枪口/弹道、受击火花、火箭爆炸）→ 音频链路（SDL3 初始化/射击/爆炸/脚步/UI cue）→ Tab 计分板（K/D/DMG/BLOCK 随机器人战斗更新）→ 事件流 First Blood/连杀 → Esc 暂停；在线 `--connect --no-auto-ready` → WAR ROOM → R/Auto Ready → 倒计时 → 结算/Rematch。Debug/Release 客户端均能加载 `game.rml`、初始化 SDL3 音频并显示新界面。

## 配置

默认配置在 `config/`，构建后会复制到可执行文件目录。客户端/服务器优先读取可执行文件目录或源代码目录下的 `config/*.ini`。

客户端：

```ini
client.width=1280
client.height=720
client.vsync=true
client.fullscreen=false
client.blood=true
client.language=zh
client.max_frames=0
client.max_ticks=0
client.simulate_click=false
client.show_menu=true
client.hot_reload=false
audio.enabled=true
audio.volume=80
client.net=false
client.connect=false
client.auto_ready=true
client.auto_rematch=false
client.client_step_lag=false
client.fixed_step_scheduler=false
client.name=player
client.hero=scout
client.server_host=127.0.0.1
client.server_port=40100
client.net_latency_ms=150
client.net_jitter_ms=30
client.net_loss_percent=0
client.net_duplicate_percent=0
client.input_delay_ticks=3
client.map=maps/crystal_01.json
client.mode=crystal
simulation.tick_rate=60
simulation.max_catchup_steps=5
log.level=info
```

服务器：

```ini
server.tick_rate=60
server.max_catchup_steps=5
server.max_ticks=0
server.net=false
server.port=40100
server.players=2
server.map=maps/crystal_01.json
server.map_rotation=maps/crystal_01.json,maps/crystal_02.json
server.seed=1
server.snapshot_interval=3
server.input_sync_model=original
server.server_lag_ticks=8
server.client_event_lag_ticks=3
server.sync_period_ticks=30
server.match_duration_ticks=18000
server.score_limit=30
server.mode=crystal
server.lobby=true
server.lobby_countdown_ms=3000
server.match_end_delay_ms=3000
server.lobby_min_humans=1
log.level=info
```

命令行参数覆盖配置文件。

设置页保存的覆盖项写入基础配置同目录的 `config/client.settings.ini`，加载顺序为 `client.ini` → `client.settings.ini` → 命令行；键名示例：

```ini
client.width=1600
client.height=900
client.fullscreen=false
client.blood=true
client.vsync=true
client.language=zh
audio.enabled=true
audio.volume=80
input.left=A
input.right=D
input.jump=W
input.down=S
input.reload=R
input.weapon=Q
input.gadget1=E
input.gadget2=F
input.gadget3=G
input.scoreboard=Tab
input.pause=Escape
```

键盘值使用 SDL scancode 名称；保存文件只包含运行期设置，因此不会改写 `client.ini` 的注释或其他字段。界面语言可用 `client.language=zh|en`、`--language en|zh` 或设置页右上角按钮切换；默认中文；`client.language=en` 可切回英文。

## 内容管线

作者格式在 `assets/data/content.json` 和 `assets/maps/*.json`，运行时支持 JSON，也支持 `nsync_mapc` 生成的 `.bin`。

移动属性按 `velocities.move/jump` 表和 `steam` 1.3 倍系数换算为 60Hz 每 tick 值：Scout `run=0.114833/jump=0.1625`、Gunner `0.093167/0.169`、Assault `0.101833/0.169`。重力使用英雄配置的上升/下降不对称值（约 `1/142`、`1/97` 每 tick²），并支持地图的 jump pad（`jumpVelocity` 换算为每 tick 初速度）。生命/护盾暂保留当前武器数值下的平衡值；武器伤害尚未完整换算。当前内容库包含 Scout/Gunner/Assault 三个英雄、Rifle/Shotgun/Rocket/SMG/Laser（内容 id `burst`，对应 Assault 的 LaserGun1）/Grenade 六把武器和内置地图。`world1` 含 546 个 border solid、24 个平台、9 个 jump pad、10 个出生点；`crystal_01`/`crystal_02` 含 13 个水晶点、2 个中立 depot，均带美术；`world5`/`world6`/`world8` 是 Control 地图（含 `objective`）；`world3`/`world9` 是 Sphere 布局地图，`world10`/`world_flag_capture` 是 Flag 布局地图，Sphere/Flag 玩法尚未实现，目前只能以 TDM 加载其原始布局。所有地图均带美术；`arena_01/02` 保留为测试与沙盒地图。

可选地图：

| map | 说明 | 可用模式 |
|---|---|---|
| `world1` | 基础 PvP 布局 | TDM / Control |
| `world5` / `world6` / `world8` | Control 布局 | TDM / Control |
| `crystal_01` / `crystal_02` | 水晶布局 | TDM / Crystal |
| `world3` / `world9` | Sphere 布局 | TDM |
| `world10` / `world_flag_capture` | Flag 布局 | TDM |
| `arena_01` / `arena_02` | 测试地图 | TDM / Control / Crystal |

服务器用 `--map maps/world5.json --mode control` 指定单图，或用 `--map-rotation maps/world5.json,maps/world6.json,maps/world8.json` 每局轮换；客户端不需要指定服务器地图，会自动按 `map_id` 跟随。`world3/9/10` 的 Sphere/Flag 玩法尚未实现，目前以 TDM 加载原始布局。

### 技能槽

每个英雄有 3 个 gadget 槽；槽位定义来自英雄数据中的 `gadgets` 数组。只有前两槽带 `default`，第三槽没有默认值（通常为空），这里按 selector 第一项补位：

| 英雄 | 槽 1 | 槽 2 | 槽 3 |
|---|---|---|---|
| Scout | 红宝石之刃：近战 100，12s | 能量电池：短时无敌，15s | 磁力喷嘴：伤害 +60%，12s |
| Machine Gunner | 破片手雷：爆炸 160，11.5s | 静电护盾：20 + 眩晕，20s | 注射器：约 3s 后回复 250，20s |
| Assault | 火箭齐射：50 直击 + 25 范围，0.27s | 涡轮背包：开关式 8 向喷射（WASD），5s 燃料（启动需 ≥50），关闭后自动回充 | 坚韧：40% 减伤，12s |
| Grenadier | 震荡手雷：50 + 眩晕，11.5s | 破片地雷：爆炸 160，15s | 中和器：清除负面，10s |
| Cyborg | 回旋镖：80，19s | 陷阱：定身，10s | 双发系统：伤害 +40%，12s |
| Hermit | 喷洒器：50，15s | 化学物：中毒/减速，20s | 放血者：伤害 +35%，1.5s |
| Predator | 战轮：100，11.8s | 物质分解器：短时免伤，12s | 变异器：伤害 +10%，15s |
| Pyromancer | 恶魔面具：20，15s | 火焰陷阱：140 + 减速，20s | 化身：伤害 +50%，15s |
| Soldier | 火箭发射：46 直击 + 24 范围，15s | 防护机器人：50 + 减速，6s | 急救包：8 秒内每秒回复 25，20s |
| Warrioress | 超级加速器：冲刺 + 短时免伤，12s | 机械义肢：100 + 眩晕，20s | 抑制剂：清除负面，15s |

`assets/data/content.json` 中的单 `ability*` 字段保留为旧内容校验兼容字段；实际三槽技能定义在 `src/core/gadgets.h`。技能帧数按 ×2 换算为当前 60Hz tick；`Power Battery` 缺少 activeFrames 数据，当前用 2 秒近似；部分无直接等价状态的技能（护盾减伤、碰撞忽略、暴击等）用现有减伤/免伤/伤害加成近似。技能输入通过 `InputMessage` 的 `ActivateGadget + action_param=0/1/2` 同步，快照版本升到 **v15**（弹丸增加技能表现标签），协议仍为 v9；HUD 右侧三张卡显示 `E/F/G` 键、技能名、冷却秒数和激活状态。技能表现按类别区分：释放时按技能颜色播放脉冲/斩击/冲刺拖影/枪口闪光，持续型技能显示角色光环，投掷物按技能使用圆形/旋转/火箭形状以及对应颜色的拖尾、命中和爆炸特效。

地图 JSON 可带 `"objective": { "rect": [x, y, w, h] }`，控制点模式要求该区域存在。

```json
"objective": { "rect": [-2.5, 9, 5, 2] }
```

```powershell
.\nsync_datac.exe --in assets\data\content.json --check
.\nsync_mapc.exe --in assets\maps\arena_02.json --out assets\maps\arena_02.bin --check
```

`nsync_datac` 校验 ID 唯一、字段和数值范围，并输出内容 fingerprint；服务器和客户端登录握手会校验该 fingerprint。`nsync_mapc` 输出带 magic/version 的二进制地图并做回读校验。开发期使用 `--hot-reload` 时，客户端会重新读取 `content.json` 和当前地图；内容/地图变更只允许在直接模式生效，并会重置本地沙盒。

## 精灵图集与动画

英雄源帧位于 `assets/sprites/source/*.png`，图集元数据位于 `assets/sprites/heroes.atlas.json`，动画片段位于 `assets/data/animations.json`。客户端加载 `512x586` 图集并按 `idle/run/jump/fall/shoot/hurt/death` 状态选择帧；图集或动画缺失时自动回退到彩色矩形。

```powershell
.\nsync_atlas.exe --in assets\sprites\source --check
.\nsync_atlas.exe --in assets\sprites\source `
  --out assets\sprites\heroes.png `
  --meta assets\sprites\heroes.atlas.json `
  --image sprites/heroes.png
py -3 scripts\gen_placeholder_sprites.py
```

`nsync_atlas` 使用 STB 解码 PNG、shelf 布局打包并写出元数据；源帧名即图集帧名。当前占位美术为 3 英雄 × 7 状态 × 4 帧 = 84 帧，动画数据包含 21 个 clip。自制/预烘焙的成套帧仍可只替换源帧并重新打包，不动 C++；拆件资源（Body/Head/Shoulder/Hand/Foot）需要额外的 rig/分层渲染。

### 分层角色（方案 A）

第一版使用基础皮肤 1 的 8 个部件（脚、后肩、身体、前肩、头、后手、前手、武器）和武器 sprite。导入工具只在开发期运行，不进入运行时/CMake：

```powershell
py -3 scripts\import_original_heroes.py `
  --assets-root <导出的资源目录> `
  --sprite-root reference\onraid\extracted\sprites `
  --out-dir assets\generated\original_heroes `
  --rig-out assets\data\hero_rigs.json
```

生成物为 `assets/generated/original_heroes/heroes.png`、`heroes.atlas.json`、`assets/data/hero_rigs.json` 和手工维护的 `assets/data/hero_poses.json`。客户端按 `m_RD.m_Vertices` 与 prefab rest 变换绘制旋转 quad，z-order 为脚 0、后肩 2、身体 4、前肩 10、头 11、后手 21、武器 22、前手 23；不使用 Unity 运行时/Puppet2D。躯干与靴子本来就是分离的，不补假腿。动作是程序化近似，不是逐帧复刻；如需逐帧动作，需要补 `.anim`/FBX 或单独解码 streamed clip。

派生的角色图集当前按“仅本地开发/演示”处理；缺失或解析失败时 `auto`/`original` 会回退 84 帧占位图集，构建和测试不依赖这些 PNG 一定存在。`--hot-reload` 还会监听 `data/hero_rigs.json`、`data/hero_poses.json` 和角色图集/图片；F5 失败保留旧视觉。

### 地图美术

地图的背景、装饰和平台美术以 Unity 资源包（`worlds/<world>`）形式提供，开发期用下面的工具把目标场景的贴图导出成运行时资源：

```powershell
py -3 scripts\import_original_map.py --bundle worlds/world5 --map-id world5
py -3 scripts\import_original_map.py --bundle worlds/worldflagcapture --map-id world_flag_capture
```

工具只烘焙目标 scene 所在 CAB 的 SpriteRenderer，并跳过 `activeInHierarchy == false` 的对象。部分地图的资源包会通过依赖带入其他场景；如果不过滤，其他地图的桥梁、水晶、旗帜底座和机关会以“悬空图片”出现在当前地图。生成 `assets/generated/original_map/<map_id>_atlas.png`、`.atlas.json` 和 `assets/maps/<map_id>_art.json`。客户端加载 `maps/<map_id>_art.json` 后会自动用地图美术替换纯色 AABB 地图；资源缺失或解析失败时回退到旧的纯色地图。图层按 Unity 的 **sorting layer + sorting order** 排序（Background/Ground/Default/Hero/Decor），不会出现背景盖住可行走平台；`SpriteLightMaterial` 图层按加色混合绘制，背景层保留 `ParallaxLayer` 因子。

相机按 `MOUSE_REACTION` 模式实现：鼠标死区 1.2 单位、水平/垂直弹性 1/1.5、回中 Lerp 2/4、最大前移 ±6/±4 单位，并夹在地图边界内。`client.camera_zoom`（或 `--camera-zoom`）默认 1.0，与 `CameraScale` 一致；大于 1 会拉远视角。

`world1` 的 `border` 是零厚度线段，转换后的 AABB 约为 0.12 厚，桥面和崖顶齐平时会在接缝处多出约 0.06 的隐形台阶；斜线按 0.5 步长栅格化后，AABB 角点最多会比真实线段外凸约 0.33。角色移动仍使用 AABB 近似，`World::step` 允许已落地角色水平跨过不超过 0.12 的人造小台阶，因此木桥两端不会再把人卡住；真正的墙仍会阻挡。弹丸和命中射线改用地图新增的 `borders` 原始线段（`swept_point_hits_segment`），榴弹不会再在这些 AABB 台阶的“空气”里提前反弹。平台同样新增了 `platform_shapes`（原版四边形顶点），弹丸按四条真实边碰撞；倾斜平台不再用外接 AABB 判定，避免在平台端部的空三角区域提前反弹。角色移动仍使用 `solids`/`platforms` 的 AABB 近似。

线段/端点碰撞按原版 `Collider` 的语义做了方向过滤：只有速度方向真正指向该表面（`velocity·normal < 0`）时才判定碰撞，擦着平台端角或沿墙平行飞行不会再被端点圆角“粘住”弹回。平台的多边形边按原版 `collideRangeCP(..., lenient=true)` 做**单向**判定（顶点顺序给出的外法线，只从外侧撞；从平台下方/背面穿过不反弹），`border` 墙线仍按双向线段处理。`world1` 的平台数据此前与 `platform_shapes` 顺序/尺寸不一致，已按当前导入脚本重新对齐（平台、hatchway、跳板三者同一顺序）。

客户端渲染重力弹丸（榴弹）时不再做固定步插值，直接画在模拟位置上；否则画面里的榴弹会比碰撞体落后最多一个 tick（下落越快越明显），看起来就是“还没碰到墙/平台就反弹”。本地角色也按**模拟位置**绘制（远端角色保留插值），保证枪口与弹丸在同一时间基准上。原版榴弹只有弹壳精灵（`shellPrefabName` + `ArtifactGameObjectController`），**不画弹道曲线**，所以现在默认也不再给榴弹画曲线；需要调试时设 `NSYNC_SHELL_TRAIL=1`，曲线起点用 `projectile.origin`（真实发射点），不会再出现“起点对不上枪口”。设置环境变量 `NSYNC_COLLISION_DEBUG=1` 可把 `border`（绿线）和平台碰撞四边形（品红线）画出来对照。

弹道预览（默认关闭，原版没有）：游戏内按 `F6` 切换，或启动前设 `NSYNC_BALLISTIC_PREVIEW=1`。只对受重力的武器（榴弹）生效：从枪口按模拟参数（初速、重力、反弹次数、`normal_velocity`、生命、射程）逐 tick 预测，画出黄色细线与落点圆点，撞墙按 `restore=1/2` 反射，落地两次（`terrain_bounces`）或超出射程即停。用来补偿“原版直线瞄准 + 重力下坠”带来的偏低。

此前为修斜坡跳跃曾把 8 个 `y=33.54` 的 border solid 下移 0.7，导致 `x≈6.5 / 37.6 / 43.2` 一带的碰撞面偏低 0.7，角色会陷进草坡；这些 solid 已恢复到位，并新增 `test_world1_slope_surface` 回归测试。

分层角色 CPU 基准（10 玩家 × 9 部件、全部受击闪白，1280×720）：

```powershell
.\nsync_client.exe --no-menu --no-audio --hero-art original --benchmark-heroes --log-level info
```

Release 连续 3 次 avg 19.7–24.3 µs、p95 26.3–30.9 µs，低于 0.5 ms/帧预算；基准只覆盖分层角色绘制路径，不含 GPU/UI。详细结果见 [`docs/ORIGINAL_HERO_ART_RESULTS.md`](docs/ORIGINAL_HERO_ART_RESULTS.md)。

### 网络性能（Release）

```powershell
.\nsync_net_real.exe --loopback --lobby --players 10 --humans 10 --ticks 600 --match-ticks 300 --log-level warn
```

10 玩家/10 人类短时 loopback 两次实测：快照 **1.96–2.28KB**，服务器快照生成 **44–61µs avg / 103–624µs max**，含 10 客户端发送的广播 **786–877µs avg / 1.74–2.00ms max**；每客户端快照带宽 **18.5–22.1 KiB/s**（20Hz 约 38–45 KiB/s），低于 64 KiB/s 目标；`corrections=0`、`desyncs=0`。2 人类场景快照 1,354B、每客户端 12.8 KiB/s（20Hz 约 26.5 KiB/s）。服务器退出日志会输出 `snapshot_bytes`、`snapshot_gen_avg_us`、`snapshot_max_us` 等累计统计。

2 客户端 300 tick 延迟矩阵：0/50/150ms 模拟 RTT 均 `corrections=0`、`desyncs=0`；50/150ms 会保留预测 `rollbacks`（约 60–155），150ms 偶有迟到输入被服务器窗口拒绝（实测 0–6/约 400 input）。目标 RTT 50/150ms 时 KCP 实测约 77–96 / 170–192ms。

### 崩溃日志与对局摘要

客户端/服务器默认安装崩溃处理器，报告写入可执行文件旁的 `crash/`。Windows 未处理异常生成 `crash_<时间>_<pid>.txt` 和 `.dmp`；文本含异常码/地址、最后快照 tick/hash 和最近 256 条日志。可用内置测试触发一次（进程会退出）：

```powershell
.\nsync_server.exe --crash-test --log-level info
```

对局结束时服务器日志输出 `ticks/inputs/snapshots/snapshot_avg/ack_mismatches`，客户端输出 `corrections/rollbacks/desyncs/inputs/snapshots/snapshot_avg`。非 Windows 平台只提供 signal 文本报告，不生成 minidump。

### 版本与更新检查

```powershell
.\nsync_client.exe --version
.\nsync_client.exe --check-update --update-url https://example.com/nsync/update.json
.\nsync_server.exe --check-update --update-url .\update.json
```

版本来自 CMake `project(nsync VERSION ...)`，启动日志和崩溃报告都会带版本与构建配置。更新清单是 JSON：

```json
{
  "version": "0.1.1",
  "url": "https://example.com/nsync-0.1.1-win64.zip",
  "notes": "optional release notes"
}
```

比较版本时按数字段 `major.minor.patch`（允许省略尾部 0，不支持预发布后缀）。退出码：`0` 已是最新，`1` 有更新，`2` 检查失败。也可在 `config/client.ini` 或 `config/server.ini` 设置 `update.url=` 后省略 `--update-url`。Windows 使用 WinHTTP 支持 HTTP/HTTPS，其他平台目前只支持本地清单路径；检查只报告结果，不自动下载或安装。

### Windows 打包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

脚本构建 Release、收集 `nsync_client.exe`/`nsync_server.exe`、vcpkg 运行库 DLL、`assets/`、`config/`、`shaders/`、README 和 `scripts/deploy_server.ps1`，输出 `dist/nsync-0.1.0-win64/`、`.zip` 和 `.zip.sha256`。默认不打包派生角色图集，并把包内 `client.hero_art` 设为 `placeholder`；只有确认拥有分发权时才用 `-IncludeOriginalHeroArt`。包内程序仍需系统安装 Visual C++ 2015-2022 x64 运行库。

Windows CI 在 Release 测试后执行同一脚本并上传 zip artifact。

### 服务器部署

用发布包或 Release 构建安装到独立目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\deploy_server.ps1 `
  -PackageZip dist\nsync-0.1.0-win64.zip `
  -InstallDir C:\ProgramData\NSYNC\server `
  -Port 40100 -Players 10 -LobbyMinHumans 2 `
  -Install -Start -OpenFirewall
```

若已解压发布包，可直接在包根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\deploy_server.ps1 `
  -InstallDir C:\ProgramData\NSYNC\server `
  -Port 40100 -Players 10 -Install -Start
```

`-Install` 复制 server、DLL、assets、生成的 `config/server.ini`、部署脚本和 README；`-Start` 启动隐藏窗口进程并把 stdout/stderr 写入 `logs/`，PID 写入 `server.pid`。`-Status`、`-Stop`（强制结束进程）、`-Uninstall -Force` 分别查看、停止和卸载；`-OpenFirewall` 需要管理员权限，创建 UDP 入站规则。`-Status` 会从已安装的 `server.ini` 读取端口，所以后续不必重复传 `-Port`。

部署脚本不注册 Windows 服务，也不做自动重启；生产环境需用外部进程守护或计划任务托管 `nsync_server.exe --net --config config\server.ini`。默认只安装 server 所需文件，不含派生角色图集。

## 资源

开发期客户端优先挂载 `assets/` 目录；没有目录时回退到 `assets.pak`。启用 `--hot-reload` 时优先使用源码 `assets/`，避免修改源码后仍读取构建目录中的副本。

生成 pak：

```powershell
.\nsync_pak.exe assets assets.pak
```

pak 是当前自定义的未压缩格式（`ONRPAK1`），后续可替换成 zip/miniz，接口仍走 `ResourceManager`。

## 目录

```text
src/
  core/      定点数、几何、PCG32、实体、世界、快照、replay
  shared/    日志、断言、配置、路径、资源挂载、资源监听
  client/    SDL3/bgfx/RmlUi 客户端、GameUi/设置控制器、程序化音效
  server/    headless 服务器
  net/       协议、KCP 传输、真实服务器/客户端、本地预测、回滚
  tools/     pak 打包、replay、本地网络、真实网络工具
tests/       单元/确定性/golden/网络测试
assets/      RML/RCSS、地图 JSON、图集与占位资源
config/      client.ini / server.ini
docs/        设计与结果文档
shaders/     bgfx shader
```

## 当前边界

- 仅 DX11 shader，客户端强制 D3D11。
- 渲染/输入仍是占位级别；UI 为纯色 RmlUi 面板，已加入三技能卡但尚未加载英雄头像/技能图标纹理。战斗特效由程序化几何/线段叠加技能粒子纹理（`assets/sprites/particles.png`，48 个 `ParticleSystem` 材质贴图）组成，粒子的尺寸/速度/寿命按 prefab 层级做了近似。战斗角色默认使用基础皮肤分层角色（`auto`），缺失时回退 84 帧占位图集；逐帧动画、7 套皮肤、头像仍未接入，详见 [`docs/ORIGINAL_HERO_ART_REQUIREMENTS.md`](docs/ORIGINAL_HERO_ART_REQUIREMENTS.md) 和 [`docs/ORIGINAL_HERO_ART_RESULTS.md`](docs/ORIGINAL_HERO_ART_RESULTS.md)。
- 世界已加载地图 JSON/bin：实体 AABB、单向平台、10 个队伍出生点、可选控制点；客户端可本地移动/跳跃/瞄准/射击/换武器并跟随相机。三英雄、9 个 gadget 技能、TDM/控制点、计时/比分/胜负和 5v5 机器人补位已接入；设置页已接入基础音频/视频/键位，中文默认 UI、启动加载页和错误提示已接入；纹理粒子和邀请小队待补。
- 武器/命中/护盾/脚步/换弹/死亡/复活/UI 仍是 SDL3 运行时合成的短 PCM；技能释放/命中/爆炸已加载磁盘上的 WAV 音频。暂无背景音乐、环境循环、随机音源池/优先级/语音/多材质脚步。
- 设置页是简化版：单套键位而非 3 套 KeySet，没有音乐/announcer 开关、画质档位、鼠标键重绑或手柄设置；分辨率来自显示器 fullscreen modes 加当前窗口尺寸，全屏使用 SDL 当前显示器模式。
- 本地化是占位实现：内置 EN/中文 UI 文案，动态服务器文本、英雄名/技能名、事件流和部分状态仍为英文；中文依赖系统 CJK 字体，未做完整语言包/字体打包。启动加载页覆盖初始化后的启动过渡和本地部署，资源加载早期的致命错误使用原生错误对话框；组队/邀请/聊天仍为后续。
- 快照仍是全量、未压缩格式；已做每 tick 单次编码广播、哈希复用和发送路径预分配，实测每客户端低于 64 KiB/s。真实多进程 10 分钟 soak 已通过；尚未做 delta 编码、WAN 长时带宽/内存曲线；250/400ms WAN、丢包/抖动/重复与 10 场并发已做短测；250ms 10 分钟 soak 已通过。
- 崩溃报告是本地文本 + Windows minidump，未做符号化、自动上传、遥测后端或敏感信息过滤；非 Windows 只有 signal 文本报告。
- 更新检查只读取版本清单并报告，不下载/校验/安装更新；无签名验证、增量更新或强制升级；HTTP/HTTPS 依赖 Windows WinHTTP。
- 可视客户端支持 `--net` 本地权威测试台和 `--connect` 真实服务器；`--connect` 默认进入大厅并自动 Ready，`--no-auto-ready` 可手动操作。
- 在线入口仍由 `--connect` 命令行决定；在线主菜单、服务器地址输入、小队/邀请/聊天和账号系统未实现。
- 计分板已有 K/D/CausedDamage/BlockedDamage，但仍无 assists 和单人 Score；团队比分仍在计分板标题栏显示。
- 护盾/技能是确定性核心机制的简化版：盾先于 HP 吸收、脱战 1.75 秒后每 10 tick 回复 1 点；技能使用 9 个 gadget 名称/方向/主要数值与 3 槽 HUD，但仍为简化实现：技能释放/命中/爆炸已接入音效与粒子纹理，无技能图标/技能动画，粒子动力学按 prefab 层级近似（Unity `ParticleSystem` 序列化数据因 typetree 版本不匹配未能直接读取），部分持续时间和范围是近似，尚无充能层数或护甲屏/穿透。连杀提示由客户端根据公开 frags 派生，不伪造击杀归属。
- 战斗表现由客户端根据公开状态派生：弹药下降触发枪口、生命/护盾下降触发受击火花、弹丸消失触发命中或爆炸；没有服务器特效事件，也没有伤害数字。相机抖动/后坐力只改变世界渲染，不改变瞄准方向或模拟结果。
- 真实网络目前是单进程单对局，session token 仅做会话绑定和重连，没有加密、正式鉴权、排位、跨服匹配或 NAT 穿透；10 个真人 600 tick loopback、真实 10 分钟多进程 soak、250/400ms WAN、250ms 10 分钟 soak 与 10 场并发短测已验证；真实跨网/10 场真实进程并发未验证。
- 资源 pak 未压缩，仅用于验证挂载路径。
- 热重载是开发期客户端功能：源码 `assets/` 必须存在；内容/地图变更在联机模式下被拒绝；服务器地图轮换由 `server.map_rotation` 在每局开始时切换，修改轮换列表或内容仍需重启服务器；UI RML/RCSS 也需重启客户端。
