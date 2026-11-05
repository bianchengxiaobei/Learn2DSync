# 插队需求：使用反编译角色资源替换战斗角色

状态：**方案 A 基础版已实现，A6.1 性能实测通过；逐帧原版动画未实现**  
日期：2026-10-30  
范围：客户端战斗表现；不改核心模拟、命中判定、快照、协议  
关联文档：`docs/TODO.md`、`docs/PHASE7_RESULTS.md`、`docs/ORIGINAL_HERO_ART_RESULTS.md`、`README.md`

## 1. 背景

当前战斗角色是 Phase 7 为验证图集/动画管线制作的占位美术：

- `assets/sprites/source/*.png`：3 英雄 × 7 状态 × 4 帧 = 84 帧，单帧约 48×64。
- `assets/sprites/heroes.png` + `assets/sprites/heroes.atlas.json`：`nsync_atlas` 打包后的占位图集。
- `assets/data/animations.json`：21 个 clip，状态为 `idle/run/jump/fall/shoot/hurt/death`。
- `WorldRenderer` 每个玩家只画一个 quad，按状态选帧；`sprite_height = 1.8` 世界单位。
- 角色外观是简化色块/线稿，和原版角色差距明显。

本需求目标：**用反编译版本中的原版角色部件替换占位图**，让战斗角色具备原版轮廓、部件层次和基本动作。第一版不要求逐帧复刻原版 Unity 动画，但必须明显优于当前占位图，并保留现有玩法/网络/确定性约束。

需要特别修正一个旧假设：README 曾写“后续接入正式美术只需替换源帧并重新打包，不动 C++”。实际提取到的原版资源不是 84 张成套帧，而是**按皮肤拆分的 Body/Head/Shoulder/Hand/Foot 部件**，由 prefab 层级和 Animator/Puppet2D 驱动；因此这次要么做分层部件渲染（推荐），要么先把 prefab/动画烘焙成新帧再走现有图集管线。不能只把 PNG 改名替换。

## 1.1 实施状态（2026-10-30）

方案 A 基础版已落地：

- 新增 `scripts/import_original_heroes.py`，从 3 个 prefab GLB + Sprite JSON + 已裁 PNG 生成 `assets/generated/original_heroes/heroes.png`、`heroes.atlas.json` 和 `assets/data/hero_rigs.json`。
- 新增 `src/core/hero_rig.*`：rig/pose 数据加载与校验；`src/client/world_renderer.*` 增加多部件旋转 quad、z-order、整体翻转、原版武器挂点、七状态程序化 pose、受击 additive 闪白、死亡淡出、队伍脚下环和占位 fallback。
- 新增 `assets/data/hero_poses.json`（手写关键参数）。
- 新增 `client.hero_art=auto|placeholder|original` / `--hero-art`；默认 auto；缺失原版图集/rig/pose 自动回退占位。
- 三英雄基础皮肤、六种武器 sprite 已接入；快捷测试 `--hero-art original`、`--hero-art placeholder` 和 F5 热重载已验证。
- A6.1 已实测：`--benchmark-heroes` 在 10 玩家 × 9 部件、全部受击闪白的 1280×720 最坏几何场景下，Release avg 19.7–24.3 µs、p95 26.3–30.9 µs（预算 0.5 ms/帧）。
- 详细文件、验证命令、结果与限制见 `docs/ORIGINAL_HERO_ART_RESULTS.md`。

仍未做：原版逐帧动画、7 套皮肤、头像/技能图标、原版粒子、骨骼蒙皮；原版派生图集的分发授权仍待确认。

## 2. 资源调研结论

### 2.1 当前英雄与原版 prefab 映射

| 当前内容 id | 原版 hero family | 原版 prefab | 可用部件前缀 |
|---|---|---|---|
| `scout` | `scout` | `ScoutPref` | `Scout*` |
| `gunner` | `machine_gunner` | `MachineGunnerPref` | `MachineGunner*` |
| `assault` | `assault` | `AssaultPref` | `Assault*` |

原版还有 Grenadier、Cyborg、Predator、Hermit、Pyromancer、Warrioress 等英雄；本次只接入当前 3 个英雄，不扩英雄数量。

### 2.2 已提取的原始 PNG

目录：`reference/nsync/extracted/sprites/`

每个英雄有 **7 套皮肤**，每套包含以下部件（命名随 hero 略有差异）：

