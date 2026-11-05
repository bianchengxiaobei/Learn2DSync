# Phase 0 Spike 结果

> 日期：2026-10-29  
> 目标：验证 SDL3 + bgfx + RmlUi + KCP + 定点数这条技术路线，先不写业务代码。  
> 注：Phase 1 起可执行文件已重命名为 `nsync_client` / `nsync_server`；本文件保留 Phase 0 当时的命令与结果。

## 结论

**Phase 0 通过。** 五条验收线都跑通了：

| 验收项 | 结果 |
|---|---|
| SDL3 + bgfx 开窗、清屏、画 sprite | 通过 |
| RmlUi 面板/按钮显示并能触发点击 | 通过（`--simulate-click` 自动点击，状态文本更新） |
| 定点数加减乘除/开方/角度查表 | 通过（`--fixed-test`） |
| 两个进程用 KCP 互发消息 | 通过 |
| 模拟 100ms RTT + 5% 丢包 | 通过（每端 50ms 延迟、5% 丢包） |

## 运行方式

```powershell
# 配置 + 构建
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release

# 定点测试
cd build\vs2022\Release
.\nsync_phase0.exe --fixed-test

# 窗口 spike：SDL3 + bgfx sprite + RmlUi
.\nsync_phase0.exe --frames 180

# 窗口 spike + 自动点击 RmlUi 按钮
.\nsync_phase0.exe --frames 240 --simulate-click

# KCP 两个进程：先 server，再 client
.\nsync_phase0.exe --kcp-server --port 40100 --duration 7000 --delay-ms 50 --loss 5
.\nsync_phase0.exe --kcp-client --port 40100 --duration 5000 --delay-ms 50 --loss 5
```

> `build\vs2022\Debug` 和 `build\vs2022\Release` 都会在构建后复制 `assets/` 和 `shaders/`。  
> 从仓库根目录运行也可以：程序会回退到源码目录/构建目录查找 shader。

## 已验证内容

### 1. SDL3 + bgfx

- SDL3 创建 1280×720 可缩放窗口。
- 通过 Win32 `HWND` 交给 bgfx，强制使用 **Direct3D 11**。
- bgfx 初始化日志确认 renderer：`Direct3D 11`，NDC depth `[0, 1]`，origin top left。
- 程序绘制了一个 256×256 的程序化棋盘 sprite；位置随时间轻微移动，证明主循环在跑。
- 关闭窗口 / Esc 正常退出；修复了 `BgfxUiRenderer` 在 `bgfx::shutdown()` 之后析构导致的访问冲突。

### 2. RmlUi + bgfx 自定义 RenderInterface

- 实现了最小 `Rml::RenderInterface`：
  - `CompileGeometry` / `RenderGeometry` / `ReleaseGeometry`
  - `GenerateTexture` / `ReleaseTexture`
  - `LoadTexture` 暂未实现（当前 UI 不加载图片）
  - `EnableScissorRegion` / `SetScissorRegion`
  - `SetTransform`（平移 + 可选 4×4 变换）
- UI 使用独立 view（view 1），世界使用 view 0；view 1 使用 `ViewMode::Sequential`。
- RmlUi 字体使用 Windows `segoeui.ttf`，通过 `Rml::LoadFontFace(..., "SpikeFont", ...)` 注册。
- 面板、标题、状态文本、按钮、提示文本均正常显示。
- `--simulate-click` 调用 `Element::Click()`；状态文本变为：
  `Button clicked 1 time(s). SDL3 + bgfx + RmlUi are alive.`

### 3. 定点数

- `src/core/fix32.h`：Q16.16 `Fix32`、`Vec2`。
- 支持：加、减、乘、除、取负、比较、绝对值、min/max、sqrt、lerp、`to_int_trunc/round`。
- `src/core/fix_trig.h`：256 项正弦表 + 线性插值，`sin_deg` / `cos_deg`。
- 表由 `scripts/gen_trig_table.py` 生成。
- 测试覆盖：1/2、3×7、-3×7、3/2、1/3×3、sqrt(4/9/2)、sin/cos 0/30/60/90/180/270/360、Vec2 3-4-5、归一化。
- `--fixed-test` 输出：`fixed-point tests: PASS`。

### 4. KCP 双进程

Release 构建的一次实测（示例；具体 RTT 会随机器和丢包时序变化）：

```text
kcp-client: sent=20 received=20 min=105ms avg=178.2ms max=426ms packets=43 dropped=3 errors=0 (6.5% simulated loss)
kcp-server: packets sent=25 dropped=2 errors=0 (7.4% simulated loss)
```

配置：客户端/服务端各加 50ms 延迟、5% 丢包，因此理论 RTT ≈ 100ms；实测基线 105–160ms，5% 丢包触发的重传会把单次 RTT 拉到 300–426ms。这符合 KCP 快速重传的预期。

KCP 细节：

- `ikcp_nodelay(kcp, 1, 10, 2, 1)`
- `ikcp_wndsize(kcp, 256, 256)`
- `ikcp_setmtu(kcp, 1200)`
- 固定 `conv = 0x4F4E5244`（"ONRD"）
- 服务端收到首个 datagram 后记录 peer 并创建 KCP；客户端预先解析 IPv4 地址。
- `SimulatedLink` 在应用层模拟延迟/丢包，KCP 仍看到真实 UDP 丢包并重传。

## 已知边界 / 后续要补

1. **Shader 只编译了 DX11**（`s_5_0`），程序强制 D3D11。Phase 1 再加 GLSL/SPIR-V 或按 renderer 选择。
2. **RmlUi 外部 RCSS 路径问题**：绝对路径加载时 RmlUi 结果显示为 `E|/NSYNC...`，导致 `<link>` 模板找不到。当前 `spike.rml` 使用内联 `<style>`。后续实现 `Rml::FileInterface` 或统一用相对路径/虚拟文件系统。
3. **RmlUi RenderInterface 是最小实现**：未实现 clip mask、layer、filter、custom shader、外部图片纹理；目前够 Phase 0 UI 用。
4. **字体是系统字体**（Segoe UI）。正式项目要打包开源字体，避免依赖用户系统。
5. **KCP 测试是单会话、固定 conv**：没有握手、session token、加密、超时踢人、重连。Phase 5 再补。
6. **快照未走网络**：Phase 0 只验证 KCP 消息通路；snapshot latest-wins、输入广播、回滚在 Phase 4/5。
7. **`--simulate-click` 用 `Element::Click()`**：验证了 RmlUi 事件监听/状态更新；SDL3 真实鼠标事件路径已接线，但自动化点击走的是 RmlUi 自身 API。

## 下一步

进入 **Phase 1：工程骨架**：

- `nsync_core` 静态库（不依赖 SDL/bgfx/RmlUi/KCP）
- `nsync_client` / `nsync_server` 两个 target
- 固定步长主循环、日志、断言、配置、资源路径
- CMake + vcpkg manifest + CI（Windows）
- 把 Phase 0 的 spike 代码拆成可复用模块，而不是继续堆在一个可执行文件里
