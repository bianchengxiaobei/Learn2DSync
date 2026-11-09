# NSYNC-LIKE 多人横版射击：设计与实施 TODO

> 版本：v1.76（服务器选图与客户端自动跟随）  
> 日期：2026-11-05  
> 目标平台：Windows 10/11 x64 优先，后续 Linux  
> 技术栈：C++20 / CMake / vcpkg / bgfx / SDL3（含 audio）/ RmlUi / KCP  
> 参考对象：NSYNC（Unity 5.4，Pragmatix，Steam AppID 511800）  
> 状态：Phase 2–7 通过；Phase 8 已完成 RmlUi 界面、K/D/DMG/BLOCK 战斗统计、护盾/First Blood/连杀、程序化战斗特效与表现层相机反馈、SDL3 程序化音效、设置页/音频视频与键位、中文默认本地化、启动加载页和错误提示；v1.67 已加入反编译原版 3 槽 gadget 技能；v1.68 修复联机输入卡键/整帧回滚抖动并默认关闭客户端 lag；v1.69 修复预测 tick 比较、迟到输入与服务器踩点输入导致的路径重复回拉；v1.70 增加 TICK/POS 与快照分叉诊断；v1.71 修复斜坡/薄桥起跳穿地；v1.72 校正远端玩家快照；v1.73 平滑玩家渲染；v1.74 降低远端校正开销；v1.75 加入原版 World2/World7 水晶地图与头顶水晶背包/数量；v1.76 加入服务器地图轮换、`map_id` 下发与客户端自动跟随；v1.77 修正原版地图美术串场景/悬空图片；组队仍留 Phase 8

---

## 0. 先读我：默认决策与待确认项

这份文档是“设计 + 实施 TODO”，不是代码。原则是**先设计、再验证、再实现**，每个阶段都有明确的交付物和验收标准，避免一开始就陷进网络同步和内容制作。

### 0.1 默认决策

| # | 项目 | 默认方案 | 备选 | 说明 |
|---|---|---|---|---|
| 1 | 语言标准 | C++20 | C++23 | C++20 对 MSVC/Clang/GCC 支持最稳 |
| 2 | 构建 | CMake + vcpkg manifest | 手工依赖 | 便于 Windows/Linux/CI 统一 |
| 3 | 窗口/输入 | **SDL3** | GLFW3 | SDL 自带手柄、音频、剪贴板、跨平台，适合游戏 |
| 4 | 渲染 | **bgfx** | — | 已确定 |
| 5 | UI | **RmlUi** | — | 已确定；需要自己实现 bgfx RenderInterface |
| 6 | 音频 | **SDL3 audio** | miniaudio | 复用现有 SDL3 依赖；程序化音效 + 软件混音，后续可换 |
| 7 | 游戏内传输 | **KCP（UDP）** | TCP/WebSocket | KCP 做可靠 ARQ；动作射击更适合 UDP；大厅后续可用 TCP/WS |
| 8 | 模拟频率 | **60Hz（已确认）** | 30Hz（原版） | 默认 60Hz 手感更好；保留 `kTickRate` 可切 30 |
| 9 | 玩法范围 | **只做 PvP；5v5 设计，MVP 2v2 + 机器人** | — | 不做 PvE/Boss/任务；先小规模验证帧同步，再扩到 5v5 |

### 0.2 已确认（本轮）

1. 窗口/输入：**SDL3**
2. 游戏内传输：**KCP over UDP**
3. 模拟频率：**60Hz**
4. 玩法范围：**只做 PvP，不做 PvE**；MVP 为 **2v2 + 机器人**，架构按 5v5 设计

### 0.3 待确认（默认值）

5. 美术先用**占位图/自制素材**，原版资源只做参考，是否同意？
6. 平台先做 **Windows x64**，Linux 服务器后续，是否可以？
7. 大厅/账号第一版是否允许用**本地假登录 + 命令行参数**，不做真实账号系统？
8. UI 是否确定 RmlUi（HTML/CSS 风格），而不是 ImGui/自绘？
9. 服务器是否接受**单进程单对局**起步，后续再做多对局调度？

> 以上默认值已经写进本文；你只需要指出要改的项。

---

## 1. 产品目标与范围

### 1.1 核心体验

做一个**横版 2D 竞技射击**：

- 2 队，每队 5 人（MVP 2v2 + 机器人）
- 侧视角、平台跳跃、空中射击、技能/道具
- 一局 5–10 分钟
- 服务器权威、确定性帧同步、输入延迟 + 回滚
- 大厅 → 匹配 → 装配/确认 → 对局 → 结算 → 再来一局

### 1.2 首个垂直切片（MVP）

只做一条最短闭环：

- 1 张地图
- 2 个英雄
- 3 把武器
- 1 种模式：团队死斗（TDM）
- 2v2，空位用机器人补齐
- **只做 PvP，不做 PvE / Boss / 任务**
- 本地/局域网 2 个客户端 + 1 个 headless 服务器
- 完整跑通：启动 → 登录 → 开局 → 击杀/死亡/复活 → 时间/分数结束 → 结算

### 1.3 明确不做（第一版）

- 任何 PvE、Boss、任务、剧情模式（本项目只做 PvP）
- 商城、皮肤、通行证
- 跨平台匹配、全球服务器调度
- 观战/回放编辑器（先保留 replay 日志）
- 复杂载具/破坏地形
- 手机端

### 1.4 成功标准

- 2 个客户端在 150ms RTT、30ms 抖动下打完 10 分钟，不出现永久不同步
- 服务器能纠正不同步，客户端能回滚并继续
- 新增英雄/武器/地图只改数据，不改核心代码
- 单人开发也能在 8–12 周内拿到可玩 MVP（不含美术打磨）

---

## 2. 对局设计

### 2.1 玩家与队伍

- 10 个玩家槽位，队伍 1 / 队伍 2 各 5 个
- 每队 5 个出生点；出生点在地图 JSON 里按“前 5 / 后 5”分配
- 死亡后按队伍出生点选择“离敌人最远”的点
- 复活时间：基础 10 秒，可按死亡次数递增（配置）
- 机器人补位：匹配不足时按难度补；玩家掉线后由机器人接管

### 2.2 模式

第一版只做 TDM：

- 击杀 +3，死亡 -2，自杀 -3，友伤 -3（可配置）
- 先到分数上限或时间结束，分数高者胜
- 首杀额外奖励

第二版加一个目标模式（三选一）：

- 控制点：站在区域内推进占领条（已实现）
- 水晶：拾取并带回己方 depository（已实现，`GameMode::Crystal`）
- 旗帜：夺旗并带回（未实现）

> 目标模式的世界插件（控制点/水晶/旗帜）都应是**确定性实体**，进入快照，不能是客户端本地特效。
>
> 水晶模式实现要点：`map.drop_spawns` / `map.drop_depots`、`content.drops` / `content.mode_rules.crystal`、`World::update_drops()`、快照 v16 / 协议 v13；地图二进制 v5。
>
> 已支持原版参数化：`DropDef.max_capacity`、`attach_score`、`hold_score` / `hold_score_ticks`、`deliveryScore`（`score_per_delivery`）、`dropPickUp` 四种规则；客户端使用原版水晶 sprite、拾取/交付音效、depot 脉冲与回点光柱。

### 2.3 英雄

第一版 2 个：

| 英雄 | 定位 | 生命/护盾 | 速度 | 武器 |
|---|---|---|---|---|
| Scout | 机动/狙击 | 100 / 0 | 快 | 步枪/霰弹/火箭 |
| Gunner | 重装/压制 | 140 / 0 | 慢 | 步枪/霰弹/火箭 |

> 当前实现先验证英雄属性差异；护盾、英雄专属武器和选择 UI 后续扩展。

后续扩展参考原版 9 个英雄的定位：Scout、Machine Gunner、Grenadier、Assault、Cyborg、Predator、Hermit、Pyromancer、Warrioress。

英雄数据字段建议：

```json
{
  "id": "scout",
  "hp": 220,
  "shield": 200,
  "speed": 53,
  "jump": 45,
  "gravity": -22,
  "hitbox": { "w": 0.65, "h": 1.4 },
  "weapons": ["rifle_1"],
  "gadgets": ["jetpack"],
  "sprite": "hero/scout"
}
```

### 2.4 武器

第一版 3 把：

- 步枪：中距离、连发、中等伤害
- 霰弹：近距离、多弹丸、强散布
- 火箭：慢速、范围伤害、自伤

武器字段建议：

```json
{
  "id": "rifle_1",
  "fire_rate_ticks": 6,
  "magazine": 30,
  "reload_ticks": 60,
  "damage": 12,
  "spread_deg": 2.0,
  "projectile_speed": 40.0,
  "projectile_life_ticks": 60,
  "sprite": "weapon/rifle_1"
}
```

### 2.5 地图

地图是手写数据，不运行时随机生成（第一版）：

- 世界边界
- 碰撞线段（地面/墙/斜坡）
- 单向平台
- 10 个出生点（前 5 队 1，后 5 队 2）
- 插件：补给点、控制点、水晶点、旗帜点（按模式）
- 背景层/装饰层
- 可选：AI 导航网格（后续机器人用）

坐标：y 向下，单位米；渲染时再转像素。

---

## 3. 技术架构

### 3.1 总体结构

```text
+-----------------------------+          +-----------------------------+
| Client                      |          | Headless Server             |
|                             |          |                             |
|  platform (SDL3)            |          |  match manager              |
|  render (bgfx)              | KCP/UDP  |  authoritative simulation   |
|  ui (RmlUi)                 |<========>|  validation                 |
|  audio (SDL3)               |          |  snapshot / reconnect       |
|  prediction / rollback      |          |                             |
+--------------+--------------+          +--------------+--------------+
               |                                        |
               +------------- shared core ---------------+
                     deterministic simulation
                     fixed-point math / snapshot / rng
```

### 3.2 进程与线程

第一版：

- 客户端：一个进程
- 服务器：一个进程，单对局
- 本地测试：一个进程内跑“服务器 + 2 个客户端端点”，方便调试
- 模拟：**单线程固定步长**，不要在模拟里开线程
- 网络 I/O：KCP 定时 `ikcp_update` + UDP 收包；主线程只消费事件
- 渲染：主线程；从最近两个快照插值

后续：

- 服务器一进程多对局（每对局一个线程或分进程）
- 客户端资源加载/解码放 job 线程，但不能碰模拟状态

### 3.3 模块划分

```text
src/
  core/       定点数、向量、几何、随机数、实体/世界、快照、哈希
  game/       英雄、武器、弹丸、伤害、复活、模式、机器人
  net/        协议、序列化、KCP 传输、会话、回滚、校验
  client/     bgfx 渲染、SDL 输入、RmlUi、音频、插值、调试绘制
  server/     headless 主循环、对局管理、匹配、结算
  shared/     配置、日志、断言、时间、文件、压缩
  tools/      地图编译器、图集打包、replay 工具
tests/
assets/
docs/
third_party/
```

命名空间统一 `nsync::core`、`nsync::game`、`nsync::net`、`nsync::client`、`nsync::server`。

### 3.4 关键依赖

- `bgfx` + `bx` + `bimg`
- `SDL3`
- `RmlUi`
- `KCP`（`ikcp.c` / `ikcp.h`，单文件可靠 UDP 实现）
- `SDL3 audio`（已接入；程序化 PCM + 软件混音）
- `nlohmann/json`（只用于工具/加载，不用于每帧模拟）
- `spdlog`
- `Catch2`（或 doctest）
- `xxHash`（快照哈希）
- `stb_image`（如 bimg 不够用）

### 3.5 构建

- CMake 3.24+
- vcpkg manifest 固定版本；bgfx/RmlUi/KCP 如果 vcpkg 版本不合适，允许 git submodule/vendored
- CMake Presets：`windows-x64-debug`、`windows-x64-release`、`linux-x64-release`
- MSVC `/W4 /permissive-`；Clang/GCC `-Wall -Wextra`
- 模拟核心单独编译成静态库 `nsync_core`，客户端/服务器都链接它
- 核心库禁止链接 SDL/bgfx/RmlUi/KCP（KCP 只属于 `net` 层）
- CI：Windows + Linux，跑单元测试和 deterministic replay

---

## 4. 确定性模拟设计

这是整个项目的根基。**先把单机确定性跑通，再谈网络。**

### 4.1 固定时间步

- 默认 `kTickRate = 60`
- `kFixedDt = 1/60`
- 渲染帧率与模拟解耦
- 主循环用 accumulator：

```text
accumulator += frame_dt
while accumulator >= fixed_dt:
    world.step(input)
    accumulator -= fixed_dt
render(interpolate(prev_snapshot, curr_snapshot, accumulator / fixed_dt))
```

- 模拟内**不读系统时间**；只读 tick
- 所有计时器用 tick 数，不用毫秒/秒

### 4.2 定点数

模拟内禁止 `float` / `double`。

建议：

- 位置/速度/尺寸：`Fix32`（Q16.16，int32 或 int64 中间运算）
- 角度：整数（0..65535）或 `Fix32` 弧度
- 三角函数：查表 + 线性插值，或 CORDIC
- 开方：整数牛顿迭代
- 物理常量全部用定点字符串/整数初始化

最低要求：

- 同一份输入、同一初始状态，在 Windows/Linux、Debug/Release 下 tick 哈希一致
- 如果做不到跨编译器一致，至少保证**同一构建**下确定；服务器和客户端使用同一构建配置

### 4.3 实体与组件

不要用指针地址、`unordered_map` 迭代顺序作为逻辑顺序。

建议：

- 实体 ID：`uint32_t`，低 20 位 index，高位 generation
- 组件存储：按类型分数组（SoA）或 sparse set
- 世界遍历：按实体 ID 升序，或按稳定的插入顺序
- 碰撞候选：按 ID 排序
- 快照：按实体 ID 排序输出

第一版可以不用“完整 ECS”，用简单的：

```text
World
  vector<Entity> entities
  vector<Transform> transforms
  vector<Body> bodies
  vector<Health> healths
  vector<Weapon> weapons
  ...
  free_list
```

