# Phase 8 结果：RmlUi 界面、战斗统计、英雄能力、战斗表现、音效、设置与本地化

日期：2026-10-30

## 目标

把 Phase 6 的单行 HUD 调试文本升级为可展示的 RmlUi 界面壳，覆盖主菜单、在线房间、对局 HUD、计分板、暂停和结算，同时保持确定性模拟、服务器权威状态和既有命令行自动化。

第一切片优先完成“能完整跑一局并看懂状态”的界面；第二切片补齐计分板的 Frags/Deaths/CausedDamage 统计和快照传输；第三切片补齐护盾/回盾、三名英雄主动技能、BlockedDamage 和 First Blood/连杀事件流；第四切片补齐枪口/弹道/命中/爆炸的程序化战斗表现和仅作用于世界渲染的相机抖动/后坐力；第五切片补齐 SDL3 程序化音效、武器族变体、脚步和 UI 点击；第六切片补齐设置页、音量/静音、分辨率/全屏/VSync/血效和键位重绑；第七切片补齐 EN/中文占位本地化、启动/部署加载页和启动/运行时错误提示。原版音频资源、纹理粒子、组队/邀请和完整账号系统仍不在本文件范围内。

## 已完成

### RmlUi 界面壳

`src/client/game_ui.h` / `game_ui.cpp`、`assets/ui/game.rml`、`assets/ui/game.rcss`：

- 屏幕状态：`Menu`、`Lobby`、`Match`、`Paused`、`Results`、`Settings`、`Loading`。
- 单一文档内切换界面，所有动态内容通过 RmlUi `SetInnerRML`、`SetClass` 和显示属性更新。
- 一个文档级点击路由读取 `data-action` / `data-value`，按钮和动态英雄卡共用同一套事件处理。
- 文档根节点尺寸跟随上下文窗口更新，支持窗口 resize。
- 所有动态玩家名先做 HTML 转义，避免用户名破坏 RML。

### 离线主菜单

- 默认启动进入主菜单；`DEPLOY` 才开始本地 2v2 沙盒。
- 英雄卡来自运行时内容库，显示 HP/护盾/速度/主动技能名；支持点击选择，也支持 `1/2/3` 选英雄、`Enter` 部署。
- TDM / CONTROL 模式切换；地图路径和内容 fingerprint 显示在菜单。
- `QUIT` 退出。
- `--no-menu` 跳过菜单；`--frames` / `--ticks` 自动化运行时默认跳过菜单；`--menu` 可显式强制菜单。
- 菜单期间不推进模拟，只保留 UI 和资源热重载轮询。

### 在线房间 / 大厅

- 在线连接后直接进入 `WAR ROOM`，两队分栏显示已加入成员、英雄、槽位和 Ready 状态。
- 未占用槽位显示 `OPEN SLOT`，并提示空位会在开局时补机器人。
- 显示 Ready 人数、人数、服务器地址、地图和协议版本。
- 服务端倒计时显示在房间顶部，不再遮挡 Ready 按钮。
- 英雄卡可选择；`READY / CANCEL READY` 按钮与 `R` 键互通；`1/2/3` 仍可选择英雄。
- `LEAVE` 退出客户端。

### 对局 HUD

- 顶部：模式标签、双方比分、剩余时间、比分上限。
- 控制点：目标状态、阵营、占点/得分进度条和百分比。
- 左下：英雄、HP 条、护盾条、低血量红色状态、主动技能名与冷却、弹药、当前武器、存活/阵亡状态。
- 阵亡时显示复活倒计时。
- 右上：事件流；当前包括 First Blood、Double/Triple/Monster Kill、Killing Spree 等连杀提示、技能激活、英雄阵亡、队伍得分和复活提示。
- 屏幕中央准星、受击红色闪屏、资源重载 toast。
- 右下：FPS、本地 tick 或联机 RTT/ACK/回滚/correction/时钟偏移。
- `Tab` 打开计分板：按队伍显示玩家、英雄、K、D、DMG、BLOCK、HP、LIVE/DOWN；本地玩家高亮。