- `Body`
- `Head` 或 `Hat`
- `ShoulderBack` / `ShoulderFront`
- `HandBack` / `HandFront`
- `Foot`（部分英雄拆为 `FootBack` / `FootFront`）

示例：

- `ScoutBody1.png` 63×114、`ScoutHat1.png` 70×71、`ScoutShoulderFront1.png` 41×42、`ScoutHandFront1.png` 33×29、`ScoutFoot1.png` 38×24。
- `MachineGunnerBody1.png`、`MachineGunnerHat1.png`、`MachineGunnerHandFront1.png` 等。
- `AssaultBody1.png` 105×157、`AssaultHead1.png`、`AssaultShoulderFront1.png`、`AssaultFoot1.png` 等。
- 武器/道具另有 `SoldierRifle1..7`、`minigun1..7`、`shotgun1..7`、`rocketLauncher1..9`、`LaserGun1..7`、`grenade1..20`、`gadgetRifle/Shotgun/Rocket` 等精灵。

后缀 `1..7` 是**皮肤编号**，不是动画帧号；不能按“7 帧动画”直接播放。

### 2.3 prefab 与动画数据

用 UnityPy 只读检查 `NSYNC_Data/resources.assets`（该检查不进入构建）：

- 场景资源包含约 11913 个 `GameObject`、11907 个 `Transform`、2070 个 `Sprite`、660 个 `AnimationClip`。
- 可读取 `ScoutPref`、`MachineGunnerPref`、`AssaultPref` 的完整 Transform 层级、局部位置/旋转/缩放和 `SpriteRenderer` 引用的 Sprite。
- `ScoutPref` 层级包含：
  - `Hero/playerPivot/bodyAnim/bodySprite`
  - `headAnim/headPivot/headSprite`
  - `shoulderBackAnim`、`shoulderFrontAnim`
  - `handBackAnim`、`handFrontAnim`
  - `footBackAnim`、`footFrontAnim`
  - `Hero/weapon/pivot/allWeaponAnim` 下的手部/武器挂点
- 原始动画 clip 仍可读到名字，例如：
  - Scout：`ScoutIdle`、`ScoutRocketFire`、`ScoutRocketRun`、`ScoutRocketMoveBodyOnly`、`ScoutStartJump`、`ScoutFly`、`ScoutEndJump`、`ScoutStun` 等。
  - MachineGunner：`MachineGunnerIdle`、`MachineGunnerRun`、`MachineGunnerFire`、`MachineGunnerFly`、`MachineGunnerStartJump`、`MachineGunnerStun` 等。
  - Assault：`AssaultShotgunIdle`、`AssaultShotgunRun`、`AssaultShotgunFire`、`AssaultFly`、`AssaultStartJump`、`AssaultStun` 等。
- 但这些 clip 是 Unity 压缩动画（`m_MuscleClip`/binding constant），当前仓库没有可直接求值的解码器；现有 `reference/nsync/tools` 只导出 texture/sprite/text，不包含骨骼/动画采样工具。

### 2.4 当前资产/构建约束

- `reference/` 不参与构建，且已加入 `.gitignore`；不要把反编译代码或原始提取资源提交到仓库。
- 当前 `assets/` 是可参与构建的占位资源；原版资源不能直接复制进 `assets/` 后无条件提交/分发。
- 第一步需要决定：原版角色美术是**仅本地开发使用**，还是允许把派生图集放入仓库/发行包。该决定影响 CI、打包和许可证。
- 现有 `nsync_atlas` 只支持单帧矩形/ pivot，不支持运行时骨骼、部件层级或旋转 quad。

### 2.5 本次 AssetRipper 导出实测（2026-10-30）

用户提供的新导出目录：`E:\迅雷下载\AssetRipper_win_x64\t4t4t\AssetRipper_export_20261030_112341\Assets`

已确认：

- `Resources\heroprefabs\characters\scout\ScoutPref.glb`、`machinegunner\MachineGunnerPref.glb`、`assault\AssaultPref.glb` 包含完整节点层级和局部 Transform：
  - ScoutPref：48 个节点（Hero/playerPivot/bodyAnim/headAnim/shoulder/hand/foot/weapon）。
  - MachineGunnerPref：44 个节点。
  - AssaultPref：49 个节点（含 jetPack）。