关键是**顺序稳定**。

### 4.4 物理

第一版只需要：

- AABB + 线段/平台碰撞
- 重力
- 地面检测
- 单向平台（从下往上穿过）
- 斜坡可选
- 扫掠碰撞（swept AABB）避免高速穿透
- 每 tick 最多 4 次碰撞修正
- 角色之间可碰撞/可推开（第一版可先不碰撞）

不要用 Box2D 默认浮点版本；如果要用，必须确认确定性并固定版本/编译选项。第一版建议自己写，范围可控。

### 4.5 输入

输入是**意图**，不是状态：

```cpp
struct InputCommand {
    uint32_t tick;
    uint16_t buttons;   // left/right/up/down/fire/reload/gadget1...
    int16_t aim_x;      // 归一化方向，定点
    int16_t aim_y;
    uint8_t sequence;
};
```

- 移动是按下/抬起位
- 瞄准是归一化方向，量化到 `int16`
- 开火是按钮位；自动武器由模拟按 `fire_rate` 控制实际射速
- 客户端可以更频繁采样，但每个 tick 只提交一个合并后的命令

### 4.6 随机数

- 用 PCG32 或 xorshift128+
- 状态（seed/state）必须进快照
- 每局一个 seed；服务器生成，随初始快照下发
- 模拟内禁止 `rand()`、`std::random_device`、时间种子
- 需要“按 tick 可复现”的随机时，可以用 `hash(seed, tick, salt)` 派生

### 4.7 快照

第一版用**全量快照**，简单可靠：

```text
Snapshot
  version
  tick
  rng_state
  scores[2]
  players[]      // 英雄、生命、护盾、武器、位置、速度、状态
  projectiles[]  // 弹丸、手雷、火箭
  pickups[]      // 地图插件/掉落
  events[]       // 本 tick 产生的事件（可选）
```

- 小端序
- 自定义 `BitWriter` / `BitReader`
- 快照长度前缀
- 可选 LZ4 压缩（第一版可以先不压）
- 哈希：xxHash64 或 CRC32；每 N tick 计算一次
- 快照恢复后，渲染层根据实体 ID 重建/复用显示对象

### 4.8 回滚与重放

- 保存最近 2 秒的快照（60Hz = 120 帧）
- 收到迟到输入时：
  1. 找到输入目标 tick 的快照
  2. 恢复
  3. 重放到当前 tick
  4. 更新渲染
- 回滚次数、回滚深度要打点
- 回滚不能影响音频/粒子等纯表现层；表现层只做“追赶”

### 4.9 确定性检查清单

- [x] 模拟核心不包含 `float`/`double`
- [x] 不读系统时间
- [x] 不用 `rand()`/`std::random`
- [x] 不用指针地址做 key
- [x] 不依赖 `unordered_map` 迭代顺序
- [x] 实体/碰撞/快照输出顺序固定
- [x] 所有网络输入量化后再进模拟
- [x] 快照包含 RNG 状态
- [x] 有 golden replay 测试
- [x] Debug/Release 哈希一致
- [x] 客户端/服务器使用同一份核心库

Phase 2 验证：`nsync_tests` 覆盖定点/几何、PCG32、实体 generation/free-list、字节流、快照往返、恢复续跑、replay 往返和 golden replay；`nsync_replay --verify` 双跑一致。Debug/Release 通过同一组 golden 哈希。

---

## 5. 网络同步设计

### 5.1 拓扑

- 客户端-服务器，服务器权威
- 客户端只发输入
- 服务器跑同一套确定性模拟，广播输入事件 + 定期快照
- 客户端本地预测，服务器快照纠错
- 不做 P2P、不做客户端命中上报

### 5.2 传输层：KCP over UDP

默认所有游戏内消息走 **KCP（`ikcp.c` / `ikcp.h`）**，底层是 UDP：

- KCP 负责可靠、有序、重传、分片；它没有“连接、加密、通道”的概念
- 每条 UDP 包是一个 KCP segment；`conv` 作为会话 ID
- 应用层消息再带 `type`，相当于逻辑通道
- 第一版用一个 KCP 会话承载控制、输入、快照、聊天
- 控制/输入/聊天：可靠有序
- 快照：应用层 **latest-wins**，只保留最新 tick；客户端忽略旧 tick
- 如果实测快照被输入重传阻塞，再拆成两个 KCP 会话（`control` / `snapshot`），或快照改原生 UDP

KCP 参数建议：

```cpp
ikcp_nodelay(kcp, 1, 10, 2, 1); // nodelay, 10ms interval, fast resend 2, no congestion control
ikcp_wndsize(kcp, 256, 256);
ikcp_setmtu(kcp, 1200);
```

- `interval=10ms`：每 10ms 调一次 `ikcp_update`；客户端可在主循环每帧调用
- `resend=2`：快速重传，降低输入延迟
- `nc=1`：5v5 小流量先关闭拥塞控制；如果开放观战/大流量再打开
- `conv` 随机生成；服务器按 `(endpoint, conv)` 建会话
- 需要自己做：握手、session token、超时断开、心跳、重连、加密
- KCP 没有内置加密；第一版至少用 token 防串会话，正式版再加 XChaCha20/Poly1305

KCP 使用要点：

- `ikcp_input()` 收到 UDP 数据后调用；`ikcp_update()` 定时驱动（内部会 flush）；必要时手动 `ikcp_flush()`
- `ikcp_recv()` 一次取一条应用消息；KCP 保留消息边界
- 发送快照前，如果队列里还有更旧的未发快照，先丢弃旧的，只发最新 tick
- 客户端收到快照时，如果 `tick <= last_applied_tick`，直接丢弃
- 时钟同步的 `Ping/Pong` 第一版走 KCP；RTT 会包含重传，样本取中位数/低分位
- 如果后续要更纯净的 RTT，可以增加一个 raw UDP 探针（带 magic 前缀），不影响 KCP 主通道

为什么不用纯 TCP：

- 射击游戏怕队头阻塞
- KCP 在 UDP 上做可靠 ARQ，重传策略比 TCP 更适合低延迟
- 仍然保留输入延迟 + 回滚，不依赖传输层“绝不丢包”

传输层抽象成 `ITransport`，第一版 `KcpTransport`，后续可加 TCP/WebSocket 用于大厅。

### 5.3 协议

统一包头：

```text
magic      4 bytes  "ONRD"
version    1 byte
type       1 byte
flags      1 byte
length     2 bytes
sequence   4 bytes
payload    N bytes
```

这是**应用层包头**，位于 KCP payload 内部；外层还有 KCP 自己的包头（约 24 字节）。

消息类型：

| ID | 消息 | 方向 | 可靠性 |
|---|---|---|---|
| 1 | `Hello` | C→S | reliable |
| 2 | `Welcome` | S→C | reliable |
| 10 | `JoinMatch` | C→S | reliable |
| 11 | `MatchInfo` | S→C | reliable |
| 12 | `Ready` | C→S | reliable |
| 20 | `InputCommand` | C→S | reliable（KCP） |
| 21 | `InputBroadcast` | S→C | reliable（KCP） |
| 30 | `Snapshot` | S→C | reliable latest-wins（KCP，旧 tick 丢弃） |
| 31 | `SnapshotAck` | C→S | 可选诊断（KCP） |
| 32 | `SyncRequest` | C→S | reliable（KCP） |
| 33 | `SyncResponse` | S→C | reliable（KCP） |
| 40 | `Ping` | C→S | reliable（KCP；后续可换 raw UDP） |
| 41 | `Pong` | S→C | reliable（KCP；后续可换 raw UDP） |
| 50 | `MatchEnd` | S→C | reliable |
| 51 | `Reconnect` | C→S | reliable |

`InputCommand` 字段：

```text
tick        uint32
buttons     uint16
aim_x       int16
aim_y       int16
sequence    uint8
```

`InputBroadcast` 额外带 `player_id`、`server_tick`、`server_time_ms`。

`Snapshot` 第一版直接放全量状态；后续再做 delta。KCP 本身是可靠有序的，`latest-wins` 是应用层策略：发送前丢弃旧快照，接收端忽略旧 tick；如果 KCP 重传导致快照排队，再考虑把快照拆到独立会话或原生 UDP。

### 5.4 帧时间线

```text
Client tick N
  │
  ├─ 采样输入，生成 InputCommand(tick = N + input_delay)
  ├─ 本地立即应用一份（预测）
  ├─ 发给服务器
  │
Server tick N
  ├─ 收到输入，校验
  ├─ 广播 InputBroadcast
  ├─ 在自己的 tick N 应用输入
  ├─ 每 N tick 生成快照，计算哈希
  │
Client
  ├─ 收到 InputBroadcast
  │    ├─ 如果目标 tick >= 当前 tick：排队
  │    └─ 如果目标 tick < 当前 tick：回滚到目标 tick，重放
  ├─ 收到 Snapshot
  │    ├─ 比对哈希
  │    └─ 不一致：恢复服务器快照，重建显示
  └─ 渲染：最近两个快照之间插值
```

- `input_delay` 默认 3 tick（60Hz ≈ 50ms；30Hz ≈ 100ms）
- 服务器额外保留 `server_lag` tick 的缓冲
- 客户端回滚窗口 2 秒
- 服务器快照周期默认 60 tick（1 秒），可调
- 哈希每 30 tick 一次
- 客户端每 500ms 发一次 `SyncRequest`，校验一个较老的 tick

### 5.5 客户端预测与纠错

- 本地输入立即应用，保证手感
- 服务器回显 `InputBroadcast` 按 `(player_id, sequence)` 确认
- 如果服务器给的目标 tick 与本地不同，回滚到服务器 tick 重放
- 如果服务器快照哈希与本地不同：
  - 恢复服务器快照
  - 重放已确认的本地输入
  - 渲染层 `resync`
- 记录 `rollback_count`、`desync_count`、`snapshot_bytes`、`rtt`

### 5.6 时钟同步

- `Ping` 带客户端发送时间；`Pong` 带服务器时间
- 客户端估计：
  - `rtt = now - send_time`
  - `offset = server_time - (send_time + rtt/2)`
- 保留最近 N 个低 RTT 样本，取中位数/线性回归
- 用 `server_time + offset` 安排 tick
- 每 5–10 秒重新采样

### 5.7 重连

- 登录时发 `session_token`
- 断线后客户端带 token 重连
- 服务器找到对局，发当前 `Snapshot` + `tick` + `input_delay`
- 客户端恢复，进入 `Resyncing` 状态，收到下一份快照后恢复输入

### 5.8 服务器校验

服务器必须校验：

- 发送者是否拥有该 `player_id`
- `sequence` 是否在窗口内（防重放/乱序）
- `tick` 是否在允许窗口
- 输入频率是否超限
- `aim` 向量长度是否合法
- 武器是否存在、是否装备、是否有弹药、是否在冷却
- 移动/跳跃是否满足状态（例如眩晕时不能开火）
- 不信任客户端上报的命中、伤害、位置

### 5.9 带宽预算（目标）

- 10 玩家、60Hz、每玩家每秒约 10–20 条输入
- 单条输入 ~16 bytes；广播 ~20 bytes
- 输入广播总带宽：10 × 15 × 20 ≈ 3 KB/s
- 快照：全量 5–20 KB，1 秒一次 ≈ 5–20 KB/s
- KCP 外层约 24 bytes/包，另有重传；以上是应用层目标值，实际以抓包统计为准
- 目标每客户端 < 64 KB/s

---

## 6. 渲染、UI、音频

### 6.1 bgfx

- 初始化：SDL3 创建窗口，把 native window handle 传给 bgfx
- 视图：
  - View 0：世界（正交相机）
  - View 1：UI（RmlUi）
  - View 2：调试绘制
- 着色器：
  - sprite：纹理 + tint + UV
  - line：调试线
  - 后续加 blur/glow 后处理
- 纹理：bimg/stb_image 加载 PNG；图集由工具生成
- 精灵批处理：
  - 动态顶点缓冲
  - 按纹理/材质/层级排序
  - 每帧重建或持久映射
- 相机：正交，跟随本地玩家，支持死斗观战切换

### 6.2 插值

- 模拟 60Hz，渲染跟随显示器
- 保存最近两个快照
- 渲染位置 = lerp(prev, curr, alpha)
- 不插值的东西：UI、准星、纯本地特效
- 回滚后不要平滑插值到错误位置；直接 resync 或短时间淡入

### 6.3 调试绘制

第一版就要有：

- 碰撞盒/平台线
- 玩家 ID/血量/武器
- 服务器 tick、客户端 tick、回滚次数、快照大小、RTT
- 输入延迟、丢包/抖动模拟

### 6.4 RmlUi

- 实现 `Rml::RenderInterface`（bgfx）
- 实现 `Rml::SystemInterface`（时间、日志）
- 输入：SDL3 鼠标/键盘/文本输入转发给 RmlUi
- 屏幕：
  - 启动/连接
  - 主菜单
  - 匹配/房间
  - 英雄/武器选择
  - HUD（血条、弹药、技能、比分、计时）
  - 计分板（Tab）
  - 结算
  - 设置/按键绑定
- UI 不参与模拟；所有数据来自客户端只读视图

### 6.5 音频

- SDL3 audio，第一版 2D 音效
- 客户端根据公开状态派生 cue，不修改模拟；参考 `SoundManager.PlayFxAtPoint`
- 距离衰减 17 世界单位、左右 pan、随机变体、每 cue 并发上限
- 声音不进入快照
- 已覆盖射击/命中/爆炸/脚步/换弹/技能/UI；设置页支持主音量和音效开关，背景音乐/announcer 语音仍无实际资源

### 6.6 输入

- SDL3 键盘/鼠标/手柄
- 鼠标瞄准：屏幕坐标 → 世界坐标 → 归一化方向
- 手柄：右摇杆方向
- 按键绑定存 JSON
- 回放时用录制的 `InputCommand`，不读真实输入

---

## 7. 数据与内容管线

### 7.1 数据格式