### 战斗统计与计分板

- `core::Player` 新增确定性字段：`kills`、`deaths`、`damage_dealt`、`blocked_damage`、`shield`。
- 伤害通过 `Projectile::owner` 归属到发射者；护盾先吸收伤害，`blocked_damage` 记给受击者；`damage_dealt` 只统计实际扣 HP 的部分，溢出伤害不计。
- 击杀只在敌方目标死亡时记给发射者；自伤/友军爆炸伤害仍会造成死亡和团队比分变化，但不增加 `kills` 或敌方伤害统计。
- 统计跨复活保留，随 `WorldSnapshot` 序列化；快照升级到 **v6**，KCP 协议升级到 **v6**。
- 计分板列参考原版 `CharacterColumn` 的 Frags/Deaths/CausedDamage/BlockedDamage；Assists 和单人 Score 仍留后续。
- 统计变化会影响快照哈希，因此 `golden_replay_expected.h` 已按 v6 重新生成。

### 护盾、技能与连杀

- 英雄内容新增数据驱动字段：`shield`、`ability`、`ability_name`、`ability_cooldown`、`ability_duration`、`ability_power`；JSON schema 和内容 fingerprint 同步更新。
- 护盾先于 HP 吸收伤害；受击后暂停 105 tick，再每 10 tick 回复 1 点，直到内容配置的 `shield` 上限。复活重置护盾与技能冷却。
- 三名英雄各有一个 `E` 技能：
  - Scout：`KICKSTART`，沿朝向获得短促水平冲刺，冷却 420 tick。
  - Gunner：`POWER BATTERY`，立即恢复 40 点护盾（不超过上限），冷却 600 tick。
  - Assault：`OVERCHARGE`，180 tick 内将武器伤害提高到 1.5 倍，冷却 600 tick。
- HUD 显示护盾条、护盾数值、技能名、READY/冷却秒数/ACTIVE/DOWN；世界内玩家头顶显示护盾条。
- 客户端根据公开 frags 和存活状态派生 First Blood、Double/Triple/Monster Kill、Killing Spree/Rampage/Insanity/Unstoppable/Stop Him、Streak Ended；不伪造服务器未提供的击杀归属。
- 技能输入复用现有 `kButtonAbility`（协议 bit 7 / `E`），服务器输入范围仍限制在 `0x00FF`，不新增网络包类型。

### 战斗表现（第四切片）

- 表现层位于 `src/client/world_renderer.h` / `world_renderer.cpp`，只读取公开的玩家/弹丸状态，不修改确定性模拟、快照或网络协议。
- **枪口闪光**：通过弹药下降识别开火，在枪口位置生成队伍色短时星形闪光；本地开火同时触发轻微相机后坐力。
- **弹道拖尾**：按项目 ID 缓存最近 10 个弹丸位置，绘制队伍色/火箭色线段拖尾；弹丸本体保留原有方块标记。
- **命中反馈**：生命下降生成红色受击火花，护盾下降生成蓝色护盾火花；本地受击触发短时相机抖动。该效果只表达“我方被击中”，不伪造击杀者。
- **爆炸表现**：弹丸消失时，按内容中的 `explosion_radius` 生成火球、扩张冲击环、外围碎片火花；火箭和手雷共用同一表现生成器。相机距离爆炸较近时增加抖动。
- **相机反馈**：世界渲染使用单独的 presentation camera，叠加短时正弦抖动和开火反方向后坐力；偏移指数衰减，暂停/菜单/结算不会影响模拟，HUD 和准星不跟随世界抖动。
- **资源重置**：直接沙盒重开、在线 match id 变化、图集热重载和 renderer shutdown 会清理弹丸轨迹、特效与相机偏移，避免跨局残留。
- 特效使用白色纹理和程序化线段/三角扇绘制，并采用 alpha-additive blending；不依赖原版粒子 prefab 或纹理资源。
- 反编译对应关系：`EffectOnFireController` 的枪口 prefab、`FiringLineController` 的弹丸路径、`CollideEffectController` 的碰撞火花、`DetonateEffectController` + `ExplosionConfigurator` 的爆炸半径缩放、`BattleCamera`/`CameraControl.CameraBounce` 的相机反馈；本项目用简化几何表现，不复制资源。

