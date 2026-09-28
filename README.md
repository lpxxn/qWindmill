# qWindmill

Qt 6 Widgets + C++20 版 windmill 设备实时查询工具。对应 Go 源码：`apps/windmill/cmd/read-devices` 和 `apps/windmill/internal/collector`。输入一个或多个 `DeviceDefinition.Address`，程序按两台一组读取当前寄存器，在树形表中显示每台设备的全部 32 项测量数据。

一键生成 Windows Release ZIP：在 PowerShell 7 中运行 `./release.ps1`。脚本会构建、测试、收集 Qt 依赖、做启动检查，并生成 ZIP 与 SHA-256 校验文件；参数和逐步命令见 [RELEASE.md](RELEASE.md)。

## 用 Qt Creator 打开

1. 用 Qt Creator 打开本目录的 `CMakeLists.txt`。
2. 选择 Qt 6.11.2 MinGW 64-bit Kit，构建配置使用 C++20。
3. 构建并运行 `qWindmill`。
4. 输入 `1`、`1,2,39` 或 `3 4`，点击“立即查询”。

已经通过 Qt Creator 或 CMake 构建后，也可在本目录运行 `./run.ps1`，脚本会为当前进程补齐 Qt/MinGW 运行库路径，再启动窗口。

命令行构建示例（按本机 Qt 安装位置）：

```powershell
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;' + $env:PATH
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\mingw_64
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
```

## 代码阅读顺序

1. `src/main_window.ui`：Qt Designer 可编辑的窗口布局。这里定义输入框、按钮、结果树和日志框。
2. `src/main_window.cpp`：界面行为、信号连接、线程生命周期。`ui_main_window.h` 由 AUTOUIC 生成，不手工修改。
3. `src/query_plan.cpp`：从 Address 找设备定义，再用 `(Index-1)/2` 找采集批次。1/2 对应 `0x0000` 读 98，3/4 对应 `0x0062` 读 98；39 对应 `0x0746` 读 49。
4. `src/collector_worker.cpp`：每个批次单独建立 TCP 连接，发送请求，精确读取响应头和数据区，按配置重试与限速。工作线程避免阻塞界面。
5. `src/protocol.cpp`：构造厂家私有帧、验证响应、按 Go 的偏移和倍率解码全部测量项。
6. `tests/protocol_test.cpp`：对照 Go 的实际请求帧和设备清单验证映射。
7. `tests/window_test.cpp`：用 Qt 控件点击和本地假网关验证界面、线程、TCP 与结果树的完整流程。

## C++20 与 Qt 学习提示

- `std::optional` 表示“解析成功后有计划/数据，失败时没有”。调用方必须检查 `if (!plan)`，不能把错误输入当成空批次。
- `std::bit_cast<qint32>` 保留寄存器组合后的原始位型，再按补码解释负值；这与直接把 `quint32` 数值转换成 `qint32` 的意图不同。
- `QObject::moveToThread` 改变对象的线程归属。工作线程里创建 `QTcpSocket`，通过信号把结果传回主线程；窗口控件只能在主线程修改。
- `QPointer` 在其指向的 `QObject` 被销毁后自动变空。关闭窗口时先设置原子取消标记，再等待线程退出，避免工作对象仍在使用时窗口被释放。
- `.ui` 文件由 Qt Designer 编辑，CMake `AUTOUIC` 生成 `ui_main_window.h`。修改界面布局请改 `.ui`；修改按钮行为请改 `MainWindow::connectUi()` 和槽函数。

## 与 Go CLI 保持一致的约束

- 设备 Address 是业务地址，不能直接当作寄存器地址。
- 一个批次最多读取两台设备的 98 个寄存器；最后一批只有 Address 39，读取 49 个。
- 相邻地址属于同一批次时只发一条请求。未选中的相邻设备会被解码，但界面只显示用户选中的设备。
- 每批独立建连，默认超时 5 秒、批次间隔 3.2 秒、额外重试 2 次、重试间隔 1 秒。
- `Status` 是原始寄存器值，不把它猜测为开闸或合闸。
- 界面的“请求与状态”页显示服务地址、连接 IP、起始寄存器、数量及完整十六进制请求帧。