- 作者格式：JSON（可读、可 diff）
- 运行时格式：自定义二进制（快、无解析）
- 工具：`mapc`（地图编译）、`atlas`（图集打包）、`datac`（数据校验）

### 7.2 地图格式

```json
{
  "id": "arena_01",
  "bounds": [-14, 0, 28, 16],
  "solids": [
    { "rect": [-14, 11, 28, 1] }
  ],
  "platforms": [
    { "rect": [-8, 7.5, 5, 0.5] },
    { "rect": [3, 7.5, 5, 0.5] }
  ],
  "spawns": [
    [-5, 10.1], [-7, 10.1], [-9, 10.1], [-11, 10.1], [-13, 10.1],
    [5, 10.1], [7, 10.1], [9, 10.1], [11, 10.1], [13, 10.1]
  ]
}
```

- `solids`：实心 AABB，横向和纵向都阻挡。
- `platforms`：单向平台，只从上方落地，跳跃时可从下方穿过。
- `spawns`：固定 10 个，前 5 个给 Team 1，后 5 个给 Team 2；世界按队伍槽位分配。
- 坐标单位米，y 轴向下（与当前模拟的 gravity/jump 方向一致）。

### 7.3 英雄/武器 schema

- 用 JSON Schema 或手写校验器
- 加载时校验：ID 唯一、引用存在、数值范围合法
- 客户端/服务器共用同一份数据；服务器启动时也加载
- 数据版本号写入协议握手，不一致直接拒绝

### 7.4 图集与动画

- 角色采用模块化精灵（身体/头/手/腿/武器挂点）
- 第一版可以用整图序列帧，后续再拆模块
- 图集打包工具输出：
  - `atlas.png`
  - `atlas.json`（frame rect、pivot、duration）
- 动画状态机第一版可以手写：idle/run/jump/fall/shoot/hurt/death

### 7.5 资源管线

```text
assets_src/  (png/aseprite/json)
   │
   ├─ atlas 工具 → assets/atlas/*.png + *.json
   ├─ mapc 工具  → assets/maps/*.bin
   └─ datac 工具 → assets/data/*.bin
```

- 开发期支持热重载（文件变更 → 重新加载；客户端 `--hot-reload`/F5，源码 assets 目录）
- 发布期打包成一个或多个 pak（可用 miniz/zip）
- 资源版本号和协议版本号分开

---

## 8. 工程与质量

### 8.1 目录建议

```text
NSYNC_RE/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  cmake/
  src/
    core/
    game/
    net/
    client/
    server/
    shared/
    tools/
  tests/
  assets_src/
  assets/
  docs/
  third_party/
```

### 8.2 测试

- 单元测试：定点数学、几何、RNG、序列化、快照、哈希
- Golden replay：固定种子 + 输入日志 → 期望 tick 哈希序列
- 集成测试：headless 服务器 + N 个机器人，跑 10 分钟
- 网络模拟：延迟/抖动/丢包/重复/乱序
- 模糊测试：协议解析、快照解析
- 性能测试：10 玩家 + 200 弹丸，tick < 2ms

### 8.3 性能预算（第一版）

| 项目 | 目标 |
|---|---|
| 模拟 tick | < 2ms（10 玩家 + 100 实体） |
| 快照生成 | < 1ms |
| 快照大小 | < 16KB（未压缩） |
| 客户端带宽 | < 64KB/s |
| 服务器 CPU | 单对局 < 10% 单核 |
| 客户端帧率 | 1080p 60fps 起 |

### 8.4 日志与遥测

- 分级日志：trace/debug/info/warn/error
- 网络日志：tick、sequence、rtt、rollback、desync
- 对局结束输出摘要：tick 数、快照数、平均快照大小、回滚次数
- 崩溃时保存最近 N 条日志和最后一个快照哈希

---

## 9. 分阶段 TODO（按顺序执行）

> 每个阶段结束都要能运行、能验收；不要跳过 Phase 2 的确定性测试。

### Phase 0：决策与技术 Spike（1–2 天）✅

目标：确认技术选型可行，避免后面返工。

- [x] 确认 SDL3（已确认）
- [x] 确认 KCP over UDP（已确认）
- [x] 确认 60Hz（已确认）
- [x] 确认 MVP 范围：只做 PvP，2v2 + 机器人（已确认）
- [x] Spike：SDL3 + bgfx 打开窗口，清屏，画一个 sprite
- [x] Spike：RmlUi 显示一个按钮/面板
- [x] Spike：定点数加减乘除/开方/角度查表
- [x] Spike：两个进程用 KCP 互发消息，并模拟 100ms 延迟/5% 丢包

**交付物**：`nsync_phase0` 可执行程序 + `docs/PHASE0_RESULTS.md`。  
**验收**：窗口能开、sprite 能画、RmlUi 能点、两个进程能通信、定点测试通过。  
**结果**：全部通过。详见 `docs/PHASE0_RESULTS.md`。

### Phase 1：工程骨架（2–4 天）✅

目标：可持续开发的空壳。

- [x] CMake + vcpkg + presets
- [x] `nsync_core` 静态库（不依赖渲染/网络）
- [x] `nsync_client` / `nsync_server` 两个 target
- [x] 日志、断言、配置、文件路径
- [x] 主循环 + 固定步长 accumulator
- [x] 资源挂载（开发目录/ pak）
- [x] CI：Windows 构建 + 单元测试（workflow 已加；本地 CTest 3/3 通过）
- [x] `README`：构建/运行命令

**交付物**：客户端开窗，服务器 headless 跑 60Hz，本地 CTest 绿；GitHub Actions workflow 已就绪（仓库尚未接远端，未实际触发）。  
**验收**：一条命令构建；客户端/服务器都能启动；单元测试通过。  
**结果**：全部通过。详见 `docs/PHASE1_RESULTS.md`。

### Phase 2：确定性核心（1–2 周）✅

目标：不涉及画面，先把“可复现”做出来。

- [x] `Fix32` 定点数、Vec2、AABB、线段
- [x] 确定性 RNG（PCG32/xorshift）
- [x] 实体 ID / 组件存储 / 世界遍历
- [x] `InputCommand` / `Tick` / `World::step`
- [x] 快照 writer/reader
- [x] 快照哈希
- [x] replay：输入日志 → tick 哈希序列
- [x] 单元测试：定点、序列化、快照恢复
- [x] golden replay 测试
- [x] 确定性检查清单逐项打勾

**交付物**：`nsync_replay` headless 测试程序，输出每 tick 哈希。  
**验收**：同一输入日志跑两遍哈希完全一致；快照恢复后继续跑一致；Debug/Release 一致。  
**结果**：全部通过。详见 `docs/PHASE2_RESULTS.md`。

### Phase 3：单机玩法垂直切片（2–3 周）✅

目标：一张图、两个英雄、三把武器、能打死人。

- [x] 地图加载（JSON → 世界）
- [x] 角色移动/跳跃/地面检测/单向平台
- [x] 相机跟随
- [x] 武器：步枪/霰弹/火箭
- [x] 弹丸、碰撞、伤害、死亡
- [x] 复活、出生点选择
- [x] TDM 计分/计时/胜负
- [x] 简单机器人（巡逻/追击/射击）
- [x] 调试绘制（碰撞、血量、tick）
- [x] 音效占位（Phase 8 已替换为 SDL3 程序化音效）
- [x] 两个英雄的属性差异（Scout/Gunner；选择 UI 留到大厅）

**交付物**：单机 2v2 机器人对局。  
**验收**：能完整打完一局；5 分钟不崩；击杀/死亡/复活/结算正确。  
**结果**：通过。2v2 机器人、三武器、TDM 5 分钟/30 分、两个英雄、胜负和初始音效占位已接入（Phase 8 已替换为 SDL3 音效）；详见 `docs/PHASE3_RESULTS.md`。

### Phase 4：本地多人测试台（1 周）✅

目标：在同一进程里模拟“服务器 + 2 个客户端”。

- [x] `LocalServer` + `LocalClient` 端点
- [x] 输入队列、input delay
- [x] 快照广播/恢复
- [x] 客户端预测
- [x] 回滚 + 重放
- [x] 网络模拟器：延迟/抖动/丢包/重复/乱序
- [x] NetGraph：tick、rtt、rollback、desync
- [x] 自动测试：150ms RTT 跑 10 分钟

**交付物**：可配置网络条件的本地对战。  
**验收**：150ms RTT/30ms 抖动下无永久不同步；人为注入不同步能修复；回滚次数在可接受范围。  
**结果**：通过。`src/net/local_net.*`、`nsync_net`、`nsync_client --net` 和 `tests/test_net.cpp` 已接入；10 分钟预测测试无 correction/desync。详见 `docs/PHASE4_RESULTS.md`。

### Phase 5：真实网络（1–2 周）✓

目标：两个真实客户端 + 一个服务器。

- [x] KCP 传输封装（`ITransport` / `KcpTransport`）
- [x] KCP 会话：`conv`、握手、session token、超时、心跳
- [x] KCP 参数：`ikcp_nodelay`、窗口、MTU、分片、快速重传
- [x] 协议编解码 + 版本握手
- [x] 会话/玩家 ID/session token
- [x] 服务器对局循环
- [x] 客户端连接/断线/重连
- [x] 输入广播/确认
- [x] 快照下发/哈希校验 + 应用层 latest-wins
- [x] 时钟同步
- [x] 服务器输入校验
- [x] 传输统计/日志（RTT、重传、发送/接收、session）；pcap 抓包未做

**交付物**：`nsync_server --net` + 真实 KCP 客户端；2–10 个 slot。  
**验收**：本机 loopback 与两个独立客户端通过；断线重连恢复；服务器拒绝非法 token/slot/tick；10 分钟真实网络 soak 和 10 场并发留到 Phase 9。
**结果**：通过。`src/net/protocol.*`、`kcp_transport.*`、`real_server.*`、`real_client.*`、`server_clock.*`、`nsync_net_real` 和客户端 `--connect` 已接入。详见 `docs/PHASE5_RESULTS.md`。

### Phase 6：大厅与匹配（1–2 周）

目标：从启动到开局的完整流程。

- [x] 假登录（用户名/令牌）
- [x] 主菜单（启动即连接；轻量大厅 HUD，完整 RmlUi 菜单留 Phase 8）
- [ ] 小队/邀请（第一版只做单人）
- [x] 简单匹配（先到先开/填机器人）
- [x] 房间/装配界面（轻量 HUD：Ready、Scout/Gunner 选择）
- [x] 全员 Ready
- [x] 地图加载协调（地图 fingerprint + MatchStart 初始快照）
- [x] 对局结束/结算（MatchEnd + 比分/胜负）
- [x] 掉线机器人接管（超时/Disconnect 切换 bot）
- [x] 再来一局（Rematch/自动 Rematch）

**交付物**：5v5（真人 + 机器人）完整闭环。  
**验收**：从启动到结算无手工步骤；中途退出/重连正确。  
**结果**：核心闭环通过。10 slot、2 真人 + 8 机器人、Ready/倒计时、对局、结算、机器人接管、token 重连和自动 Rematch 已接入；Debug/Release CTest 11/11。详见 `docs/PHASE6_RESULTS.md`。完整 RmlUi 主菜单/房间、邀请小队和更长时间的 10 真人并发仍留后续。

### Phase 7：内容管线（1–2 周）

目标：新增内容不改代码。

- [x] JSON schema + 校验器
- [x] 地图编译器（JSON → bin）
- [x] 图集打包工具
- [x] 动画数据格式
- [x] 3 个英雄
- [x] 6 把武器
- [x] 2 张地图
- [x] 1 个目标模式（控制点）
- [x] 开发期热重载
- [x] 内容版本号/协议校验

**交付物**：数据驱动的内容库。  
**验收**：只加 JSON + 图集就能新增英雄/武器/地图；服务器/客户端数据一致。  
**结果（第四切片）**：STB 图集打包器、84 帧英雄图集、21 组动画 clip、角色动画状态机、直接模式 `--mode control`/`CTRL` HUD、`AssetWatcher` 和 content/map/atlas/animation 热重载完成；Debug/Release CTest 16/16。详见 `docs/PHASE7_RESULTS.md`。

### Phase 8：表现层与打磨（1–2 周）

目标：像个游戏，而不是调试器。

- [x] RmlUi 离线主菜单
- [x] RmlUi 在线房间/Ready/英雄选择
- [x] HUD：血条/弹药/比分/计时/控制点
- [x] 计分板、轻量事件流（阵亡/得分/复活）
- [x] 暂停、结算、Rematch/Main Menu
- [x] 计分板 frags/deaths/伤害列（K/D/CausedDamage）
- [x] 护盾/技能/连杀
- [x] 受击/枪口/弹道/爆炸特效
- [x] 相机抖动/后坐力（仅表现）
- [x] 音频：枪声/脚步/受击/UI
- [x] 设置：音量/画质/按键
- [x] 本地化占位（EN/中文）
- [x] 启动/加载/错误提示

**交付物**：可展示的垂直切片。  
**验收**：不用调试 UI 也能完整玩一局；界面不阻塞模拟。  
**结果（第一至第七切片）**：RmlUi 主菜单、在线 WAR ROOM、结构化对局 HUD、Tab 计分板、暂停和结算完成；**第二切片**加入确定性 K/D/CausedDamage 统计、弹丸发射者归属、快照 v5、协议 v5 和计分板战斗列；**第三切片**加入英雄护盾/回盾、Kickstart/Power Battery/Overcharge 主动技能、BlockedDamage 列和 First Blood/连杀事件，快照/协议升到 v6；**第四切片**加入队伍色枪口闪光、弹丸轨迹、命中/护盾火花、爆炸环/火球和纯表现相机抖动/后坐力；**第五切片**加入 SDL3 程序化武器/命中/爆炸/脚步/换弹/技能/UI 音效、随机变体和距离衰减/声像；**第六切片**加入 GAME/CONTROLS 设置页、音量/静音、分辨率/全屏/VSync/血效、键位重绑和 `config/client.settings.ini` 持久化；**第七切片**加入 EN/中文占位本地化、语言设置持久化、启动过渡、部署加载页、启动致命错误对话框和控制模式错误弹窗；Debug/Release CTest 16/16；手动验证中英文菜单/设置、语言保存、启动加载和部署加载。详见 `docs/PHASE8_RESULTS.md`。