### 音效（第五切片）

- `src/client/audio.h` / `audio.cpp` 替换原 `audio_stub`，使用 SDL3 `SDL_OpenAudioDeviceStream` 打开默认播放设备；音频线程回调中混合 PCM，不阻塞渲染/模拟。
- 启动时程序化合成 48kHz PCM 音效库：Rifle/Shotgun/Rocket/Grenade/SMG/Burst 射击各 3 个变体，命中/护盾/爆炸/脚步/换弹/换武器/技能/死亡/复活/比赛结束/UI 点击/悬停；每类设置独立音量和并发上限。
- 采用固定 32 voice 软件混音、256 项主线程待播队列、每 cue 并发上限（射击 12、脚步/弹丸命中 8、爆炸 6、UI 4）；队列满时丢弃新事件，不阻塞主线程。
- 空间化参考 `SoundManager.PlayFxAtPoint`：声源距离超过 17 世界单位静音，音量按 `1 - distance / 17` 线性衰减，左右声道按声源相对 listener 的 x 方向 pan；UI/比赛结束为居中 2D。
- 全部 cue 由客户端公开状态派生，不新增模拟/快照/协议字段：
  - 弹药下降 → 当前武器族射击音。
  - 弹丸从世界移除 → 非爆炸命中或爆炸音。
  - HP/护盾下降 → 本地受击/护盾命中音。
  - 存活且 grounded 且水平速度超过阈值 → 本地/远端脚步，0.31 秒节拍。
  - 换弹计时从 0 开始、技能激活、死亡/复活、胜利/失败、武器切换、RmlUi action → 对应 cue。
- `--audio` / `--no-audio` / `--audio-volume 0..100` 和 `audio.enabled` / `audio.volume` 配置；音频初始化失败只记录 warning，客户端继续静音运行。
- 反编译对应关系：`SoundManager.PlayFxAtPoint` 的距离/pan/并发限制、`Sound` 的命名音效与随机数组、`SoundController` 的 `DisplayingSoundDef` 随机选择、`UIPlaySound` 的点击触发；本项目用程序化 PCM 替代原版 `sounds/` 资源。

### 设置（第六切片）

- 新增 `src/client/client_settings.h` / `client_settings.cpp`：`ClientSettings`、`KeyBindings`、`GameAction`、SDL scancode 名称序列化和默认键位。
- `UiScreen::Settings` 覆盖主菜单/暂停；`GAME` 页包含 SOUND EFFECTS、MASTER VOLUME、RESOLUTION、FULLSCREEN、VSYNC、BLOOD EFFECTS；`CONTROLS` 页列出 MOVE LEFT/RIGHT、JUMP、RELOAD、SWITCH WEAPON、ABILITY、SCOREBOARD、PAUSE。
- 音效可在 OFF/ON 间切换；音量段点击即预览，Apply 后生效；取消时恢复已应用音量。`AudioEngine::set_enabled` 支持运行期懒启动/关闭，不要求重启客户端。
- `AudioEngine` 的 `--no-audio` 启动路径仍不打开设备；设置页开启音效时才尝试打开默认设备，失败显示 `AUDIO UNAVAILABLE` 并保持静音。
- 分辨率列表由 SDL `SDL_GetFullscreenDisplayModes` 加当前窗口尺寸生成；全屏使用 `SDL_SetWindowFullscreenMode` + `SDL_SetWindowFullscreen`，窗口模式使用 `SDL_SetWindowSize`；VSync 通过 `bgfx::reset` 切换；血效开关同时抑制红色命中/死亡特效和受击闪屏，护盾火花保留。
- 键位交互：点击行进入 capture，下一键写入；Backspace 重置当前项，Esc 取消；重复键显示 `KEY ALREADY BOUND TO ...`，F5 保留给资源重载；Esc 仍是通用返回键。鼠标左键射击固定为 LMB，本切片不重绑鼠标/手柄。
- 设置是单套键位（原版有 3 套 KeySet + gamepad）；不伪造未实现的 music/announcer、画质档位或手柄页。
- Apply 先应用音频/显示/键位/血效，再写 `config/client.settings.ini`；保存失败保留设置页并显示错误，不假装成功。`Config` 新增 `save_file` / `merge`，启动顺序为 `client.ini` → `client.settings.ini` → CLI。
- 设置页打开时离线直接模拟停止；联机大厅/对局仍由 `RealClient::tick()` 接收网络包，关闭后回到打开前的屏幕。HUD/主菜单控制提示随成功 Apply 的键位更新。
- 反编译对应关系：`SettingsWindow` 的 tab/Apply/Cancel、`GamePage` 的音效/音乐/分辨率/全屏/VSync/血效、`ControlPage` 的 KeySet/ActionKey capture、`UserSettingsData` 的默认音量和键位、`PlayerSettingsController.ApplySettings` 的即时应用；本项目简化为单套键位和本地 `client.settings.ini`，不复制原版 3 KeySet/远端设置同步。

