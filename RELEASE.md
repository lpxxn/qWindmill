# qWindmill Windows 发布操作手册

本文给出在当前开发机上生成 **Windows x64 便携 ZIP 包** 的完整命令。交付物是一个目录及其同名 ZIP；使用者解压后运行 `qWindmill.exe`。这里的“发布”指制作本地交付包，不包含上传网站或生成安装器。

本文已在 2026-09-28 用以下已安装工具验证：Qt **6.11.2 MinGW 64-bit**、MinGW-w64 **13.1.0**、CMake **3.30.5**、Ninja **1.12.1**。若安装目录或版本改变，先修改下面第 1 步中的路径，并始终使用**同一套 Qt 与 MinGW** 构建及部署。Qt 官方将 `windeployqt` 作为 Windows 应用收集 Qt DLL、插件和运行依赖的推荐工具；`--dir` 指定部署目录，`--release` 指定 Release 二进制。[Qt Windows 部署文档](https://doc.qt.io/qt-6/windows-deployment.html)

## 一键发布

使用 PowerShell 7，在项目目录直接运行：

```powershell
Set-Location 'C:\Users\mi_du\go\src\qWindmill'
.\release.ps1
```

脚本默认从 `CMakeLists.txt` 读取版本号，追加当前时间生成唯一发布标识。也可以自己指定名称和并行编译任务数：

```powershell
.\release.ps1 -ReleaseId 0.1.1 -Jobs 8
```

Qt、MinGW、CMake、Ninja 不在本文所列位置时，传入 `-QtDir`、`-MingwDir`、`-CmakeExe`、`-NinjaExe`。脚本依次完成下面第 1～6 步，自动运行 Release 测试、检查 EXE 是否为 Windows GUI 程序，并执行本机交付包启动检查；输出位于 `dist/`，包含完整目录、同名 ZIP 和 `.zip.sha256` 校验文件。它不会覆盖已有发布输出，也不会上传文件。若某一步失败，检查错误并换一个 `-ReleaseId` 重试；已经生成的部分目录会保留，便于排查。

2026-09-28 已以 `-ReleaseId 0.1.0-gui-fix-20260928` 完整运行该脚本，编译、两组测试、GUI 子系统检查、依赖部署、独立启动、ZIP 和校验文件全部成功。后文保留逐步命令，方便学习和单独排错。

## 0. 交付目录应该是什么样

最终 ZIP 解压后大致是：

```text
qWindmill-0.1.0-时间戳-windows-x64/
├─ qWindmill.exe
├─ Qt6Core.dll
├─ Qt6Gui.dll
├─ Qt6Widgets.dll
├─ Qt6Network.dll
├─ libgcc_s_seh-1.dll
├─ libstdc++-6.dll
├─ libwinpthread-1.dll
├─ platforms/
│  └─ qwindows.dll
└─ 其他由 windeployqt 自动选择的 DLL、插件和 translations/
```

`platforms/qwindows.dll` 的目录层级不能随意改动。源码、`.ui` 文件、CMake 构建目录以及测试程序不需要放进交付包；`.ui` 在构建时由 AUTOUIC 转成 C++ 代码。[Qt Windows 部署文档](https://doc.qt.io/qt-6/windows-deployment.html)

## 1. 打开 PowerShell，设定路径

以下所有命令在**同一个 PowerShell 窗口**依次执行。变量只对这个窗口有效。`releaseId` 加上时间戳是为了每次使用新目录，避免旧 DLL 混入新版本；正式发布时可改成固定版本号，但要保证同名目录尚不存在。

```powershell
$projectDir = 'C:\Users\mi_du\go\src\qWindmill'
$qtDir = 'C:\Qt\6.11.2\mingw_64'
$mingwDir = 'C:\Qt\Tools\mingw1310_64'
$cmakeExe = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctestExe = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$ninjaExe = 'C:\Qt\Tools\Ninja\ninja.exe'
$compilerExe = Join-Path $mingwDir 'bin\g++.exe'
$deployExe = Join-Path $qtDir 'bin\windeployqt.exe'
$buildDir = Join-Path $projectDir 'build-release'
$releaseId = '0.1.0-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$packageDir = Join-Path $projectDir ("dist\qWindmill-{0}-windows-x64" -f $releaseId)
$zipPath = "$packageDir.zip"

foreach ($tool in @($cmakeExe, $ctestExe, $ninjaExe, $compilerExe, $deployExe)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "工具不存在：$tool" }
}
Set-Location -LiteralPath $projectDir
$env:PATH = "$(Join-Path $qtDir 'bin');$(Join-Path $mingwDir 'bin');$env:PATH"
& $deployExe --version
```

此处将 Qt 与 MinGW 的 `bin` 加入当前进程的 `PATH`，既方便构建/测试，也让 `windeployqt` 找到匹配的编译器运行库。不要混用 Qt 的 `mingw_64` 和 `msvc2022_64` 构建目录。

## 2. 配置并编译 Release

用独立的 `build-release` 目录，不覆盖开发时的 `build`。项目使用 Ninja 单配置生成器，所以必须在配置时指定 `Release`；若这个目录以前按 Debug 配置过，请换一个新的构建目录名称。

```powershell
$configureArgs = @(
    '-S', $projectDir,
    '-B', $buildDir,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$($ninjaExe.Replace('\','/'))",
    "-DCMAKE_CXX_COMPILER=$($compilerExe.Replace('\','/'))",
    "-DCMAKE_PREFIX_PATH=$($qtDir.Replace('\','/'))",
    '-DBUILD_TESTING=ON'
)
& $cmakeExe @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败，请检查 Qt/MinGW 路径。' }

& $cmakeExe --build $buildDir --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Release 编译失败。' }

Get-Item -LiteralPath (Join-Path $buildDir 'qWindmill.exe')
```

期望看到 `build-release/qWindmill.exe`。CMake 中的 `WIN32_EXECUTABLE TRUE` 使它使用 Windows GUI 子系统，双击时不会自动打开控制台。它此时只是一份构建产物，还没有携带 Qt DLL 和插件，不能把这个 EXE 单独发给别人。

## 3. 运行 Release 测试

```powershell
& $ctestExe --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw '测试失败，停止制作发布包。' }
```

当前工程有两组测试：协议与批次测试、窗口与本地假 TCP 网关的交互测试。窗口测试由 CTest 设置 `QT_QPA_PLATFORM=offscreen`，可以在无人点击桌面的情况下验证按钮、请求报文和结果树。预期输出为 `100% tests passed, 0 tests failed out of 2`。

## 4. 创建干净的交付目录并收集依赖

`windeployqt` 扫描已编译的程序，复制对应的 Qt DLL、`platforms/qwindows.dll` 等插件，以及 MinGW 运行库。先只复制主程序，再对**交付目录中的 EXE** 执行部署。`--compiler-runtime` 显式要求复制编译器运行库；该选项和默认行为见[Qt Windows 部署文档](https://doc.qt.io/qt-6/windows-deployment.html)。

```powershell
if (Test-Path -LiteralPath $packageDir) { throw "交付目录已存在，请换 releaseId：$packageDir" }
New-Item -ItemType Directory -Path $packageDir | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir 'qWindmill.exe') -Destination (Join-Path $packageDir 'qWindmill.exe')

& $deployExe --release --compiler-runtime --dir $packageDir (Join-Path $packageDir 'qWindmill.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt 收集依赖失败。' }
```

本项目是 Qt Widgets + Qt Network 程序，没有 QML，不需要 `--qmldir`。部署日志中的 `Direct dependencies` 应包含 Qt6Core、Qt6Gui、Qt6Network、Qt6Widgets。插件及 DLL 由工具按依赖选择，不要只手工复制几个顶层 DLL。[Qt Windows 部署文档](https://doc.qt.io/qt-6/windows-deployment.html)

## 5. 检查交付目录

```powershell
$requiredFiles = @(
    'qWindmill.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'Qt6Network.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll',
    'libwinpthread-1.dll', 'platforms\qwindows.dll'
)
foreach ($relativePath in $requiredFiles) {
    $candidate = Join-Path $packageDir $relativePath
    if (-not (Test-Path -LiteralPath $candidate)) { throw "交付包缺少：$relativePath" }
}
Get-ChildItem -LiteralPath $packageDir | Select-Object Name
```

接着在**未安装 Qt/MinGW 的 Windows x64 电脑或干净虚拟机**解压这个文件夹并双击 `qWindmill.exe`。确认窗口能打开，再输入 `1` 发起查询：请求日志应出现 `请求地址：123456789006010300000062`。读取现场网关还需要目标电脑能访问界面中填写的 TCP 服务地址；窗口能启动与网关能连接是两项不同的检查。

本机也可先做启动检查：临时从当前 PowerShell 的 `PATH` 中去掉 `C:\Qt\...`，从交付目录启动程序，观察是否因缺少 DLL 或平台插件而立即退出。发布前仍建议在干净电脑上检查，因为开发机可能从其他目录找到依赖。

## 6. 压缩为 ZIP 并记录校验值

```powershell
if (Test-Path -LiteralPath $zipPath) { throw "ZIP 已存在，请换 releaseId：$zipPath" }
Compress-Archive -LiteralPath $packageDir -DestinationPath $zipPath -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $zipPath)) { throw 'ZIP 创建失败。' }
Get-Item -LiteralPath $zipPath | Select-Object FullName, Length
Get-FileHash -LiteralPath $zipPath -Algorithm SHA256 | Format-List Algorithm, Hash, Path
```

把 ZIP 和对应的 SHA-256 值一起交给使用者。接收方可用 `Get-FileHash -Algorithm SHA256 <zip文件>` 对比传输前后的值。若需要安装器、代码签名或自动更新，那是 ZIP 交付之外的后续工作。

## 本机已完成的验证

2026-09-28 已按上述流程生成并验证了 `dist/qWindmill-0.1.0-gui-fix-20260928-windows-x64/` 和同名 ZIP：

- Release 编译通过；两组 CTest 测试均通过。
- 打包后的 `qWindmill.exe` 的 PE Subsystem 值为 2（Windows GUI）。
- `windeployqt 6.11.2` 已部署 Qt DLL、`platforms/qwindows.dll` 和 MinGW 运行库。
- 暂时移除本机 Qt/MinGW `PATH` 后，交付目录中的 GUI 能独立启动并持续运行 3 秒。
- ZIP 大小 27,172,351 字节；SHA-256：`A6CA1DF812189A2BEC497438328127699860E27CB5BD215C896D905514207815`。

这个启动检查未在未安装 Qt 的另一台机器上进行，现场网关查询也需要目标网络环境。`windeployqt` 本次提示找不到 `dxcompiler.dll`/`dxil.dll`，工具说明仅在使用 Direct3D 12 等相关功能时才有影响；当前 qWindmill 代码是普通 Widgets 与 TCP 客户端，发布包的窗口启动检查已通过。

## 常见问题

| 现象 | 先检查什么 |
| --- | --- |
| `Qt6Widgets.dll was not found` | 是否直接发送了 `build-release/qWindmill.exe`；应发送完整交付目录或 ZIP。 |
| `Could not find the Qt platform plugin "windows"` | `platforms/qwindows.dll` 是否仍在 EXE 同级的 `platforms` 子目录；是否把其他 Qt 版本放进了 `PATH`。 |
| `windeployqt` 找不到 Qt/编译器文件 | 第 1 步的 Qt、MinGW 路径是否正确，且 `PATH` 中优先使用同一套工具链。 |
| 双击 EXE 时出现黑色终端，关闭终端后程序退出 | 这是旧包被编译为 Windows 控制台程序所致；重新运行 `release.ps1`，并使用新 ZIP 中的 `qWindmill.exe`。发布脚本会拒绝控制台类型的 EXE。 |
| 程序启动，但设备读取超时 | 查看“请求与状态”页中的 TCP 服务地址、实际连接 IP、起始寄存器和完整请求报文；再检查现场网络与服务。 |

参考：[Qt 6.11 Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)、[Qt CMake 部署](https://doc.qt.io/qt-6/cmake-deployment.html)。