### 插队需求：原版角色美术接入（方案 A 基础版已实现）

状态：**方案 A 基础版已实现，A6.1 性能实测通过；Debug/Release CTest 16/16。逐帧原版动画/7 套皮肤/分发授权仍待后续。**

目标：用反编译版本中的 Scout / MachineGunner / Assault 原始部件，替换当前 84 帧占位战斗角色；保留现有模拟、命中盒、快照、协议和确定性回放。

#### 第一版范围

- 3 个英雄，使用原版基础皮肤 1：`ScoutBody1`、`MachineGunnerBody1`、`AssaultBody1`。
- 原版部件分层绘制：Body / Head-Hat / ShoulderBack / ShoulderFront / HandBack / HandFront / Foot / 武器。
- 使用 prefab rest 变换 + `m_RD.m_Vertices` 作为部件布局；保留原版漂浮靴子风格，不补假腿。
- 动作使用程序化 pose：`idle` / `run` / `jump` / `fall` / `shoot` / `hurt` / `death`，不依赖原版 `.anim`。
- 武器接入 `rifle` / `shotgun` / `rocket` / `smg` / `burst` / `grenade` 六种原版 sprite；缺图回退隐藏。
- 队伍识别先用队伍色描边/脚下环 + 轻微 tint，不整张染色。
- 原版资源缺失或格式错误时自动回退当前 84 帧占位图集。
- 保留 `--hot-reload`/F5：rig、pose、部件图集可重载，失败保留旧资源。

#### 实施切片

- [x] A0 资源与方案调研：确认新 AssetRipper 导出的 prefab 层级、Sprite 顶点、部件 PNG；临时拼装三英雄 rest pose 验证通过。
- [x] A0.1 确认方案 A；确认当前 GLB 无动画、导出目录无 `.anim`/`.controller`，第一版不依赖原版 clip。
- [x] A1 导入器：`scripts/import_original_heroes.py` 读取 3 个 `*Pref.glb` + Sprite JSON + 已裁 PNG，输出原版图集和 `assets/data/hero_rigs.json`；三英雄 rest pose 已生成。
- [x] A2 运行时数据层：新增 `src/core/hero_rig.*` 的 rig/pose 解析校验；增加 `client.hero_art=auto|placeholder|original` 和 `--hero-art`；默认 `auto`，缺失回退占位。
- [x] A3 分层渲染：`WorldRenderer` 支持多部件旋转 quad、UV、z-order、整体翻转、原版 Y-up→Y-down、队伍脚下环和头顶标记；单批次纹理绘制。
- [x] A4 程序化动作：实现七状态 pose、状态切换、开火后坐力、受击 additive 闪白、死亡淡出；低帧率 delta 有上限。
- [x] A5 武器挂点：按 `player.weapon` 切换六种原版武器 sprite，统一 weapon attach，处理翻转和切枪。
- [x] A6 三英雄验收：Scout → Gunner → Assault 基础皮肤均已接入；960×600/1280×720 截图、占位回退、F5 热重载、Debug/Release CTest 16/16 已验证；性能单独列 A6.1。
- [x] A6.1 性能仪器化：新增 `WorldRenderer::run_original_hero_benchmark` 与 `--benchmark-heroes`；Release 在 10 玩家 × 9 部件 × 全部受击闪白（90 quad/帧）下连续 3 次 avg 19.7–24.3 µs、p95 26.3–30.9 µs，低于 0.5 ms/帧预算；Debug max 不作为准入指标。
- [x] A7 收尾：新增 `docs/ORIGINAL_HERO_ART_RESULTS.md`，更新 README/TODO；快照/协议/fingerprint/golden replay 未变。

估算：A1–A2 约 1–1.5 天；A3–A4 约 1.5–2 天；A5–A6 约 1–1.5 天；合计约 3–5 个工作日（不含原版动画精确复刻和 7 套皮肤）。

#### 交付物

- `scripts/import_original_heroes.py`（开发期工具，不进入运行时/CMake）。
- `assets/generated/original_heroes/heroes.png` + `heroes.atlas.json`（生成物是否提交按资源决策）。
- `assets/data/hero_rigs.json`（部件、顶点矩形、rest 变换、z-order、武器挂点）。
- `assets/data/hero_poses.json`（程序化 pose 关键帧）。
- `WorldRenderer` 的分层角色渲染与占位 fallback。
- 三英雄截图、性能记录、回归测试结果。

#### 验收

- 三英雄在 960×600/1280×720 完整显示，部件层级/比例正确，漂浮靴子保留原版风格。
- 七状态肉眼可区分；翻转、受击闪白、死亡淡出、武器切换正常。
- 玩家 AABB、射击、技能、比分、复活、控制点行为不变。
- `kSnapshotVersion=6`、`kProtocolVersion=6`、content fingerprint、golden replay 不变；Debug/Release CTest 16/16。
- 缺失原版资源时自动回退，不崩溃/黑屏；F5 热重载失败保留旧视觉。
- 10 玩家 × 最多约 9 部件时，客户端 CPU 增量目标 ≤0.5 ms/帧（实测记录）。

#### 不做

- 7 套皮肤/皮肤选择 UI、原版逐帧动画、原版骨骼蒙皮、头像/技能图标、粒子资源替换。
- 不引入 Unity 运行时、Puppet2D 或原版 prefab 作为运行时依赖。
- 不改英雄数值、武器数值、命中盒、快照/协议。

#### 待确认（后续版本）

- 原版派生图集/生成物是否允许提交到仓库或发行包；默认仅本地开发使用，CI 用占位回退。
- 是否坚持原版逐帧动作；若坚持，需要补 `.anim`/FBX 或单独解码 streamed clip。
- 7 套皮肤是否在第二版接入。

详细需求、资源实测、方案 A/B/D 见 [`docs/ORIGINAL_HERO_ART_REQUIREMENTS.md`](ORIGINAL_HERO_ART_REQUIREMENTS.md)；实现文件、验证命令、结果和限制见 [`docs/ORIGINAL_HERO_ART_RESULTS.md`](ORIGINAL_HERO_ART_RESULTS.md)。当前原版动作是程序化近似；逐帧原版动画仍需补 `.anim`/FBX 或解码 streamed clip。

### Phase 9：加固与发布（1–2 周）

目标：能给别人玩。

- [x] 协议模糊测试：`tests/test_net.cpp` 对全部协议 payload 解码器做确定性 truncation、追加字节、逐字节/长度字段变异和随机输入；`nsync_core_tests` Debug/Release 16/16 通过。
- [x] 输入校验/速率限制/反作弊基线：保留 slot/button mask/tick ahead 校验，新增每会话 `RateLimiter`；默认 600 packets/s、240 inputs/s，超限分别计入 `rate_limited_packets` / `rate_limited_inputs`；新增限流单测，覆盖窗口重置、超限和 32 位时间回绕。
- [x] 60 分钟 soak（模拟时间）：新增 `nsync_soak`；Release 运行 10 场并发 × 60 分钟模拟时间（2,160,000 ticks），wall 11.0 秒，0 mismatch/desync/correction；另加 `nsync_soak_smoke`（1 分钟模拟）。wall-clock/真实 UDP 60 分钟仍需单独跑。
- [x] 10 场并发（多进程/多线程）：新增 10 个独立 `LocalSession` 并发测试（10 线程 × 2 人类/10 玩家 × 600 ticks），验证各 session 服务器/客户端哈希一致、无 desync；真实 UDP 多进程并发仍待 soak。
- [x] 内存/CPU/带宽优化：快照每 tick 只保存/编码/哈希一次并广播给全部会话（原实现每客户端重复），ack 校验复用最近广播哈希，发送路径去掉 `Packet` 二次拷贝，`ByteWriter` 预分配；10 玩家/10 人类 loopback 600 ticks 两次实测快照 1.96–2.28KB、生成 44–61µs avg/max 103–624µs、广播 786–877µs avg/max 1.74–2.00ms（10 客户端），每客户端快照 18.5–22.1 KiB/s（20Hz≈38–45 KiB/s）<64 KiB/s，0 correction/desync；Debug/Release CTest 17/17。
- [x] 崩溃日志/遥测：新增 `shared/crash_handler`，客户端/服务器安装未处理异常处理器；Windows 写 `crash/crash_<time>_<pid>.txt` + `.dmp`，报告含异常码/地址、最后快照 tick/hash、最近 256 条日志；Windows `MiniDumpWriteDump`，非 Windows 为 signal 文本报告。`nsync_server --crash-test` 已验证报告/转储生成；新增崩溃报告 writer 单测。服务器/客户端对局结束输出 ticks/inputs/snapshots/snapshot_avg/ack_mismatches 或 corrections/rollbacks/desyncs 摘要。Debug/Release CTest 17/17。
- [x] Windows 打包：新增 `scripts/package_windows.ps1`，收集 client/server、vcpkg DLL、assets/config/shaders/README，生成 `dist/nsync-0.1.0-win64.zip` + `.sha256` 和 manifest；默认排除原版派生角色图集并把包内 `client.hero_art` 设为 `placeholder`，`-IncludeOriginalHeroArt` 需显式授权。实测包 3.29 MiB、SHA256 `02FCDF8A...F949`；解压后 client `--help`、2 帧运行和 server 5 tick 均通过；包内附 `scripts/deploy_server.ps1`；CI 增加打包/artifact 上传。
- [x] 服务器部署脚本：新增 `scripts/deploy_server.ps1`，支持从发布 zip 或 Release 构建安装 server/DLL/assets/生成 `server.ini`，提供 `-Install/-Start/-Status/-Stop/-Uninstall -Force/-OpenFirewall`，PID 和分离的 stdout/stderr 日志写入安装目录；实测安装到临时目录、UDP 40201 启动/监听、GUI client 连接并开局、停止卸载通过。脚本不注册 Windows 服务/自动重启，生产需外部守护。
- [x] 版本号/更新检查：版本来自 CMake `project(nsync VERSION 0.1.0)`，client/server 支持 `--version`，启动日志和崩溃报告带版本/构建配置；新增 `shared/update_check`，支持本地清单和 Windows WinHTTP HTTP/HTTPS，`--check-update --update-url` 读取 `{version,url,notes}` 并比较数字版本，退出码 0/1/2；配置文件增加 `update.url`；新增版本比较/清单/BOM 单测，并手动验证本地清单与本地 HTTP 清单。

**交付物**：可分发 MVP。  
**验收**：60 分钟无崩溃；10 场并发稳定；无永久不同步；日志可定位问题。  
**状态（2026-10-31）**：Phase 9 清单全部勾选；仍待真实验收的风险是 wall-clock 60 分钟/真实 UDP 多进程 soak、原版资源分发授权，以及崩溃报告符号化/自动上传。Phase 10 原版输入同步完整模型已实现并改为默认；`legacy` 路径保留。

### Phase 10：原版输入同步完整模型（已实现）

目标：用反编译版 `BattleMaster` 的服务器时间参数和输入帧调度，替换客户端本地 `input_delay` 简化实现。

实现结果：

- 协议 v7 的 `LoginResponse`/`MatchStart` 下发 `serverLag`、`clientEventLag`、`syncPeriod`；v8 后续增加 `sync_time_to_step_ms`（当前默认 0）。
- 服务器默认 `original`：`server_lag=8`、`client_event_lag=3`、`sync_period=30`，并回传/重连保持 timing。
- 普通输入按 `CurrentFrame + clientEventLag` 排帧（默认 lead=3）；仅 `SyncResponse` 后下一批输入使用完整恢复公式，默认恢复 lead=13。`--input-delay-ticks` 仅作为无服务器 timing 时的 fallback。
- 输入改为状态变化事件：只有按钮/瞄准变化才发送；服务器按事件目标 tick 持久化状态，迟到输入进入下一 tick 窗口。
- 远端迟到事件触发客户端回滚并重放；普通快照 mismatch 单独记录 `snapshot_mismatches`/`snapshot_restores`，权威 mismatch 才增加 `corrections`/`desyncs`。
- 客户端使用快照对齐 + 未来快照队列；不再强制开局等待 `serverLag`。
- 服务器快照历史支持按 ack/sync tick 回发，Ack 校验不再只比较当前 tick。
- HUD/日志输出 `SLAG/ELAG/RTTC/EST/CELAG/SYNC/STS/LEAD/Q/RB/ERB/CIN/SM/SR`；Debug/Release CTest **24/24** 通过，真实 UDP loopback 2 客户端 300 tick 在 0/50/150ms 模拟 RTT 下 `corrections=0`、`desyncs=0`；0ms `rejected_inputs=0`。

原版基线：

- `MultiPlayerGame.cs` 从服务器通知取得 `serverLag`/`syncPeriod`；在线 PvP `clientEventLag=3`。
- `BattleMaster.cs:222` 输入目标帧为 `CurrentFrame + serverLag + clientEventLag + clientEventLag/2 + 1`。
- `BattleMaster.cs`/`MultiPlayerGame.cs` 用 `serverLag` 修正客户端 `stepTime`，客户端模拟相对服务器有偏移。

详细步骤、文件、测试和验收见 [`docs/INPUT_SYNC_MODEL_PLAN.md`](INPUT_SYNC_MODEL_PLAN.md)。