### 本地化、加载与错误提示（第七切片）

- 新增 `src/client/localization.h` / `localization.cpp`：`UiLanguage::English/Chinese`、`parse_language`、`language_code`、`language_label` 和内置 EN/中文文案表；`Localization::text` 在缺失 key 时返回 `[key]`，保证占位可见而不是静默空字符串。
- `ClientSettings` 增加 `language`，配置键 `client.language`，CLI `--language en|zh`；`client.settings.ini` 持久化语言，加载顺序仍是 `client.ini` → `client.settings.ini` → CLI。
- 设置页右上角增加语言按钮，点击即时预览 EN/中文；Cancel 恢复已应用语言，Apply 写入设置文件；重置默认值恢复英文。绑定列表和主要动态状态文案也通过本地化表。
- RML 静态文案使用 `data-loc` 属性，`GameUi::apply_localization()` 递归遍历文档并替换文本，避免给每个标签单独维护 ID；动态文本在 `GameUi` / `bgfx_app` 中通过 `Localization::text` 生成。
- 中文只在 `body.lang-zh` 下使用系统 CJK 字体（优先 `msyh.ttc`，其后 `msyhbd.ttc`/`simhei.ttf`/`simsun.ttc`/`msjh.ttc`）；英文继续使用原有系统字体。未打包完整语言包，服务器动态文本、英雄/技能名、事件流和部分状态仍为英文。
- 新增 `screen-loading`：启动时显示 `CHECKING RESOURCES → LOADING CONTENT → ARENA READY` 过渡；本地 `DEPLOY` 显示 `PREPARING ARENA → SPAWNING AGENTS → STARTING MATCH`，加载完成后再重置沙盒并进入对局。加载页不推进本地模拟。
- `UiScreen::Loading` 与 `UiLoadingModel` 支持进度、步骤、详情、失败状态和错误字段；Esc/Enter 可跳过启动/部署过渡，错误态点击 OK 返回来源屏幕。
- 启动阶段致命错误（资源/内容/地图/英雄/网络/SDL/bgfx/shader/RmlUi 初始化失败）调用 Windows 原生错误对话框，避免窗口尚未创建时只有黑屏或日志。控制模式缺少地图 objective 时使用 RmlUi 错误面板，而不是只写日志/toast。
- 反编译对应关系：`ApplicationLoadingWindow` 的 Step/Progress/IsFailed/Error/Recommendation/CanQuit、`LoadingStepType` 的检查硬件/初始化/连接/预载、`MainApplication` 的 `FailStep` 和连接重试、`MessageBox` 的标题/正文/OK、`WorldLoadingScreen`/`MissionLoadingScreen` 的加载界面；本项目简化为客户端启动过渡和本地部署加载，不复制原版资源预载/异步任务系统。