- 但这些 GLB **没有 mesh/skin/animation**：三个 prefab 的 `animations = []`；扫描全部 1498 个 GLB，带动画的数量为 **0**。
- `Resources\index.bytes` 只是资源清单，能列出 `ScoutIdle`、`ScoutRocketRun`、`MachineGunnerIdle`、`AssaultShotgunIdle` 等动画名和 controller，但导出目录中**没有实际 `.anim`/`.controller` 文件**。
- `Resources\*.json` 是 Sprite 元数据（含 `m_RD.m_Vertices`、`m_PixelsToUnits`、`m_TextureRect`），不是动画。
- 原始 `resources.assets` 里仍有 660 个 `AnimationClip`；抽检 `ScoutIdle` 可见 `m_Compressed=false`、`m_DenseClip.m_CurveCount=0`、`m_StreamedClip.curveCount=16`、`m_ConstantClip` 等数据，但没有可直接求值的骨骼/曲线导出器。
- 结论：**当前导出足以做角色拼装，不足以直接播放原版动画**。要逐帧原版动作，需要重新导出 `.anim`/Animator 或写 streamed clip 解码；第一版建议不依赖它。

已做静态拼装验证：

- 用 GLB 节点层级 + `Resources\ScoutBody1.json` 等 Sprite 顶点/PPU + `reference\nsync\extracted\sprites` 的 PNG，临时拼出 Scout/MachineGunner/Assault 的 rest pose。
- 三个英雄的部件位置/颜色/朝向正确；Scout 的躯干与靴子本来就是分离的（原版 Steam 截图同样如此，这是原版漂浮部件风格，不要擅自补腿）。
- 结论：**原版角色美术可以接出来**；当前主要缺口是动画和批量导入，而不是拼装本身。

## 3. 可选实现路线

### 方案 A：原版部件 + 分层 puppet + 程序化 pose（推荐第一版）

用 prefab 的 rest 层级/部件 `m_RD.m_Vertices` 作为初始骨架，用原版 PNG 作为部件纹理；运行时每个英雄画 6–9 个带旋转/缩放的 quad。当前状态机仍提供 `idle/run/jump/fall/shoot/hurt/death`，每个状态用 JSON 配置的程序化关键帧驱动部件旋转/位移。

- 优点：
  - 使用真正的原版角色美术，不依赖 Unity 动画解码；当前 GLB 没有动画也能开工。
  - 不需要把 3×7×4 帧预先烘焙成巨大图集。
  - 可以保留当前状态机、朝向、受击闪白、死亡淡出和武器切换逻辑。
  - 数据驱动，后续可加皮肤或替换 pose。
- 缺点：
  - 动作是近似原版，不是逐帧一致。
  - 需要新增多 quad 旋转绘制/部件 z-order/pose 求值；不能再只画一张图。
- 工作量：约 3–5 个工作日（含导入、pose 调试、截图验收）。

### 方案 B：用原版 prefab + 原版动画烘焙成图集（画质最高，风险最高）

拿到可解析的原版 `.anim`/FBX（重新导出）或解码 `m_MuscleClip` 后，把每个状态采样成合成帧，再交给现有 `nsync_atlas`。

- 优点：动作最接近原版，运行时仍可用单 quad/现有图集管线。
- 缺点：当前这份导出**没有动画文件**，GLB 的 `animations` 全为空；需要先补导出或实现 streamed clip 解码。坐标/皮肤/z-order/采样对齐容易出错；图集尺寸大。
- 建议：先做 A；如果 A 的动作质量不被接受，再要求补一份带 `.anim` 的导出，单独立项做 B。

### 方案 C：只做静态原版 composite

把每套皮肤拼成一张 rest pose 静帧，仅做轻微上下浮动/翻转。

- 实现最快，但动作仍会“像纸片”，不满足“战斗角色不简陋”的目标。
- 不推荐作为最终方案，只可作为导入管线验证。

### 方案 D：完整运行时骨骼 + 原版动画

运行时实现 Unity 动画采样 + 骨骼层级 + 蒙皮。质量最高，但超出本次插队需求，不建议现在做。

## 4. 推荐范围（方案 A）

### 4.1 第一版使用基础皮肤

- 只使用每个英雄的皮肤 `1`（如 `ScoutBody1`、`MachineGunnerBody1`、`AssaultBody1`）。
- 7 套皮肤、皮肤选择 UI、原版皮肤解锁逻辑留到后续。
- 角色名/技能名仍来自现有 `content.json`，不改英雄数值和技能。

### 4.2 部件与层次

每个英雄至少包含：

