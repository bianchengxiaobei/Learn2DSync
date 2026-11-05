# Phase 1 工程骨架结果

> 日期：2026-10-29  
> 目标：可构建、可测试、客户端/服务器可启动的工程空壳。

## 结论

**Phase 1 通过。** Debug/Release 均构建成功，本地 CTest 3/3 通过；服务器 headless 循环稳定跑 60Hz；客户端正常开窗并显示 RmlUi，固定步长累计器在运行。

## Targets

| Target | 说明 |
|---|---|
| `nsync_core` | 静态库，只依赖标准库；定点数、固定步长、日志、配置、路径、资源挂载 |
| `nsync_client` | SDL3 + bgfx + RmlUi 客户端 |
| `nsync_server` | headless 服务器，固定步长 60Hz |
| `nsync_tests` | 单元/冒烟测试 |
| `nsync_pak` | 资源打包工具（当前未压缩格式） |

## 验证

### 构建

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
```

### 测试

```powershell
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

结果：

```text
1/3 nsync_core_tests     Passed
2/3 nsync_client_help    Passed
3/3 nsync_server_smoke   Passed
100% tests passed
```

### 服务器 60Hz

```powershell
.\build\vs2022\Debug\nsync_server.exe --ticks 121 --tick-rate 60 --log-level info
```

实测：

```text
headless loop started: tick_rate=60 max_catchup=5 max_ticks=121
tick=60 simulated=1s dropped=0s
tick=120 simulated=2s dropped=0s
stopped at tick=121
```

总耗时约 2.10 秒，符合 121 tick × 16.67ms。

### 客户端

```powershell
.\build\vs2022\Debug\nsync_client.exe --frames 60 --simulate-click
```

结果：

- 正常创建 1280×720 SDL3 窗口。
- bgfx D3D11 初始化成功，sprite 可见。
- RmlUi 面板、按钮、状态文本、HUD 正常显示。
- `--simulate-click` 后状态文本变为 `Button clicked 1 time(s). SDL3 + bgfx + RmlUi are alive.`
- HUD 显示固定 tick；截图时后台窗口被系统限帧，FPS 显示较低，但服务器/客户端 tick 计数按固定步长推进。

### 资源挂载

- 开发目录：客户端优先挂载可执行文件旁的 `assets/`。
- pak：当可执行文件旁没有 `assets/` 时，挂载 `assets.pak`，RmlUi 通过自定义 `RmlResourceFile` 读取。
- 已验证：

```powershell
.\build\vs2022\Debug\nsync_pak.exe assets build\vs2022\Debug\assets.pak
# 临时移走 build\vs2022\Debug\assets 后启动客户端
[INFO] [client] mounted assets.pak: ...\assets.pak
```

## 代码结构

```text
src/core/fixed_step.*      固定步长 accumulator
src/shared/log.*           分级日志
src/shared/assert.h        断言/致命错误
src/shared/config.*        key=value 配置
src/shared/paths.*         可执行文件/源码/资源路径查找
src/shared/pak.*           自定义 pak 读写
src/shared/resource.*      目录/pak 资源挂载
src/client/bgfx_app.*      SDL3/bgfx/RmlUi + 固定步长主循环
src/client/rml_resource_file.*  RmlUi 文件接口
src/server/server_app.*    headless 60Hz 循环
src/tools/pak_pack.cpp     pak 打包 CLI
tests/test_main.cpp        单元测试
```

## 当前边界

- pak 是未压缩自定义格式，仅用于验证挂载路径。
- shader 仍仅编译 DX11，客户端强制 D3D11。
- 客户端渲染仍是 Phase 0 的 checker sprite + 测试面板。
- KCP 还没接入 Phase 1 target；Phase 5 再做真实网络。
- CI workflow 已写入 `.github/workflows/windows.yml`，但当前目录不是 Git 仓库/没有远端，未实际触发。

## 下一步

Phase 2：确定性核心。

- `Fix32`/Vec2/AABB/线段
- PCG32/xorshift 确定性 RNG
- 实体 ID、组件存储、世界遍历
- `InputCommand` / `Tick` / `World::step`
- 快照 writer/reader + 哈希
- replay：输入日志 → tick 哈希
- 定点/序列化/快照恢复单元测试
- golden replay 测试