### 暂停与结算

- 离线直接对局中 `Esc` 打开暂停面板；暂停时停止本地模拟，可 Resume、返回主菜单或退出。`--net` 本地权威测试台不进入暂停，直接退出。
- 联机模式中 `Esc` 直接退出，避免在服务器继续推进时伪造“暂停”。
- 结算面板显示 VICTORY / DEFEAT / DRAW、比分、双方名单。
- 离线结算：Rematch 重置本地沙盒，Main Menu 返回菜单。
- 在线大厅结算：Rematch 发送 `LobbyAction::Rematch`。
- 在线 `--no-lobby` 模式没有服务器 `MatchEnd` 包时，客户端根据 `world.match_over()` 显示只读结算（仅退出），避免对局卡死。
- `--net` 本地权威测试台结算只提供退出。

### 输入与模拟隔离

- 菜单、房间、暂停、结算时不读取移动/射击输入，不推进本地直接模拟。
- 在线房间和结算仍调用 `RealClient::tick()` 接收网络包，保持大厅/Rematch 状态更新。
- UI 点击不会误触发射击；对局中 HUD 非交互元素不拦截鼠标。
- 计分板与暂停切换时自动隐藏/恢复，避免残留在结算层上。

### 反编译参考

- `Assets.Script.Main.ViewModels.MainUI/MainScreen.cs`、`MainMenuComponent.cs`：主界面/主菜单分层和可见状态机；本项目简化为离线主菜单 + 在线房间两个入口。
- `Assets.Script.Main.ViewModels.GameUI/GameScreen.cs`：对局 HUD 的 Player/Toolbar/Clock/Task/Event/ScoreWindow 分区；对应本项目的 vitals、score strip、objective、feed、scoreboard。
- `Assets.Script.Main.ViewModels.GameUI.Components/ScoreWindowComponent.cs`、`GameUI.Components.Common/CharacterColumn.cs`：计分板可切换显示和 Username/Class/Frags/Deaths/CausedDamage/BlockedDamage/Score 列；当前实现 Username/Hero/Kills/Deaths/CausedDamage/BlockedDamage/HP/Status，assists 和单人分数留后续。
- `com.pragmatix.df.armor/Armor.cs`、`com.pragmatix.df.hero/HeroDef.cs`、`com.pragmatix.df.controllers/HeroControllerImpl.cs`：护甲盾先吸收、受伤后延迟维修和 `getToughness = hp + shield`；当前实现相同的“盾先扣、脱战回盾”语义，未实现 screening/penetration/护甲层。
- `com.pragmatix.df.hero/HeroDefAbility.cs`、`extracted/kickstart.bytes`、`power_battery.bytes`、`magnetic_nozzle.bytes`：英雄能力描述、冲刺力、护盾道具和伤害增益道具；当前改为三名英雄的简化主动技能。
- `PvpMessages.cs`、`com.pragmatix.df.game.scores/PvpEvent.cs`、`com.pragmatix.df.game/Game.cs`：First Blood、Frag/Death/Suicide/FriendlyFire、4/6/8/10/15 连杀和 4 秒内多杀；当前客户端根据 frags/存活状态派生提示，不复制服务器事件结构。
- `EffectOnFireController.cs`、`FiringLineController.cs`、`CollideEffectController.cs`、`DetonateEffectController.cs`、`ExplosionConfigurator.cs`：开火闪光、弹丸路径、非英雄碰撞火花、爆炸中心和半径缩放；当前实现对应职责的程序化枪口/拖尾/命中/爆炸，不加载原版 prefab。
- `BattleCamera.cs`、`Assets.Script.Core.Scene.Control/CameraControl.cs`：相机跟随、鼠标反应、距离限制和 `CameraBounce`；当前只把类似反馈用于表现层抖动/后坐力，不改变相机逻辑或瞄准。
- `SoundManager.cs`、`Sound.cs`：命名音效表、随机变体、`PlayFxAtPoint` 的 17/21 单位距离衰减、`(1 - volume) * sign(dx)` pan、10 个同音并发上限和 0.01 最小音量；当前用固定 17 单位、软件混音和每 cue 上限实现。
- `Assets.Script.Core.Scene.General.DisplayingEvent.DisplayingController/SoundController.cs`、`LoopedWeaponSoundController.cs`：`DisplayingSoundDef` 随机选择、`ON_WEAPON_SHOOT`/reload/stop-fire 事件；当前按弹药下降和换弹计时派生，未实现 warm-up/loop。
- `UIPlaySound.cs`：OnClick/OnPress/OnHover 触发音效；当前 RmlUi `data-action` 点击统一播放 UI click，未实现 hover 音。
- `extracted/rifle_1.bytes`、`pistol_fire.bytes`：武器 `displayingEvents` 指向 `weapon_rifle_shot01..03` / `pistol_shot01..03`；当前按武器 id 选择程序化射击变体，不加载原版 clip。
- `Assets.Script.Main.ViewModels.MainUI.Windows.Settings/SettingsWindow.cs`、`GamePage.cs`、`ControlPage.cs`、`SettingsTab.cs`、`Assets.Script.Main.Models.Data.Settings/UserSettingsData.cs`、`Assets.Script.Main.Controllers/PlayerSettingsController.cs`、`ScreenResolution.cs`：设置 tab、音效/音乐/分辨率/全屏/VSync/血效、KeySet/按键 capture、默认值和 ApplySettings；当前实现 GAME/CONTROLS、单套键位和本地设置文件。
- `Assets.Script.Main.ViewModels.GameUI.Components/AbortGameWindowComponent.cs`：暂停/退出面板；对应 Resume/Main Menu/Quit。
- `Assets.Script.Main.ViewModels.GameUI.Components/DeathScreenComponent.cs`：阵亡界面；当前简化为复活倒计时。
- `CombatMessageGUIManager.cs`、`Assets.Script.Core.Scene.General/BattleLogController.cs`：战斗消息/事件流和调试日志边界；当前客户端根据存活/比分/frags 变化生成轻量事件流，未伪造服务器击杀归属。
- `Assets.Script.Main.Models.Data.GameModel/GameResult.cs`、`GameTeamData.cs`：胜负和队伍数据；对应结算面板。