- 后层：`ShoulderBack`、`HandBack`、`FootBack`（若原版拆前后脚）。
- 中层：`Body`、`FootFront`。
- 前层：`ShoulderFront`、`HandFront`、`Head/Hat`、武器。
- 层次顺序参考 `SpriteRenderer.SortingOrder`：脚 0、后肩 2、身体 4、前肩 10、头 11、后手 21、武器 22、前手 23；不要只按文件名字母序。
- 注意 prefab 中 `footBackAnim` 的子节点名是 `footFrontSprite`，`footFrontAnim` 的子节点名是 `footBackSprite`；以节点路径为准，不要按名字猜。
- 所有部件使用原版 Sprite 的 `m_RD.m_Vertices` / pixels-per-unit / prefab 局部变换；世界坐标以当前玩家脚底/中心为基准归一化到约 1.8 世界单位高。不要使用截图像素硬编码。
- 原版角色是漂浮部件风格：躯干和靴子之间本来就有间距（Steam 截图可验证），不要添加假腿去“修”它。
- 朝向沿用 `player.facing`；翻转时同步翻转所有部件和武器，不改变瞄准方向/命中盒。

### 4.3 动作

第一版必须覆盖：

- `idle`：呼吸/轻微躯干与头部摆动。
- `run`：双脚交替、手/肩反向摆动，身体前倾。
- `jump`：起跳姿态；`fall`：下落姿态。
- `shoot`：前手/武器后坐 + 肩部小幅回弹；可复用当前 `fired` 事件。
- `hurt`：短时后仰/闪白。
- `death`：倒地/下沉/淡出，至少不能保持站立 idle。
- 武器：根据当前 `player.weapon` 选择对应原版武器 sprite 并挂到 `weapon/pivot/allWeaponAnim` 的对应子节点；建议第一版映射：
  - `rifle` → `SoldierRifle1`（或当前最接近的 rifle）
  - `shotgun` → `shotgun1`
  - `rocket` → `rocketLauncher1`
  - `smg` → `minigun1`
  - `burst` → `LaserGun1`
  - `grenade` → `grenade1` / `grenadeLauncher1`
  - 没有对应图时回退当前几何/隐藏武器；不要因为缺图阻塞角色本体。
- 第一版动作使用**程序化 pose**，不宣称复刻原版动画；原版 `ScoutIdle` 等 clip 在当前导出中不可直接播放。
- 头部/武器追踪 aim 可放第二版；第一版只做整体翻转和武器旋转。

### 4.4 队伍可读性与本地标记

- 原版美术本身有颜色；第一版优先使用**队伍色描边/脚下环/小标记**而不是整张图染色。
- 若保留现有 tint，需提供开关并确认不会把原版皮肤细节完全染成蓝/红。
- 保留当前本地玩家头顶小标记、HP/护盾条位置、受击闪屏和血效开关。
- 死亡/阵亡状态仍需和现有 HUD/复活逻辑一致。

### 4.5 资产导入与回退

- 新增开发期导入工具（建议 `scripts/import_original_heroes.py`，仅开发用，不加入 CMake 运行时）：
  - 输入优先级：
    1. 用户新导出的 `Assets\Resources\heroprefabs\characters\{scout,machinegunner,assault}\*Pref.glb`（节点/Transform）。
    2. 同目录 `Resources\*1.json`（Sprite 顶点/PPU）。
    3. `reference\nsync\extracted\sprites\*1.png`（已裁好的部件 PNG；现有最可靠）。
  - 如果要从新导出直接取纹理，也可用 UnityPy 按 Sprite 的 `m_TextureRect` 从原图集裁切，避免依赖旧 `reference` 目录。
  - 读取 prefab 层级/Transform/SpriteRenderer/Sprite 顶点，输出：
    - 派生部件图集/元数据（例如 `assets/generated/original_heroes/`）。
    - `assets/data/hero_rigs.json`（部件、顶点矩形、rest 变换、z-order、武器挂点、皮肤）。
  - 原版 PNG 不直接进普通构建包；生成物是否提交按第 8 节决定。
- 运行时优先尝试加载原版 rig/图集；缺失、格式错误或 `client.hero_art=placeholder` 时回退当前占位图集。
- 建议配置：`client.hero_art=auto|placeholder|original`，默认 `auto`（检测到原版资源才启用），并提供 `--hero-art original` 做本地截图/验收。
- `--hot-reload`/F5 继续支持：rig JSON、部件图集、pose JSON 变更后重载；失败保留旧资源。

### 4.6 不改变的部分