- [x] 步骤 1：新增 `SyncTiming`、服务器配置/CLI、`input_sync_model=legacy|original` 开关；协议字段先只读。
- [x] 步骤 2：协议升 v7，`MatchStart`/`LoginResponse` 编解码三个 timing 字段并补模糊测试。
- [x] 步骤 3：服务器权威生成/下发 `serverLag`、`clientEventLag`、`syncPeriod`；输入窗口校验 + reconnect 回传。
- [x] 步骤 4：客户端按原版 `frameOnDesync` 分支计算 target；普通 lead=3，恢复 lead=13；`--input-delay-ticks` 仅 fallback。
- [x] 步骤 5：实现快照对齐 + 未来快照队列；普通/恢复 target 分支和事件式输入。
- [x] 步骤 6：回滚、Ack、Sync 适配事件目标和 timing；迟到远端事件回滚重放。
- [x] 步骤 7：重连、Rematch、Lobby→Match 边界清理 timing/快照队列/输入命令。
- [x] 步骤 8：日志/HUD 显示 `SLAG/CELAG/SYNC/LEAD/Q`；真实 UDP loopback 增加客户端 KCP 双向 RTT 模拟，0/50/150ms 2 客户端 300 tick 均通过 `corrections=0`、`desyncs=0`。模拟 RTT 下会保留预测 rollback，150ms 偶发迟到输入拒绝。
- [x] 步骤 9：验收通过后将默认模型切到 `original`，更新 Phase 5/README/打包；快照 v6、content fingerprint、golden replay 不变。

验收：

- 普通输入 lead 严格为 `clientEventLag`，恢复输入 lead 为 `serverLag + clientEventLag + clientEventLag/2 + 1`，不再由客户端固定 3/16。
- 客户端使用快照对齐/追帧，不再强制落后 `serverLag`；真实 UDP loopback 2 客户端 300 tick 在 0/50/150ms 模拟 RTT 下均为 `corrections=0`、`desyncs=0`。
- 真实 UDP 双客户端 timing 日志一致；无模拟 RTT 时 `rejected_inputs=0`，150ms 模拟下偶有迟到窗口拒绝；重连/Rematch 通过 loopback 回归。
- 保留换弹行为：打空自动换弹 + `R` 手动换弹；快照 v6 和 golden replay 不变。
- 默认已切到 `original`；`legacy` 路径保留回退。

### Phase 11：剩余原版对齐（Phase 0/1.5 完成；Phase 2.1 协议 v9 动作位已接入）

详细任务、顺序、门禁和验收见 [`NEXT_STEPS_TODO.md`](NEXT_STEPS_TODO.md)。

范围：Phase 0 统计口径/地图 fingerprint；Phase 1 客户端 `serverLag` 偏移 + `syncTimeToStep` + 迟到输入策略；Phase 2 原版 `ClientEvent` 动作事件；Phase 3 真实多进程/WAN/soak；Phase 4 文档与发布。传输层 TCP/WebSocket vs KCP/UDP 不在范围内。

- [x] Phase 0：统计口径、测地图 fingerprint、基线记录
- [x] Phase 1：原版客户端时间偏移（`--client-lag` + `--late-input-replay` 原型默认关闭；`syncTimeToStep` 映射为 `ELAG`，`--fixed-step-scheduler` 已接入 step 调度；真实 50ms STS ±100ms 验证通过；仍需降低 rollback 后再评估默认开启）
- [ ] Phase 2：原版动作事件
- [ ] Phase 3：真实环境验证
- [x] Phase 4：文档、发布包和 CI 收尾

---

## 10. 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| 定点数/确定性实现复杂 | 高 | Phase 2 先做最小核心 + golden replay；不达标不进入网络 |
| 网络同步难度高 | 高 | 先用本地模拟器验证；输入延迟/回滚参数可调 |
| KCP 可靠重传导致快照排队 | 中 | 快照 latest-wins；必要时拆独立会话或改原生 UDP |
| 物理/碰撞手感 | 中 | 先 AABB/线段，别一开始做复杂多边形 |
| 内容量失控 | 中 | MVP 只做 1 图 2 英雄 3 武器 |
| 美术资源 | 中 | 先用占位图；原版资源只做参考 |
| 服务器运维 | 中 | 第一版单进程单对局；后续再做调度 |
| 范围蔓延 | 高 | 每阶段有验收；未通过不进入下一阶段 |

---

## 11. 已确认与待确认

### 已确认

- 窗口/输入：SDL3
- 游戏内传输：KCP over UDP
- 模拟频率：60Hz
- 玩法范围：只做 PvP，不做 PvE；MVP 2v2 + 机器人，架构按 5v5

### 待确认（默认值）

5. 美术用占位/自制，原版只参考，是否可以？
6. Windows 优先，是否可以？
7. 第一版假登录 + 命令行，是否可以？
8. RmlUi 确定吗？
9. 服务器单进程单对局起步，是否可以？

确认后，下一步就是 Phase 0 的技术 Spike，不需要先写业务代码。

---

## 12. 附：本次 NSYNC 逆向结论索引

详细反编译和提取物已迁移到项目内的只读参考目录：

```text
E:\NSYNC_RE\reference\nsync\
  decompiled\         Assembly-CSharp 反编译源码（4,369 个 .cs）
  extracted\          1,226 个 JSON/bytes 配置
  extracted\sprites\  1,957 张 Sprite PNG
  extracted\map_textures\ 44 张地图图集
  tools\              提取脚本
  counts.txt          提取统计
```

`reference/` 不参与构建，并已加入 `.gitignore`；不要把原版反编译代码或提取资源提交到仓库。

关键参考（路径相对 `reference/nsync/`）：

- 原版帧同步核心：`decompiled/Assets.Script.Core.Scene/BattleMaster.cs`
- 原版模拟根：`decompiled/com.pragmatix.df.game/Game.cs`
- 原版快照：`decompiled/com.pragmatix.df.serializers/GameSerializer.cs`
- 原版时钟同步：`decompiled/Assets.Script.Core.Net/ServerTimeKeeper.cs`
- 原版匹配流程：`decompiled/Assets.Script.Main.Controllers/AssemblyController.cs`
- 原版射击：`decompiled/com.pragmatix.df.controllers/GunControllerImpl.cs`
- 原版护盾/护甲：`decompiled/com.pragmatix.df.armor/Armor.cs`、`decompiled/com.pragmatix.df.hero/HeroDef.cs`、`decompiled/com.pragmatix.df.controllers/HeroControllerImpl.cs`
- 原版技能/道具：`decompiled/com.pragmatix.df.hero/HeroDefAbility.cs`、`extracted/kickstart.bytes`、`extracted/power_battery.bytes`、`extracted/magnetic_nozzle.bytes`
- 原版 First Blood/连杀：`decompiled/PvpMessages.cs`、`decompiled/com.pragmatix.df.game.scores/PvpEvent.cs`、`decompiled/com.pragmatix.df.game/Game.cs`
- 原版开火/弹道/命中/爆炸表现：`decompiled/Assets.Script.Core.Scene.General.DisplayingEvent.DisplayingController/EffectOnFireController.cs`、`FiringLineController.cs`、`CollideEffectController.cs`、`DetonateEffectController.cs`、`ExplosionConfigurator.cs`
- 原版相机表现：`decompiled/BattleCamera.cs`、`decompiled/Assets.Script.Core.Scene.Control/CameraControl.cs`
- 原版音效管理：`decompiled/SoundManager.cs`、`decompiled/Sound.cs`、`decompiled/Assets.Script.Core.Scene.General.DisplayingEvent.DisplayingController/SoundController.cs`、`decompiled/UIPlaySound.cs`
- 原版武器音效事件：`extracted/rifle_1.bytes`、`pistol_fire.bytes`（`SoundController` + `weapon_*_shot`/`pistol_shot` 数组）
- 原版设置/键位：`decompiled/Assets.Script.Main.ViewModels.MainUI.Windows.Settings/SettingsWindow.cs`、`GamePage.cs`、`ControlPage.cs`、`SettingsTab.cs`、`decompiled/Assets.Script.Main.Models.Data.Settings/UserSettingsData.cs`、`decompiled/Assets.Script.Main.Controllers/PlayerSettingsController.cs`
- 原版协议常量：`decompiled/com.pragmatix.df.messages/Messages.cs`
- 原版地图：`extracted/World*.bytes`、`extracted/WorldFlagCapture.bytes`
- 原版配置：`extracted/games.bytes`、`extracted/bots.bytes`、`extracted/matchmake.bytes`

---

## 13. 版本记录

- v1.78（2026-11-08）：原版地图补给道具（`GESupplyGenerator`）。新增 `supply_gen` 导入与 `map.supply_spawners`（地图二进制 v8），从原版 World1/2/3/5/6/7/8/9/10/WorldFlagCapture 提取 5–8 个补给点；内容新增 `supplies`：`health_box`（原版 102，拾取回复 20% 最大生命并挂 `health_pump`：每 30 tick +75、持续 900 tick、受伤/满血移除）与 `double_damage`（原版 105，伤害 +100%、持续 600 tick）；生成器保持原版“同一时间只刷一个，拿走后按 min 间隔重刷”的确定性逻辑，机器人会在低血量/附近有道具时主动拾取。快照 v19、协议 v15；客户端绘制原版医疗箱/双倍伤害骷髅贴图并在 HUD 显示 `X2`/`+HP` 状态；新增 `test_supplies` 覆盖拾取、回血、双倍伤害与快照回读，golden replay 哈希已重生成。