## 验证

- Debug CTest：**16/16**。
- Release CTest：**16/16**。
- 战斗统计测试：直射/火箭击杀后发射者 `kills == 1`、`damage_dealt > 0`，目标 `deaths == 1`；护盾吸收后目标 `blocked_damage > 0`、`damage_dealt` 只计 HP 伤害；统计和护盾跨复活状态正确；snapshot v6 编解码、护盾/BlockedDamage 越界拒绝通过。
- 护盾/技能测试：脱战回盾、Scout 冲刺冷却、Gunner 回盾不超上限、Assault 过载后弹丸伤害提升均有断言。
- 计分板手动截图：K/D/DMG/BLOCK 随 2v2 机器人战斗更新（如 BLUE 2 - RED 5，DMG 240/300/280，BLOCK 155/146/100/79），列宽和队伍分区正常。
- Debug UI 截图冒烟：
  - 主菜单：标题、三英雄卡（HP/护盾/速度/技能）、TDM/CONTROL、DEPLOY/QUIT、地图/fingerprint。
  - DEPLOY 后 HUD：比分/时间、护盾条、技能冷却、事件流、HP/弹药、阵亡倒计时、网络/FPS。
  - `E` 技能：Scout KICKSTART 触发后事件流显示技能名，HUD 显示冷却。
  - 连杀事件流：2v2 机器人对局中观察到 `DOUBLE KILL // BOT 2`、`team red scores`、`BOT 3 eliminated` 等提示。
  - 控制点 HUD：`CTRL`、`RED CAPTURING`、进度条和百分比。
  - `Tab` 计分板：双方名单、BLOCK、HP、LIVE。
  - `Esc` 暂停：Resume/Main Menu/Quit。
