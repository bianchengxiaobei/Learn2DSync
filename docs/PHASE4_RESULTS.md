# Phase 4 结果：本地多人测试台

日期：2026-10-29  
状态：通过

## 目标

在同一进程内模拟一个权威服务器和多个客户端，验证输入延迟、快照广播、客户端预测、回滚重放和网络异常下的确定性。

## 已实现

- `net::LocalServer`
  - 权威 `World`，接收按目标 tick 提交的输入。
  - 输入按 tick/slot 排队；重复输入只接受一次，过期输入丢弃。
  - 每 tick 生成快照。
- `net::LocalClient`
  - 独立预测 `World`，按 `input_delay` 提前生成输入。
  - 接收服务器输入广播，保存远端输入历史。
  - 接收服务器快照，比较快照 tick 处的预测哈希。
  - 预测一致时不重放；不一致时恢复快照并从快照 tick 重放到当前 tick。
  - `inject_desync()` 用于自动测试人为污染预测状态。
- `net::NetworkSimulator`
  - 配置单向延迟 ticks、抖动 ticks、丢包率、重复率。
  - 包按投递 tick 入队；抖动造成乱序；重复包延迟复制。
  - 统计发送、丢弃、重复、投递和 RTT。
- `net::NetGraph`
  - 环形保存最近 240 个样本：tick、ack tick、RTT、实际回滚 tick、客户端/服务器哈希、是否匹配。
- `net::LocalSession`
  - 同一进程内 1–10 个客户端、输入 provider、服务器和网络模拟器。
  - 客户端发送输入；服务器接受后广播输入；服务器步进后广播快照。
- `nsync_net` headless 工具
  - 可配置客户端数、RTT、抖动、丢包、重复、快照周期、运行 tick 数。
  - 输出 netgraph 摘要、rollback/desync 统计和最终预测状态。
- `nsync_client --net`
  - 同一进程内启动 4 个 `LocalClient`（本地玩家 + 3 个确定性输入 provider）和 1 个 `LocalServer`。
  - 渲染客户端 0 的预测世界；HUD 追加 ack、RTT、回滚和 correction。
  - 默认 RTT 150ms、抖动 30ms（按 60Hz 量化后约 167ms/33ms），可用 `--net-latency-ms`、`--net-jitter-ms`、`--net-loss`、`--net-duplicate` 配置。

## 同步模型

- 服务器和客户端使用同一套固定步长 `World`。
- 客户端在 tick `T` 为 `T + input_delay` 生成输入；服务器在目标 tick 应用该输入。
- 服务器接收输入后广播给其他客户端，使本地测试台中的预测端也拥有远端输入。
- 客户端在收到的服务器快照 tick 比较预测哈希：
  - 一致：只确认 ack，不恢复/重放。
  - 不一致：恢复权威快照，重放快照 tick 之后的输入。
- 默认测试参数为 5 tick 单向延迟（约 167ms RTT）、2 tick 抖动（约 33ms）、16 tick input delay。

## 验证

构建和测试：

```powershell
cmake --build --preset debug
cmake --build --preset release
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

结果：Debug/Release **5/5** 通过。

自动测试覆盖：

- 网络模拟器延迟、100% 丢包、重复投递。
- 2 客户端 600 tick 预测一致性：无 correction/desync。
- 人为注入客户端预测状态后，快照回滚修复，最终客户端/服务器哈希一致。
- 10% 丢包 + 10% 重复、抖动 2 tick、1200 tick：产生 correction/rollback，客户端仍持续收到 ack。
- 150ms RTT/30ms 抖动的 **36,000 tick（10 分钟）**确定性测试：客户端最终哈希与服务器一致，`corrections=0`、`desyncs=0`。

命令行示例：

```powershell
.\nsync_net.exe --ticks 36000 --latency-ms 150 --jitter-ms 30 --log-every 0
.\nsync_net.exe --ticks 1200 --latency-ms 150 --jitter-ms 30 --loss-percent 10 --duplicate-percent 10
```

`nsync_net` 的 `--latency-ms` 表示 RTT；`--jitter-ms` 表示单向抖动。

可视客户端本地网络 smoke：

```powershell
.\nsync_client.exe --net --no-vsync --frames 600 --ticks 1800 --log-level warn
```

退出码 0。

## 当前边界

- 可视客户端支持 `--net`，但仍是同一进程内的 4 个本地 `LocalClient`/1 个 `LocalServer`；不是真实外部服务器。
- 为让本地测试台的每个客户端拥有远端输入，服务器会广播输入；真实网络阶段需要改为更严格的输入/快照协议。
- 输入延迟较高（默认 16 tick），因为输入需要经过“客户端 → 服务器 → 其他客户端”；Phase 5 接 KCP 后再优化。
- 快照仍是全量、未压缩，网络模拟器传递内存对象而非真实字节流；序列化/带宽优化留给 Phase 5/9。
- 本地测试台不含机器人 AI；输入由 `InputProvider` 提供。

## 关键文件

- `src/net/local_net.h/.cpp`
- `src/tools/net_main.cpp`
- `src/client/bgfx_app.cpp`、`src/client/main.cpp`
- `config/client.ini`
- `tests/test_net.cpp`
- `CMakeLists.txt`