- `core::World`、玩家 AABB/命中判定、武器数值、技能、比分、快照、协议、golden replay。
- 现有 `UiHero` 卡牌仍显示颜色/首字母；英雄头像/技能图标不在本需求。
- 不引入 Unity 运行时、Puppet2D 或原版 prefab 作为运行时依赖。

## 5. 建议数据结构

具体 schema 可在实现时定稿，建议至少包含：

```json
{
  "version": 1,
  "heroes": {
    "scout": {
      "scale": 1.0,
      "parts": [
        {
          "name": "body",
          "sprite": "ScoutBody1",
          "z": 0,
          "pivot": [0.5, 0.5],
          "rest": {
            "position": [0.0, 0.0],
            "rotation": 0.0,
            "scale": [1.0, 1.0]
          }
        }
      ],
      "poses": {
        "idle": [
          {"part": "body", "rotation": 0.0, "position": [0.0, 0.0]},
          {"part": "head", "rotation": 2.0, "position": [0.0, 0.0]}
        ],
        "run": []
      }
    }
  }
}
```

- `pose` 由当前状态、状态时间、速度/着地/开火事件驱动。
- 部件位置/旋转使用 prefab 局部坐标或归一化到脚底的坐标；不要硬编码屏幕像素。
- `z` 用整数排序，确保后肩/后手在 body 后、前肩/前手/头/武器在前。
- 每帧不允许动态分配；预加载 rig/atlas，运行期只更新 transform。

## 6. 验收标准

### 6.1 视觉

- 3 个英雄在 960×600 和 1280×720 下均完整显示，无缺部件、无穿模级别的错位。
- idle/run/jump/fall/shoot/hurt/death 状态肉眼可区分；朝向翻转正确；受击闪白/死亡淡出仍工作。
- 原版角色高度、脚底基线、HP/护盾条位置对齐；不能出现整体悬空或陷地。
- 原版躯干/靴子的分离间距保留，和 Steam 原版视觉一致；不得添加假腿或把部件强行吸附。
- 队伍/本地玩家仍能快速辨认；不能因原版颜色导致完全分不清队伍。
- 武器与前手/身体连接合理；切换武器时武器 sprite 跟随。
- 第一版验收以“原版部件 + 可读程序化动作”为准，不要求与 `ScoutIdle` 等原版 clip 逐帧一致。

### 6.2 玩法与回归

- 玩家 AABB、射击命中、技能、比分、复活、控制点行为不变。
- 快照版本、协议版本、content fingerprint、golden replay 不变；Debug/Release CTest 16/16。
- 菜单/大厅/HUD/设置/加载页/本地化不回归。
- 缺失原版资源时自动回退占位图集，不崩溃、不黑屏。

### 6.3 性能

- 10 个玩家、每人最多约 9 个部件时，客户端 CPU 增加不超过约 0.5 ms/帧（目标值，实测记录）。
- 同屏部件使用同一张图集/同一批次，避免每个部件单独 draw call；运行期无每帧堆分配。
- 纹理内存和启动加载时间记录在验收结果中；不能因为原版图集导致启动明显变慢。

### 6.4 内容/热重载

- `--hot-reload` 下修改 rig/pose/部件图集，F5 或文件变更后能生效；失败保留旧视觉。
- 资源缺失/格式错误有 warning 和回退，不显示残缺骨架。

## 7. 实施顺序（建议）

1. **导入验证**：用 UnityPy 读取 3 个 prefab 层级/Sprite pivot，导出基础皮肤部件和 rig JSON；先做 Scout 的静态 rest composite 验证坐标/比例。
2. **数据层**：扩展 atlas 或新增 `hero_rigs.json`/`hero_poses.json`；加入 `auto/placeholder/original` 模式。
3. **渲染层**：`WorldRenderer` 增加多部件旋转 quad、z-order、flip、team accent/local marker；保留现有 fallback。
4. **动作层**：实现 idle/run/jump/fall/shoot/hurt/death pose，接现有 `select_animation_state` 和开火/受击/死亡事件。
5. **武器层**：按当前武器 id 映射原版武器 sprite，处理 hand/weapon 挂点、切换和翻转。
6. **验收**：3 英雄 × 7 状态截图、性能、热重载、Debug/Release CTest；更新 README/PHASE8_RESULTS。

粗估：**3–5 个工作日**（方案 A，基础皮肤，不含原版动画精确复刻/7 套皮肤/头像纹理）。

## 8. 决策记录与待确认

**已确认（2026-10-30）**：