- 战斗表现截图冒烟：
  - Debug/Release 对局中观察到蓝/红队枪口闪光、弹丸轨迹拖尾、命中火花和护盾受击火花；弹丸与拖尾颜色跟队伍一致。
  - Gunner 切到 Rocket 后向脚下平台开火，观察到按 `explosion_radius` 扩张的冲击环、火球和外围碎片，近距离爆炸同时触发相机抖动。
  - 本地连续开火和受击时世界渲染出现短时后坐力/抖动；HUD、准星和 UI 不跟着抖动。
  - 截图：`phase8_combat_fx_final.png`、`phase8_rocket_fx_final2.png`。
- 音效链路冒烟：
  - Debug/Release 均输出 `[audio] ready: 48000 Hz, 2 channels, 3 rifle variants`，随后正常进入对局并在退出时输出 `[audio] stopped: queued=..., dropped=0 rendered_frames=...`；300 帧测试渲染约 28 万帧，确认 SDL3 回调实际工作。
  - `--no-audio` 输出 `[audio] disabled by configuration`，无设备错误、无音频回调。
  - 用 `SDL_AUDIODRIVER=invalid` 强制音频初始化失败时，客户端仅输出 warning 并继续静音运行，退出码 0。
  - 用 `SDL_AUDIODRIVER=disk` 捕获实际混音输出：设备协商为 44100 Hz / 2ch，生成的 `sdlaudio.raw` 约 549 KB，nonzero samples 152,767，峰值 32767（仅 1 个样本接近满幅），RMS 约 2988，确认不是静音流且重采样路径工作。
  - 主菜单 `--simulate-click` 路径能正常执行 RmlUi action 和音频初始化；`--no-menu --frames` 机器人交火期间队列持续产生事件，未出现队列溢出。
  - 未进行主观听感/真实耳机设备验收；本轮只验证 SDL3 设备初始化、混音回调、队列和客户端退出路径。
- 设置页冒烟：
  - `--settings` 直接显示 GAME/CONTROLS 设置页；Debug 截图 `phase8_settings.png`、`phase8_settings_controls.png`。
  - 临时配置下验证 `client.settings.ini` 覆盖基础配置：日志 `settings loaded: 1600x900 fullscreen=false audio=false volume=30 jump=F`，说明数值和 `input.jump=F` 均被加载；CLI 仍最后覆盖。
  - 点击 CONTROLS，选择 JUMP，按 F，再 Apply；日志出现 `ui action: settings-bind / jump` 和 `settings-save`，文件写入 `input.jump=F`。
  - 点击音量段后 Apply，文件写入 `audio.volume=40`（该次点击落在相邻 40% 段）；点击 SOUND EFFECTS 关闭后 Apply，日志出现 `[audio] stopped`，文件写入 `audio.enabled=false`；从 `audio.enabled=false` 启动后打开设置并开启音效，日志出现 `[audio] ready`，文件写入 `true`。
  - `--fullscreen` 启动验证窗口进入 1920x1080、client 区域 1920x1080；设置页切换 FULLSCREEN 后 Apply 写入 `client.fullscreen=true`。
  - Apply/Cancel/Esc 路径会更新或恢复已应用音量并返回打开设置前的屏幕；键位冲突和 F5 保留提示由 capture 分支处理。
- 本地化/加载/错误冒烟：
  - `--settings --language zh` 显示中文设置页，中文正文使用 `CjkFont`；`--settings --language en` 保持英文布局。截图：`phase8_localization_zh_settings3.png`、`phase8_localization_en_settings2.png`。
  - 设置页点击语言按钮后日志出现 `ui action: settings-language-toggle`，再点击 Apply 日志出现 `ui action: settings-save` 和 `settings saved to ...client.settings.ini`，文件包含 `client.language=zh`；重新从同一工作目录启动时日志 `settings loaded: ... language=zh`，确认语言持久化。
  - 正常菜单启动显示启动过渡页；`--menu --language zh` 截图 `phase8_loading_boot_zh2.png` 显示中文启动步骤和进度条，过渡结束后进入 `phase8_menu_zh_1280.png`。
  - 菜单按 Enter 部署后显示 `phase8_loading_deploy_zh.png`（生成特工/进度），完成后进入对局。
  - `--map maps/missing.json` 启动时出现标题为 `NSYNC - Map Error` 的原生错误对话框，而不是静默退出；控制模式缺少 objective 的错误面板路径已接入 `loading-dismiss`。