- v1.75（2026-11-05）：原版水晶地图与携带显示。`World2`/`World7` 转换为 `crystal_01`/`crystal_02`（13 个水晶点、2 个中立 depot、原版 border/platform/jump pad/出生点），`crystal_01` 同时烘焙原版地图美术；`drop_depot` 支持 `team=0` 中立交付点，双方都可交付；携带水晶时按数量显示原版 `hero_crystal_drop_01/02/03` 背包 sprite，并在头顶绘制数量，血条自动上移避让；默认客户端/服务器配置改为 `crystal_01 + crystal`。同时导入原版 `World3/5/6/8/9/10/WorldFlagCapture`（`world3/5/6/8/9/10/world_flag_capture`，Control 地图带 objective），全部带原版美术；地图形状上限提升到 2048 以容纳 WorldFlagCapture。新增地图二进制回读、原版水晶地图 pick-up/deliver 单元测试、`nsync_crystal_loopback` 和 `nsync_control_world5_loopback`。
- v1.76（2026-11-05）：服务器选图与客户端自动跟随。协议 v13 在 `LoginRequest`/`LoginResponse`/`LobbyState`/`MatchStart` 中传递 `map_id`；服务器新增 `server.map_rotation` / `--map-rotation`，每局开始时轮换地图，并在登录、房间和开局消息中下发当前地图。客户端登录被 map fingerprint 拒绝时按服务器 `map_id` 从本地 `assets/maps/<id>.json|.bin` 自动加载后重试；MatchStart 换图时重新初始化本地世界并重载地图美术。默认服务器配置轮换 `crystal_01`/`crystal_02`；单图可用 `--map` 关闭轮换。新增 `test_server_map_autofollow` 覆盖客户端从 `crystal_01` 自动切换到服务器 `world5`。
- v1.77（2026-11-05）：修正原版地图美术串场景。`import_original_map.py` 现在按目标 scene 的 CAB 过滤，并跳过 inactive hierarchy，避免 World6/8/9/10 把 World2/World3/TutorialWorld/WorldFlagCapture 的桥梁、水晶、旗帜底座和机关烘焙进来；重新生成全部地图美术（含 `crystal_02`），减少多余“悬空图片”。
- v1.74（2026-11-02）：远端校正性能。`apply_snapshot` 在只合并远端玩家权威状态时不再调用 `record_predicted`，避免远端持续移动导致每个快照都保存/哈希完整世界而出现瞬时掉帧；本地预测历史、物理和哈希不变。远端渲染继续使用 v1.73 的按帧平滑。`nsync_tests`、core、golden replay、net loopback 通过。包 3.36 MiB，SHA256 `51FF9B6F687CB562243DAF9D0274CB341B7F792944CE0D066EC396B11C6EB811`。
- v1.73（2026-11-02）：玩家渲染平滑。远端玩家的权威快照每 3 tick 才更新一次，直接绘制会在高 FPS 下表现为 20Hz 的走路/跳跃顿挫。渲染层新增按帧时间的显示坐标：远端以约 62ms 时间常数平滑，本地用约 36ms 的轻微平滑；远端偏差超过 3 个单位、本地超过 1.5 个单位或死亡/重生时直接吸附，避免橡皮筋。只影响绘制，不改变物理/哈希。`nsync_client_help`、core、golden replay、net loopback 通过。包 3.36 MiB，SHA256 `779BBE58A4F8A7798C269CFC17C90918BD17B8A1BBD8A0589A4552587FC3DCCD`。
- v1.72（2026-11-02）：远端快照校正。周期快照在本地玩家匹配、但远端玩家状态不一致时，不再整包保留本地预测；现在把服务器远端玩家状态合并回当前世界（按快照 tick 到当前 tick 的远端速度外推），并记录 `RMC`。本地玩家不会被恢复或拉回，但另一个客户端看到的位置会收敛到服务器权威状态。新增远端快照合并回归断言；Release 全量 40/40。包 3.36 MiB，SHA256 `A9DDFD5071906528FF16ED1FD1366BF29BB72451BAF3C0AEB2C93F4BB58FAF36`。
- v1.71（2026-11-02）：修复斜坡/薄桥起跳掉出地图。根因是角色站在斜坡薄板上时包围盒轻微嵌入，按跳跃后竖直碰撞把脚下 solid 当成天花板，把角色推到 solid 下方并继续下落；水平碰撞也会把微小嵌入当墙面横向弹回。现改为结合上一 tick 包围盒判断进入方向：水平只在确实从侧面进入时阻挡，竖直下落只在从上方接触时落地，上升只在确实从下方接触时顶到天花板。新增 World1 斜坡跳跃回归测试；Release/Debug 关键 CTest 通过，Release 全量 40/40。包 3.36 MiB，SHA256 `E5149A1AF7F1F895772347B2B2840062DA2655E8F8EEBA3E2C332D2A7B94B975`。
- v1.70（2026-11-02）：联机分叉诊断。客户端状态栏新增 `TICK/POS`；设置 `NSYNC_NET_TRACE=1` 后，周期快照恢复时向 stderr 输出目标 tick、服务器/预测/本地玩家坐标、速度、落地状态和按钮位（最多 64 次）。用于定位特定地图区域反复回拉和掉出地图。包 3.36 MiB，SHA256 `A9AA093045A387EDF52FC2492C9EC99FB218CD3A187B6074BA080411C3277CF2`。
- v1.69（2026-11-02）：联机预测/迟到输入稳定性修复。周期快照改为用快照目标 tick 的预测状态比较本地玩家，不再拿当前 tick 与旧快照比较，消除移动/跳跃时每 3 tick 的假恢复；服务器接受目标 tick 等于当前待模拟 tick 的输入，避免踩点跳跃被推迟一帧；快照 seed 不再覆盖客户端已生成的本地命令；远端迟到输入排到下一本地 tick，不再触发本地回滚；客户端按 RTT 自动提高 input lead，并在落后时只补本地模拟步而非未来快照整帧前跳。10,000 tick World1 无延迟回归 `corrections/rollbacks/snapshot_restores=0`；100ms RTT + 20ms jitter + 2% loss 的 6,000 tick 回归 1 次真实恢复、0 次事件回滚。Debug/Release CTest 40/40；包 3.36 MiB，SHA256 `4C9A3D8FA87EC48E978DF4CEB093366F821009C8C1BCE50F912BCBA93DCF0B26`。
- v1.68（2026-11-02）：联机手感与输入卡键修复。客户端输入状态最多每 6 tick 重发一次，服务器按最新原始状态同步 held action 位，避免丢失释放事件后持续开火；失焦时清空输入并阻止未释放鼠标开火。默认关闭 client lag/fixed-step scheduler，客户端直接对齐服务器；周期快照仅在本地玩家状态明显不一致时恢复，避免远端差异导致整帧回滚。相机跟随改为按帧时间平滑。Debug/Release CTest 40/40；包 3.36 MiB，SHA256 `2F1F27EDDB1E8299F3468295427971C36A3F5DD8CA05927E44188125203CF762`。  
- v1.67（2026-11-02）：反编译原版技能槽与战斗 HUD。新增 `src/core/gadgets.h`，按 `scout/machine_gunner/soldier` 与 gadget 数据实现 9 个技能：Ruby Blade、Power Battery、Magnetic Nozzle、Frag Grenade、Static Shield、Kickstart、Rocket Launch、Protective Droid、First Aid；输入改为 E/F/G 三槽，使用 `InputMessage.action_param=0/1/2` 同步；快照升到 v7，新增技能冷却/激活/无敌/治疗/减速/眩晕/弹丸引信与反弹状态。HUD 改为右侧三张技能卡（中/英文名、按键、冷却秒数、激活/冷却条），默认语言改为中文。golden replay 哈希已重生成。Debug/Release CTest 40/40；包 3.36 MiB，SHA256 `3DD7AB1CEEA5B8212D7881045704A90C6AABBA04C2CDC175DECBD88ADCBC4310`。
- v1.66（2026-11-02）：反编译 World1 地图与移动属性。新增 `world1.json/bin`（546 border AABB、15 平台、9 jump pad、10 出生点）并设为默认；`Map` 支持 jump pad 解析/二进制/指纹/渲染/物理；移动速度、跳跃和上升/下降重力按 `environment.bytes` 换算；玩家碰撞半高调整为 0.7，golden replay 哈希更新。新增 `nsync_map_compile_world1`、`nsync_net_map_world1`。Debug/Release CTest 40/40。包 3.35 MiB，SHA256 `A5819F09245924E3932380CC54E4033DDA4937F22B0A4B4C01F9148AB0F53F34`。
- v1.65（2026-11-01）：延迟尖峰。`KcpTransport` 新增周期性 transient spike（`--spike-ms/--spike-interval-ms/--spike-duration-ms`，收发独立），新增 `packets_delayed_spike` 统计与 CTest `nsync_net_real_process_wan_spike`。2 人/600 tick/250ms + 300ms spike 通过；10 人/314s 叠加 3% loss + 4 包 burst + 5% duplicate + 每 2s 300ms spike 完成 9 场 Rematch，spike≈8.8–9.3k，无 corrections/desync，内存/句柄稳定。Debug/Release CTest 37/37。包 3.33 MiB，SHA256 `CA4EB09986B4C205BCD1948047E0392AC783E2469B2911BB3111F84A1317B399`。
- v1.64（2026-11-01）：late-replay ACK 归因。服务器新增 `rewritten_ack_mismatches`：快照历史被 late replay 重写后，早于重写点的旧 ACK hash 不再计为真实 `ack_mismatches`，也不再触发旧 tick 权威同步；新增 CTest `nsync_net_real_ack_rewrite`。2 人/600 tick/50ms 与 250ms burst 实测 `ack_mismatches=0`、rewritten 74–78，客户端无 corrections/desync。Debug/Release CTest 36/36。包 3.33 MiB，SHA256 `F15BF742658A9E0DAF7AD9E5DEF85F89E81B5477C8C16DCA84821E06D4B85B12`。
- v1.63（2026-11-01）：连续丢包 burst。`KcpTransport` 增加有状态 burst loss（收发独立），`NetClientConfig`/`nsync_net_real`/脚本新增 `--burst-length`；新增 CTest `nsync_net_real_process_wan_burst`。10 人 314s、250ms + 30ms jitter + 3% loss + 4 包 burst + 5% duplicate 完成 9 场 Rematch，`corrections/desync=0`，内存/句柄稳定。Debug/Release CTest 35/35。包 3.33 MiB，SHA256 `278478A9373907258F43B7E41F385D05ED4E6F61EEC548731F4306A64F932F6D`。
- v1.62（2026-11-01）：快照 mismatch 诊断。新增 `smm/smp/sma` 分类统计并在 `nsync_net_real`/图形客户端/单元测试输出；50ms 2 人实测 `smm=0`、`smp=164/169`、`sma=0`，根因为客户端 input lead 与远端输入到达时序，restore 后 ack hash 同步为服务器 hash。`real_matches_soak.ps1` 句柄增长阈值调整为 2048 以消除 30 进程启动抖动。Debug/Release CTest 34/34。包 3.33 MiB，SHA256 `E85BE06F81DA375DD3887BB91B626A0ED38B4AAF1159D64F20622B908A21FAD0`。
- v1.61（2026-11-01）：低端 GPU 性能。客户端新增 `--gpu-vendor intel|nvidia|amd|microsoft|software`，`client_perf.ps1` 支持 `-GpuVendor`；Intel UHD 630 实测 720p 平均约 190–224 FPS、1080p 约 164 FPS，`corrections/desync=0`；Debug/Release CTest 34/34。包 3.33 MiB，SHA256 `76345124575BAE7D1025E869F6D0D295738C8696372C7937062065026F7EB873`。
- v1.60（2026-11-01）：长时 WAN 与发布收尾。10 人 250ms + 30ms jitter + 5% loss + 10% duplicate 的 612s soak 完成 18 场 Rematch，`corrections/desync=0`，lost≈9.8k、dup≈18.2k、retx≈1.4k，内存/句柄稳定；10 场真实并发叠加同一 WAN 模拟通过。CI 增加 60 分钟 job timeout 和包内 client/server/manifest smoke。Debug/Release CTest 34/34。包 3.33 MiB，SHA256 `6C44CD2EB09E8C6C28C8EEFD9ADDB3D9FF4BAEB765523B3E19DE61353A962280`。
- v1.59（2026-11-01）：真实 10 场并发与客户端性能。新增 `scripts/real_matches_soak.ps1` 和 CTest `nsync_net_real_process_10_matches`（10 server + 20 client、150ms、900 tick），10 场全部开局、无 corrections/desync，30 进程峰值约 187 MB；新增 `scripts/client_perf.ps1`，图形客户端输出 5s FPS/带宽曲线，720p 平均约 1685 FPS、1080p 约 1849 FPS，对局内上下行约 7–16/38–111 kbps；修正 HUD FPS 统计为真实每秒帧数。Debug/Release CTest 34/34。包 3.33 MiB，SHA256 `9D426DCB57E376E71768B08076EB8227185E3812E7646A8BC13A5A0F02C36982`。
- v1.58（2026-10-31）：WAN 丢包/抖动/重复。`KcpTransport` 支持模拟 jitter/loss/duplicate，`RealClientConfig`/`nsync_net_real` 新增 `--jitter-ms/--loss-percent/--duplicate-percent`；新增 `nsync_net_real_process_wan_loss`。250ms + 30ms jitter + 5% loss + 10% duplicate 的 10 人/313s soak 无 corrections/desync，lost≈4.6k、dup≈8.5k、retx≈685，内存/句柄稳定。Debug/Release CTest 33/33。包 3.33 MiB，SHA256 `DEECA67FC964E5FFCC727B9C0E97700BF4D0C37C48AB795F66D822335C9D6D41`。
- v1.57（2026-10-31）：WAN 加固。修复高 RTT 下重复 Login 无 reconnect token 时重复占用 slot 的问题，同 peer 重复 Login 复用现有 session；新增 `nsync_net_real_process_wan_400`。10 人 250ms/612s soak：17 场 Rematch，`corrections/desync=0`，内存/句柄 30s 后稳定。Debug/Release CTest 32/32。包 3.33 MiB，SHA256 `6B9AA63706513571B5CB4177065ABFE5AB0304B2840219657EC5BE030033F3C5`。
- v1.56（2026-10-31）：Phase 3 WAN/并发短测。新增 `nsync_net_real_process_wan_250`（真实 2 client、250ms、client lag + scheduler）与 `nsync_soak_10_matches`（10 场并发 LocalSession，CTest 1 分钟；Release 手动 10 分钟）；10 人 250ms 真实测试 `corrections/desync=0`、rollbacks 56–79；10 场并发 `mismatches/desyncs/corrections=0`。Debug/Release CTest 31/31。400ms 下 10 client 进房超时，未纳入验收（v1.57 已修复重复 Login slot 泄漏）。包 3.33 MiB，SHA256 `D5C0659C5376970806908736E49C7FECBA1D3582D0BEDF3EBEEEE569B180BF65`。
- v1.55（2026-10-31）：Phase 1 默认切换。生产 `nsync_client` 默认 `client_step_lag=true`/`fixed_step_scheduler=true`，`nsync_server` 默认 `late_input_replay=true`；新增 `--no-client-lag`/`--no-fixed-step-scheduler`/`--no-late-input-replay`。`estimated_server_tick` 增加 snapshot lead 补偿。10 人 0/50/150ms 实测 `abs(server-local-lag)=1/0/2`、`rejected=0`、`late_replay_failures=0`、`corrections/desync=0`；rollbacks 0/15/1。Debug/Release 29/29。包 3.33 MiB，SHA256 `C9C9809C5BFF1122B2E8842AD26CF46D123932844E16C2BCD3419B7CCF8C5582`。
- v1.54（2026-10-31）：Phase 1.4 `fixed_step_scheduler`/STS 闭环。`RealClient::tick` 在实验开关下以 `estimated_server_tick - effective_lag` 为目标调度 0–4 个本地 step，输出 `steps/skips/debt/target`；`estimated_server_tick` 改为按快照接收时间外推并限制在 `snapshot_interval + RTT + 2` tick。真实 2 client/50ms、STS +100/-100ms 实测 `ELAG=2/14`、目标差 0–3 tick、`corrections/desync/rollback=0`。新增 `nsync_net_real_process_step_scheduler`；Debug/Release CTest 29/29。默认仍关闭；v1.55 已默认开启。包 3.33 MiB，SHA256 `78D78C47754351E20D97288C1F8E81389F2CBA9CAA6E45A4B76AED22FC824C5E`。
- v1.53（2026-10-31）：Phase 3.4 真实进程 soak。新增 `scripts/real_process_soak.ps1`：启动独立 server + N client，采样总 WorkingSet/句柄，检查自动 Rematch、corrections/desync 和增长阈值。10 client/0ms/612s 实测 16 场，稳态内存 74.5–80.4 MB、句柄 919，无持续增长；真实进程 10 分钟 soak 完成。  
- v1.52（2026-10-31）：Phase 3.3 真实进程多局与 soak。`nsync_net_real --client` 支持 `--auto-rematch`；新增 `nsync_net_real_process_multi_match`（2 client、0ms、短对局、2 场）和 `nsync_soak_10m`（10 分钟模拟、36000 tick、10 玩家）。Debug/Release CTest 28/28；真实进程 10 分钟 soak 仍待做。包 3.33 MiB，SHA256 `47C94F9B9A7361D7C19346F341199F6DD068A3FF95C1DE37EA19258DA0C90767`。  
- v1.51（2026-10-31）：Phase 3.2 十人真实进程。`real_process_smoke.ps1` 参数化 `ClientCount/RttMs/InputDelay/ServerTicks`，server 使用 `--lobby-min-humans=ClientCount`；新增 `nsync_net_real_process_10p_150`（10 client、150ms、180 tick）。Debug/Release CTest 26/26；10p Release 约 15s。包 3.33 MiB，SHA256 `D0080E282ADD27B0AE4F23A8F05A362A06827C0ECCB10B5265081A709AFAA1AC`。  
- v1.50（2026-10-31）：Phase 3.1 真实进程验证。新增 `scripts/real_process_smoke.ps1` 和 CTest `nsync_net_real_process`：独立启动 `nsync_server` + 2 个 `nsync_net_real --client`，50ms 模拟 RTT，检查客户端会话成功且 `corrections/desync=0`。Debug/Release CTest 25/25；脚本约 10–12s。包 3.33 MiB，SHA256 `50BBC69217ADC48EBCDFC148E80CE2BAC5BA2A7CDA349FDB6B47494D3AA17757`。  
- v1.49（2026-10-31）：Phase 2.3b 迟到动作回放。迟到输入的动作帧按 target tick 进入队列；`replay_late_inputs` 在恢复/重放前把动作帧套用到对应命令（toggle 状态 + 单 tick pulse），新增 `late_action_driven_inputs/late_action_fallback_inputs`。10 人 50ms client-lag 实测 `frames=990`、`late_driven=940`、`late_fallback=0`、`rejected=0`、`replay_failures=0`、`corrections/desync=0`。Debug/Release CTest 24/24；包 3.33 MiB，SHA256 `8DB4DB415F763A17033F423ECDF7B5533272ACE9DE96AEFFB28467848CBBAACB`。  
- v1.48（2026-10-31）：Phase 2.3 动作驱动输入。`NetServerConfig::action_input=true` 时，服务器维护 session 级 toggle 状态（移动/跳跃/下蹲/开火），用 `WeaponReload/ActivateGadget` 生成单 tick pulse，并用动作事件重建权威按钮位；原始状态用于瞄准和 fallback。`late_input_replay=true` 或迟到输入时回退原始状态（`action_fallback_inputs`），避免不完整事件回放；新增 `action_driven_inputs`。0ms 2p 实测 `driven>0`、`mismatches=0`；50ms client-lag 实测 `fallback>0`、`corrections/desync=0`。Debug/Release CTest 24/24；包 3.33 MiB，SHA256 `97E7CBED102D89CC1EDB78A543BA260A50C0B687BB16A8A24644D3CBF8E0DBB5`。  
- v1.47（2026-10-31）：Phase 2.2 服务器动作队列。新增 `ActionEventQueue`：按 `(target_tick, sequence, action_mask)` 去重、按 target tick 排序合并；服务器消费当前 tick frame，校验动作与状态按钮一致（`action_mismatches`）；新增 `action_duplicate_packets/action_event_frames/action_events_applied` 统计和队列单元测试。2p 50ms client-lag 实测 `duplicates=6`、`mismatches=0`。Debug/Release CTest 24/24；包 3.32 MiB，SHA256 `A0F401A91D61EE039A22397DE2BB4B994456780BA512B0C1DCAF4C69D49CD09A`。  
- v1.46（2026-10-31）：Phase 2.1 动作事件 wire。协议升 v9：`InputMessage` 增加 `actions`（15 位 `ClientActionMask`）、`action_toggles`、`action_param`；客户端从按钮边沿采集移动/跳跃/开火 toggle、换弹/gadget 按下事件；服务器统计 `action_packets/action_events/action_toggle_events` 并随 `InputBroadcast` 转发。状态输入仍是权威路径，未切换事件驱动。补协议 roundtrip 断言和 loopback 动作计数断言；Debug/Release CTest 24/24；包 3.32 MiB，SHA256 `D543AD618FDFB1DCF9B638656F96857C2042CB457A24C413F5E4EAB1E10F5CC3`。  
- v1.45（2026-10-31）：修复迟到回放 base 缺失。对局开始把初始快照加入 `snapshot_history_`，快照历史上限按 `late_input_replay_window*2` 提升；新增 replay 失败原因统计（base/window/restore/tick）。10 人 50ms client-lag 实测 `rejected=0`、`late_replay_failures=0`、`corrections/desync=0`、rollbacks=3、post_match=20。Debug/Release CTest 24/24；包 3.32 MiB，SHA256 `761CA194B7DD07B9596811F02A92251EB8610A6AFCEA88591BB4CCF8F34E751F`。  
- v1.44（2026-10-31）：服务器输入拒绝分类与 5v5 client-lag 回归。新增 `post_match/phase/malformed/slot/future/late` 拒绝计数；对局结束输入不再计入 `rejected_inputs`，并清理 pending late replay。新增 `nsync_lobby_5v5_client_lag`（10 真人、50ms、client-lag）CTest；10p 实测 `rejected=0`、post_match=20、corrections/desync=0、rollbacks=3。Debug/Release CTest **24/24**；包 3.32 MiB，SHA256 `B855AE53AAFF2D1C696E1509737D511FDC52F0DD13F015BF77689764DB49514E`。  
- v1.43（2026-10-31）：客户端跳过已被权威快照覆盖的远端输入。`queue_input_broadcast` 对 `tick <= last_server_tick_` 的输入只计数 `covered_inputs`，不再触发 rollback；HUD/工具输出 `CIN`。50ms 实测 `CIN=98`、rollbacks=0，150ms `CIN=94`、rollbacks=0；仍无 corrections/desync/rejected。Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `9FA32F12796133E1F0044C3DF06245E4528A5D4FA78453035164E98E33D5622A`。  
- v1.42（2026-10-31）：`estimated_server_tick` 目标加入 RTT 补偿，EST 更接近实际 server tick；50ms 一次 local/server=188/197（差 9），150ms=193/204（差 11），仍无 corrections/desync/rejected。Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `E01AF4752DE31BE7539181C278C8232224468177226CE163A20217B1FC72E16F`。  
- v1.41（2026-10-31）：客户端远端输入回滚合批。`LocalClient` 新增 `queue_input_broadcast`/`flush_pending_rollbacks`，`RealClient` 在一个 pump 内只按最早 target 回滚一次；新增双事件回滚断言。Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `AB408CFF54B9929E1CA52DE737AF4552AF528FC55242033EDAA5ECD925050776`。预测 rollback 仍存在。  
- v1.40（2026-10-31）：`estimated_server_tick` 闭环。客户端保存最后快照的 `server_time`，结合 `ServerClock` 偏移和 RTT/2 估算服务器当前 tick，并用它作为 `catch_up_lag` 的观测 tick；HUD/工具新增 `EST`。0/50/150ms 仍无 corrections/desync/rejected；包 3.32 MiB，SHA256 `CB40E969037A349159D22490D6AA69C8EFCF7DE782060CDBCE27D13F0D5AA462`。固定 step 调度仍未完成。  
- v1.39（2026-10-31）：加入 RTT/2 快照时延补偿。`rtt_compensation_ticks = rtt_ms * tick_rate / 2000`，`catch_up_lag` 目标改为 `highest_server_tick + RTTC - effective_lag`；HUD/工具输出 `RTTC`。50ms 实测 `RTTC=2`、local/server 差 9；150ms 实测 `RTTC=5`、差 11；仍无 corrections/desync/rejected。Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `C0C26575A631EF77269713DEDE6C879DA6F9045E519EF424B6D6F6AC6E091AB9`。固定 step 调度仍未完成。  
- v1.38（2026-10-31）：迟到输入回放合批。`RealServer` 将同一 update 收到的迟到输入排队，在 `simulate_tick` 一次 restore 后按各自 target tick 重放；失败时恢复 `input_events`/所有 session 的 `input_history`/`input_state`。新增 `late_replay_batches` 统计；50ms 实测 `late_replays=212`、`late_replay_batches=148`。Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `509CC08E93F6C4F012E584CD53B8C243B693C54D1D14215FDA41CF69A3B09F82`。回放 rollback 仍高，默认关闭。  
- v1.37（2026-10-31）：`syncTimeToStep` 进入有效 lag 映射。客户端按 `server_lag - sync_ms * tick_rate / 1000` 计算 `ELAG`，参与 `begin_alignment`、超时和 `catch_up_lag`；服务器新增 `--sync-time-to-step-ms`/`server.sync_time_to_step_ms`；HUD/工具输出 `ELAG`。新增 `effective_server_lag_ticks` 单元断言和 `nsync_net_sync_time_to_step`；Debug/Release CTest 23/23；包 3.32 MiB，SHA256 `9452FEE25356AEA2063E8A86010988EE202EF4D556D702B0C1827F068A26402B`。固定 step 调度/accumulator 和真实 RTT 闭环仍未完成。  
- v1.36（2026-10-31）：协议升 v8，`LoginResponse`/`MatchStart`/`SyncResponse` 增加 `sync_time_to_step_ms`（int16）；服务器默认写 0，客户端记录并通过 HUD/`nsync_net_real` 的 `STS` 输出，暂未驱动 stepTime。补协议编解码断言；Debug/Release CTest 22/22；包 3.32 MiB，SHA256 `1F0CDCC2A70BA12B706237604AF2F838DDC0C82C131FA61E2FA1A61898816278`。  
- v1.35（2026-10-31）：Phase 1.5 原型完成，默认关闭。`RealServer` 新增 `late_input_replay`/窗口、按 tick 的 `input_events`/`input_history`，从最近快照恢复并重放到当前 tick，重建 snapshot history；服务器新增 `--late-input-replay`、`--late-input-replay-window` 和 `late_replays/late_replay_failures` 统计；`nsync_net_real` 在 `--client-lag` 时启用回放。0/50/150ms 实测 `corrections=0`、`desyncs=0`、`rejected_inputs=0`，但 50/150ms 仍有约 130–150 次 rollback，客户端稳态 lag 偏移仍偏大；新增 `nsync_net_late_input_replay` CTest。Debug/Release CTest 22/22；包 3.32 MiB，SHA256 `F98470B892E6061DB2D169E1F3CFFFECD7898DF918E4E0E7F980A2B4F79342A6`。  
- v1.34（2026-10-31）：Phase 1 spike。新增 `NetClientConfig::client_step_lag` 和 `nsync_net_real --client-lag`（默认关闭）；`begin_alignment` 可等待 `server_lag`，`catch_up_lag` 按 `highest_server_tick_ - server_lag` 维持偏移。实测 0ms `snapshot_mismatches=79`、`rejected_inputs=0`；50ms `rejected_inputs=66/324`，证明当前服务器 clamp 不能承载客户端 lag，决定暂不切默认、不升协议，先设计服务器输入回放；默认行为保持快照追帧。Debug/Release CTest 21/21；包 3.31 MiB，SHA256 `7B913AECA5DF8E5F56937A60D4757E0C291AB579C5EA6D4716DF49C94E1EBF59`。  
- v1.33（2026-10-31）：Phase 0 完成。`ClientStats`/`NetClientStats` 拆分 `snapshot_mismatches`、`snapshot_restores`、`event_rollbacks`、`total_rollback_ticks`；`corrections/desyncs` 仅统计权威 mismatch；HUD/日志/`nsync_net_real`/`nsync_net` 输出 `ERB/SM/SR`。`nsync_net_real` 增加 `--map PATH`，新增 `nsync_net_map_arena02`；`--client --map arena_02.json` 与同地图服务器连接验证通过。发现 0ms 仍有同 tick snapshot mismatch（一次 17 次、无 rollback），留待 Phase 1/2 查因。Debug/Release CTest 21/21；包 3.31 MiB，SHA256 `7CC9D3F18DE3B8577F79B2EFA41167950589852194D97EB758C8A68EC1BDE000`。  
- v1.32（2026-10-31）：新增 `docs/NEXT_STEPS_TODO.md`，把剩余原版对齐拆成 Phase 0–4（统计/fingerprint、serverLag+syncTimeToStep、动作事件、真实环境验证、发布收尾），补充决策门禁、验收矩阵、风险和回退；本次仅文档，未改功能代码。  
- v1.31（2026-10-31）：稳定 `nsync_net_real` 延迟矩阵。loopback 每轮不再调用两次 `server.update`，避免模拟 RTT 下服务器额外领先导致输入被拒；50ms `rejected_inputs=0`，150ms 降到 0–6；Debug/Release CTest 20/20。重新打包 3.31 MiB，SHA256 `239F766C032174A627D98C7A46C247706066A15721E0FC4E0DBE65B75920E858`。  
- v1.30（2026-10-31）：补齐 RealClient 0/50/150ms 模拟 RTT 矩阵。`KcpTransport` 增加客户端双向单向延迟（RTT/2）队列，`nsync_net_real --rtt-ms` 驱动；新增 `nsync_net_latency_50/150` CTest。0/50/150ms 2 客户端 300 tick 均 `corrections=0`、`desyncs=0`；模拟 RTT 下保留 rollback，150ms 偶发迟到输入拒绝。Debug/Release CTest 20/20。重新打包 3.31 MiB，SHA256 `8F78E9841CA6F32AAB046ACA314F57D6B7BA1A396A08AD84FCDDD0656C92D7DC`。  
- v1.29（2026-10-31）：按反编译 `BattleMaster.frameOnDesync` 修正输入排帧：普通输入 `CurrentFrame + clientEventLag`（默认 lead=3），仅 sync/desync 后下一批使用完整恢复公式（lead=13）。输入改为状态变化事件，服务器按目标 tick 持久化并接受迟到窗口；远端迟到事件回滚重放，普通快照校正只记 rollback、不增加原版语义 desync。客户端改为快照对齐 + 追帧，不再开局等待 `serverLag`；HUD 增加 `REC`。修复 `nsync_net_real` 按实际登录 slot 生成输入。Debug/Release CTest 18/18 通过；`nsync_net_loopback`、lobby、reconnect、rematch、5v5 全过。重新打包 3.31 MiB，SHA256 `1CED27B6DAA1FD61938B895F4FDE5AFEF4872F495278C5CF96D1F75B28B56050`。  
- v1.27（2026-10-30）：文档规划原版输入同步完整模型；新增 `docs/INPUT_SYNC_MODEL_PLAN.md`，明确 `serverLag`/`clientEventLag`/`syncPeriod`、原版输入目标帧公式、客户端 `serverLag` 对齐、未来快照队列、协议 v7、重连/Rematch、测试矩阵和 `legacy|original` 回退；TODO 插入 Phase 10，PHASE5 和 README 标注当前 `input_delay=3` 仅为简化实现；本次未改代码。  
- v1.26（2026-10-30）：修复本地双客户端体验问题；联机输入提前量从硬编码 16 tick 改为 `client.input_delay_ticks`/`--input-delay-ticks`，默认 3 tick（60Hz≈50ms）；弹匣打空后自动换弹，对齐反编译 `GunControllerImpl` 的 `startReload` 行为，并更新 golden replay tick 90–120 哈希；新增空弹匣自动换弹单测；两个客户端 960×540 `--no-vsync` 实测约 60–290 FPS、输入延迟日志 `input_delay=3`、corrections=0；Debug/Release CTest 17/17。默认 VSync 双窗口在部分驱动/桌面合成器下会压到 30/16 FPS，README 建议本地双开使用 `--no-vsync`。  
- v1.25（2026-10-30）：Phase 9 版本号/更新检查完成；版本来自 CMake，client/server 增加 `--version`，启动日志/崩溃报告带版本和构建配置；新增 `shared/update_check`（本地清单 + Windows WinHTTP HTTP/HTTPS）、`--check-update/--update-url`、`update.url` 配置和 0/1/2 退出码；新增版本比较/清单/BOM 单测；本地文件与本地 HTTP 检查验证通过；Debug/Release CTest 17/17。  
- v1.24（2026-10-30）：Phase 9 服务器部署脚本完成；新增 `scripts/deploy_server.ps1`，从发布 zip/Release 构建安装 server、DLL、assets 和生成的 `server.ini`，支持 Install/Start/Status/Stop/Uninstall、PID/日志和可选 UDP 防火墙规则；临时目录部署 + 40201 启动 + GUI client 连接开局 + 停止卸载验证通过；CI 增加部署 smoke。  
- v1.23（2026-10-30）：Phase 9 Windows 打包完成；新增 `scripts/package_windows.ps1`，生成 `dist/nsync-0.1.0-win64.zip`、SHA256 和 manifest，包含 client/server、vcpkg DLL、assets/config/shaders/README 和部署脚本；默认排除原版派生图集并使用 placeholder，`-IncludeOriginalHeroArt` 需显式开启；包 3.29 MiB，解压后 client `--help`/2 帧、server 5 tick、包内部署脚本安装/启动/停止/卸载验证通过；CI 增加打包/artifact。  
- v1.22（2026-10-30）：Phase 9 崩溃日志/遥测完成；新增 `shared/crash_handler`，Windows 未处理异常生成文本报告 + minidump，报告含异常码/地址、最后快照 tick/hash 和最近 256 条日志；客户端/服务器安装处理器，`nsync_server --crash-test` 已验证；新增崩溃报告单测；服务器/客户端对局结束输出 ticks/inputs/snapshots/snapshot_avg/ack_mismatches 或 corrections/rollbacks/desyncs 摘要；Debug/Release CTest 17/17。  
- v1.21（2026-10-30）：Phase 9 CPU/内存/带宽优化完成；服务器快照广播改为每 tick 生成/编码/哈希一次并复用给全部会话，ack 校验复用最近广播哈希，`send_packet` 去除 `Packet` 中间拷贝，`ByteWriter` 预分配；新增 snapshot bytes/generation/broadcast 统计；Release 10 玩家/10 人类 loopback 实测快照 1.96–2.28KB、生成 44–61µs avg、max 103–624µs，每客户端 18.5–22.1 KiB/s（20Hz≈38–45 KiB/s）<64 KiB/s，0 correction/desync；Debug/Release CTest 17/17。  
- v1.20（2026-10-30）：Phase 9 soak 完成；新增 `nsync_soak`（`--minutes/--matches/--players/--humans/--realtime`）和 `nsync_soak_smoke` CTest；Release 10 场并发 × 60 分钟模拟时间共 2,160,000 ticks、wall 11.0 秒、0 mismatch/desync/correction；Debug/Release CTest 17/17。wall-clock/真实 UDP soak 仍未跑。  
- v1.19（2026-10-30）：Phase 9 10 场并发基线完成；`tests/test_net.cpp` 新增 10 个 `LocalSession`、10 线程、每场 2 人类/10 玩家并发 600 ticks，检查服务器/客户端哈希一致与 desync；Debug/Release CTest 16/16。真实 UDP 多进程并发仍待 soak。  
- v1.18（2026-10-30）：Phase 9 第二项基础加固完成；新增每会话包/输入 `RateLimiter`（默认 600 packets/s、240 inputs/s），在 token 校验后统一限流，stats 增加 `rate_limited_packets`/`rate_limited_inputs`；保留现有 slot/button/tick 校验；新增窗口重置/超限/时间回绕单测；Debug/Release CTest 16/16。  
- v1.17（2026-10-30）：Phase 9 第一项协议模糊测试完成；`tests/test_net.cpp` 覆盖全部协议 payload 解码器的确定性 truncation、追加字节、逐字节/2/4 字节长度变异和随机输入，不崩溃且截断均被拒绝；Debug/Release CTest 16/16。  
- v1.16（2026-10-30）：完成 A6.1 原版角色渲染性能仪器化；新增 `WorldRenderer::run_original_hero_benchmark` 与 `--benchmark-heroes`，在 10 玩家 × 9 部件、全部受击闪白的 1280×720 最坏几何场景下记录 avg/p95/max；Release 连续 3 次 avg 19.7–24.3 µs、p95 26.3–30.9 µs，低于 0.5 ms/帧预算；Debug/Release CTest 16/16。  
- v1.15（2026-10-30）：原版角色美术方案 A 基础版完成；新增 `scripts/import_original_heroes.py`、`src/core/hero_rig.*`、`assets/data/hero_rigs.json`/`hero_poses.json` 和原版 512×342 图集；`WorldRenderer` 支持分层 quad/z-order/flip/Y-up→Y-down/程序化七状态/六武器/受击闪白/死亡淡出/队伍环/占位 fallback；新增 `client.hero_art` 与 `--hero-art`，更新 watcher；Debug/Release CTest 16/16，F5 和缺失原版资源回退已验证。  
- v1.14（2026-10-30）：确认原版角色美术接入采用方案 A；TODO 增加 A1–A7 实施切片、交付物、验收、不做项和待确认项，方案为原版基础皮肤分件 + 分层 puppet + 程序化 pose + 武器挂点 + 占位 fallback；当时仅文档就绪，等待用户明确开始。  
- v1.13（2026-10-30）：原版角色美术资源调研；确认 2026-10-30 AssetRipper 导出中的 `ScoutPref`/`MachineGunnerPref`/`AssaultPref` GLB 提供完整节点层级，但全部 1498 个 GLB 的 `animations` 均为空，导出目录无 `.anim`/`.controller`；用 prefab 层级 + Sprite 顶点 + PNG 临时拼装三英雄 rest pose 验证可做分层渲染；需求文档更新为待确认，未开始功能代码。  
- v1.12（2026-10-30）：Phase 8 第七切片完成；新增 EN/中文占位本地化、`client.language`/`--language`、设置页语言切换与持久化；新增启动过渡页、部署加载页、启动致命错误原生对话框和控制模式错误弹窗；中文通过系统 CJK 字体加载并仅在中文界面启用；Debug/Release CTest 16/16。  
- v1.11（2026-10-30）：Phase 8 第六切片完成；新增 GAME/CONTROLS 设置页，支持主音量/静音、分辨率/全屏/VSync/血效、移动/跳跃/换弹/武器/技能/计分板/暂停键位重绑，及冲突提示/Backspace 重置/Esc 取消；运行期设置写入 `config/client.settings.ini`，加载顺序为 `client.ini` → `client.settings.ini` → CLI；`AudioEngine` 支持运行期启停，`Config` 增加 `save_file`/`merge`；新增 `--settings`/`--fullscreen`/`--windowed`/`--blood`/`--no-blood`；Debug/Release CTest 16/16。  
- v1.10（2026-10-30）：Phase 8 第五切片完成；`audio_stub` 替换为 SDL3 `AudioEngine`，启动时合成 3 组武器射击变体及命中/护盾/爆炸/脚步/换弹/技能/死亡/复活/比赛结束/UI PCM；按 `SoundManager.PlayFxAtPoint` 的距离/pan/并发上限实现空间化，枪声按武器族区分，脚步和爆炸由客户端公开状态派生；新增 `--audio`/`--no-audio`/`--audio-volume` 和 `audio.enabled`/`audio.volume`；Debug/Release CTest 16/16。  
- v1.9（2026-10-30）：Phase 8 第四切片完成；加入程序化枪口闪光、队伍色弹丸轨迹、命中/护盾受击火花、按内容爆炸半径缩放的爆炸环/火球/碎片，以及仅影响世界渲染的相机抖动和开火后坐力；表现层按 `EffectOnFireController`、`FiringLineController`、`CollideEffectController`、`DetonateEffectController`、`ExplosionConfigurator`、`BattleCamera` 的功能分层实现；Debug/Release CTest 16/16。  
- v1.8（2026-10-30）：Phase 8 第三切片完成；加入数据驱动护盾/回盾、Kickstart 冲刺/Power Battery 回盾/Overcharge 增伤、`blocked_damage`、快照 v6/协议 v6、HUD 护盾与技能冷却、计分板 BLOCK 列、First Blood/Double/Triple/Monster Kill/Killing Spree 事件流；更新 golden replay 哈希；Debug/Release CTest 16/16。  
- v1.7（2026-10-30）：Phase 8 第二切片完成；加入确定性 `kills`/`deaths`/`damage_dealt` 战斗统计、弹丸 owner 伤害归属、快照 v5/协议 v5、Tab 计分板 K/D/DMG 列；更新 golden replay 哈希；Debug/Release CTest 16/16。  
- v1.6（2026-10-30）：Phase 8 第一切片完成；加入 `GameUi`、`game.rml`/`game.rcss`、离线主菜单、在线 WAR ROOM、结构化 HUD、Tab 计分板、暂停和结算；新增 `--menu`/`--no-menu` 与 `client.show_menu`；菜单/暂停/结算停止本地模拟，联机房间和结算保持网络 tick；在线 `--no-lobby` 世界结束显示只读结算；修正 RmlUi 文档根尺寸和显示切换；Debug/Release CTest 16/16。  
- v1.5（2026-10-30）：Phase 7 第四切片完成；加入 `AssetWatcher`、客户端 `--hot-reload`/F5、源码 assets 优先挂载、content/map/atlas/animation 热重载；直接模式 content/map 重载重建沙盒，联机模式拒绝 content/map 变更；图集热重载事务化，失败保留旧资源；核心测试覆盖文件变更/删除/重建。Debug/Release CTest 16/16。  
- v1.4（2026-10-30）：Phase 7 第三切片完成；加入通用 JSON 解析、`Atlas`/`AnimationSet` 数据格式、`nsync_atlas` STB 图集打包器、84 帧占位英雄图集、21 组动画和角色动画状态机；修复直接模式 `--mode control` 未写入世界，HUD 显示 `CTRL`/控制点百分比；新增 `nsync_atlas_check`，Debug/Release CTest 16/16。  
- v1.3（2026-10-30）：Phase 7 第二切片完成；加入地图 objective、控制点捕获/计分、快照 v4、协议 v4、服务器/客户端 `--mode control`、HUD 控制点状态和控制点 loopback 测试；地图二进制升级到 v2；Debug/Release CTest 15/15。  
- v1.2（2026-10-30）：Phase 7 内容管线第一切片完成；内容 JSON + schema/校验器、3 英雄、6 武器、2 地图、地图 JSON→bin 编译器、内容 fingerprint、协议 v3；新增 `nsync_datac`/`nsync_mapc` 和 3 个 CTest，Debug/Release CTest 14/14。  
- v1.1（2026-10-30）：Phase 6 核心闭环完成；协议升级到 v2，加入假登录用户名/令牌、LobbyState、Ready/Unready、英雄选择、MatchStart/MatchEnd、10 slot 机器人补位、倒计时、结算、掉线机器人接管和 Rematch；新增大厅 loopback、重连和 Rematch 测试，Debug/Release CTest 11/11。  
- v1.0（2026-10-30）：Phase 5 完成；加入 KCP over UDP 传输、协议编解码/版本握手、session token/slot、服务器对局循环、客户端预测/快照/ACK/同步、时钟同步、超时/重连、`nsync_server --net`、`nsync_net_real` 和客户端 `--connect`。两个独立客户端 + 服务器、loopback 和重连测试通过；Debug/Release CTest 7/7。  
- v0.9（2026-10-29）：Phase 4 完成；加入 `LocalServer`/`LocalClient`、input delay、快照广播/恢复、预测/回滚重放、延迟/抖动/丢包/重复模拟、NetGraph、`nsync_net` 和客户端 `--net` 模式；150ms RTT/30ms 抖动 10 分钟预测测试通过。Debug/Release CTest 5/5。  
- v0.8（2026-10-29）：Phase 3 完成；加入两英雄（Scout/Gunner）、三武器（步枪/霰弹/火箭）、弹丸地图碰撞/散射/爆炸、TDM 计时与胜负、确定性 2v2 机器人、客户端 4 人本地对局、HUD 与音效占位；快照格式升级到 v3，golden replay 更新。Debug/Release CTest 4/4，5 分钟机器人模拟通过。  
- v0.7（2026-10-29）：Phase 3 核心切片；加入三武器（步枪/霰弹/火箭）、弹丸地图碰撞/散射/爆炸、TDM 计时与胜负、确定性 2v2 机器人、客户端 4 人本地对局、HUD 与音效占位；快照格式升级到 v2，golden replay 更新。Debug/Release CTest 4/4，5 分钟机器人模拟通过。  
- v0.6（2026-10-29）：Phase 3 进行中；加入 `Map` JSON 加载、`assets/maps/arena_01.json`、地图实体/单向平台/出生点、`World::set_map`、客户端本地世界模拟、WASD/鼠标输入、相机跟随、碰撞/血量/计分调试绘制；地图解析与单向平台测试通过。  
- v0.5（2026-10-29）：Phase 2 完成；加入定点几何、PCG32、实体 generation/free-list、组件池、`InputCommand`/`World::step`、全量快照、xxHash64 快照哈希、replay 日志、golden replay 和 `nsync_replay` headless 工具。Debug/Release 哈希一致，结果见 `docs/PHASE2_RESULTS.md`。
- v0.4（2026-10-29）：Phase 1 完成；拆分 `nsync_core`/`nsync_client`/`nsync_server`/`nsync_tests`/`nsync_pak`，加入固定步长、日志、配置、目录/pak 资源挂载、RmlUi 资源文件接口、README 与 Windows CI。结果见 `docs/PHASE1_RESULTS.md`。
- v0.3（2026-10-29）：Phase 0 完成；SDL3+bgfx sprite、RmlUi 面板/按钮、定点数测试、KCP 双进程丢包/延迟测试全部通过。结果见 `docs/PHASE0_RESULTS.md`。
- v0.2（2026-10-29）：确认 SDL3 / KCP over UDP / 60Hz / 只做 PvP；补充 KCP 会话、`ikcp_nodelay`、MTU、latest-wins 快照和风险项。
- v0.1（2026-10-29）：初稿。基于 NSYNC 静态逆向，确定 C++/bgfx/SDL3/RmlUi 方向，拆分 10 个阶段。