- 用户选择**方案 A**：原版基础皮肤分件 + 分层 puppet + 程序化 pose；第一版不依赖原版动画 clip。
- 基础版已实现并通过 Debug/Release 回归；用户要求的“先写文档、确认后开始”流程已执行。

**仍待确认（不阻塞文档）**：

1. **资源分发**：原版派生图集是否允许提交到仓库/发行包？建议默认仅本地开发/演示使用，CI 使用占位回退；如果需要可分发，需要单独确认授权/来源。
2. **动作 fidelity**：方案 A 已确认；如果后续要求逐帧原版动画，需要补一份带动画的导出（`.anim` 或 FBX）或单独解码 streamed clip，再转方案 B。
3. **皮肤范围**：第一版只做皮肤 1，还是同时做 7 套皮肤？建议只做 1。
4. **队伍色**：使用描边/脚下环保留原色，还是继续整张 tint？建议描边/环 + 轻微 tint。
5. **武器**：第一版是否同时替换武器 sprite？建议先接 rifle/shotgun/rocket 三个基础武器，其余回退占位/隐藏。
6. **默认启用**：`client.hero_art=auto` 还是默认 `placeholder`？建议 auto，找不到原版资源自动回退。
7. **是否允许引入 UnityPy 作为 dev-only 依赖**？建议只在 `scripts/` 导入工具使用，不进入运行时/CMake。
8. **是否可重新导出动画**：如果后续必须原版动作，能否用 AssetStudio/AssetRipper 补导出 `ScoutIdle` 等 `.anim` 或 FBX？这决定方案 B 的成本。

## 9. 参考

- 现有管线：`assets/sprites/source/*.png`、`assets/sprites/heroes.atlas.json`、`assets/data/animations.json`、`scripts/gen_placeholder_sprites.py`、`assets/ui/game.rml`。
- 原版导入：`scripts/import_original_heroes.py`；生成 `assets/generated/original_heroes/heroes.png`、`heroes.atlas.json`、`assets/data/hero_rigs.json`；pose 参数在 `assets/data/hero_poses.json`。
- 原版数据层：`src/core/hero_rig.h`、`src/core/hero_rig.cpp`（rig/pose 解析、校验、查找）。
- 原版渲染/动作：`src/client/world_renderer.cpp`（多部件 quad、z-order、翻转、程序化 pose、武器挂点、闪白、死亡淡出、fallback）、`src/client/world_renderer.h`。
- 配置/热重载：`src/client/main.cpp`（`--hero-art`）、`src/client/bgfx_app.cpp`（mode 传递、watcher）。
- 结果文档：`docs/ORIGINAL_HERO_ART_RESULTS.md`。
- 新导出（用户提供）：`E:\迅雷下载\AssetRipper_win_x64\t4t4t\AssetRipper_export_20261030_112341\Assets`
  - prefab：`Resources\heroprefabs\characters\scout\ScoutPref.glb`、`machinegunner\MachineGunnerPref.glb`、`assault\AssaultPref.glb`
  - sprite 元数据：`Resources\ScoutBody1.json`、`MachineGunnerBody1.json`、`AssaultBody1.json` 等
  - 资源清单：`Resources\index.bytes`
- 原版 prefab/层级（原始资产）：`NSYNC_Data/resources.assets` 中的 `ScoutPref`、`MachineGunnerPref`、`AssaultPref`。
- 原版资源：`reference/nsync/extracted/sprites/Scout*`、`MachineGunner*`、`Assault*`、`SoldierRifle*`、`minigun*`、`shotgun*`、`rocketLauncher*`、`LaserGun*`。
- Steam 原版参考截图（用于确认漂浮部件/整体比例）：`https://store.steampowered.com/app/511800/NSYNC/` 的截图。
- 反编译参考：
  - `reference/nsync/decompiled/HeroPrefab.cs`
  - `reference/nsync/decompiled/CharacterEntity.cs`
  - `reference/nsync/decompiled/SkinnedSprite.cs`
  - `reference/nsync/decompiled/Puppet2D_SkinnedVertices.cs`
  - `reference/nsync/decompiled/HeroSkinComponent.cs`
  - `reference/nsync/decompiled/WeaponAnimationHelper.cs`
  - `reference/nsync/decompiled/Assets.Script.Core.Main.RendererEquipStructs/SkinRenderer.cs`
- 原始 prefab/动画探测工具：UnityPy（仅离线调研/导入，不加入运行时）；`reference/nsync/tools/` 现有导出脚本只处理 texture/sprite/text。