- 在线 UI 冒烟：
  - `--connect --no-auto-ready` 进入 WAR ROOM；显示蓝/红队、OPEN SLOT、Ready 人数。
  - `R` 后 Ready 变为 `CANCEL READY`，顶部显示 `STARTING IN N`。
  - 在线结算：DEFEAT 0:1、双方名单、REMATCH/QUIT。
  - 在线 `--no-lobby` 结算：DRAW 0:0、只读退出。
- Release 客户端可加载新 RML/RCSS 并显示主菜单。
- 原有网络/大厅/控制点/golden replay 测试全部保持通过。

## 已知边界

- 联机入口仍由 `--connect` 命令行参数决定；在线主菜单/手动输入服务器地址留后续。
- 设置页已实现基础音频/视频/键位；仍是单套键位，不含原版 3 套 KeySet、gamepad、音乐/announcer 开关、画质档位、鼠标键重绑或远端设置同步。键位列表不含射击（固定 LMB）；分辨率列表来自 fullscreen modes 加当前窗口尺寸。
- 音频仍只通过 `audio.enabled`/`audio.volume` 和设置页控制；背景音乐/announcer 尚无实际资源，不能伪造可播放开关。
- 本地化是占位实现：EN/中文 UI 静态文案和主要动态状态已接入，但服务器动态文本、英雄/技能名、事件流、控制提示和部分设置状态仍为英文；中文依赖系统 CJK 字体，未打包完整语言包。启动资源加载早期仅能在原生错误对话框提示，RmlUi 启动页在窗口/资源初始化后显示；原版的异步预载/任务列表、重试按钮和完整加载资源管理未实现。
- HUD 已有护盾、技能冷却和连杀提示，战斗表现已有程序化枪口/弹道/命中/爆炸和相机抖动，音频已有基础 cue；但尚无伤害数字、技能图标、纹理粒子、技能动画和原版技能语音。
- 计分板已有 K/D/CausedDamage/BlockedDamage，但仍无 assists 和单人 Score；后者需要按 GameDef/PvpEvent 重做个人积分。
- 护盾/技能仍是简化版：没有原版 Armor screening/penetration/多层保护、技能充能层数、技能动画和专属音效；连杀提示由客户端根据公开 frags 推导，不携带服务器 killer/victim 事件。
- 战斗特效和音效由客户端根据弹药/生命/护盾/弹丸/移动状态派生，没有服务器特效/音效事件；网络修正可能产生偶发受击/枪口/脚步/爆炸表现，且没有伤害数字或原版粒子 prefab。
- 音效为程序化短 PCM，不加载原版 `sounds/`；没有背景音乐、环境循环、announcer/语音、多材质脚步、looped weapon warm-up/stop 或音频优先级系统。音频线程使用软件混音，尚未做真实多声道设备、蓝牙延迟和长时间播放 soak。
- 相机抖动/后坐力只改变世界渲染相机，不改变玩家 aim、输入、命中判定或模拟哈希。
- 未实现小队/邀请/聊天；服务端仍是单进程单对局。
- RmlUi 目前只使用纯色面板和文字，未实现 `LoadTexture` 的英雄头像/图集裁切。
- 当前 bgfx UI 路径下 `rgba()` 背景和 `opacity` 的表现不稳定；RML/RCSS 暂统一使用不透明色和显式显示切换，后续修正 UI alpha/blend 后再启用半透明效果。
- UI RML/RCSS 尚未纳入 0.5 秒热重载 watcher；修改界面仍需重启客户端。
