#requires -Version 7.0
<#
.SYNOPSIS
    构建、测试并打包 qWindmill 的 Windows x64 便携 ZIP。
.EXAMPLE
    .\release.ps1
.EXAMPLE
    .\release.ps1 -ReleaseId 0.1.1 -Jobs 8
.NOTES
    默认使用本机 Qt 6.11.2 MinGW 64-bit 工具链。详细步骤见 RELEASE.md。
    此脚本只在 dist/ 下生成本地交付物，不会上传或覆盖已有发布包。
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._-]*$')]
    [string]$ReleaseId = '',

    [ValidateNotNullOrEmpty()]
    [string]$QtDir = 'C:\Qt\6.11.2\mingw_64',

    [ValidateNotNullOrEmpty()]
    [string]$MingwDir = 'C:\Qt\Tools\mingw1310_64',

    [ValidateNotNullOrEmpty()]
    [string]$CmakeExe = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',

    [ValidateNotNullOrEmpty()]
    [string]$NinjaExe = 'C:\Qt\Tools\Ninja\ninja.exe',

    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Exists {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description 不存在：$Path"
    }
}

function Invoke-Checked {
    param([string]$Description, [string]$Executable, [string[]]$Arguments)
    Write-Host "`n[$Description]"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description 失败（退出码 $LASTEXITCODE）。"
    }
}

function Assert-GuiExecutable {
    param([string]$Path)
    # PE 可选头的 Subsystem 字段位于 PE 文件头起始处之后 0x5c 字节。
    # 2 表示 Windows GUI，3 表示 Windows 控制台。发布 GUI 程序时拒绝后者。
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    $subsystem = [BitConverter]::ToUInt16($bytes, $peOffset + 0x5c)
    if ($subsystem -ne 2) {
        throw "发布程序不是 Windows GUI 子系统（实际 Subsystem=$subsystem）：$Path"
    }
}

$projectDir = $PSScriptRoot
$projectFile = Join-Path $projectDir 'CMakeLists.txt'
Assert-Exists $projectFile 'CMake 工程文件'

# 默认发布标识来自 CMake 的版本号，避免脚本里的版本与工程版本长期漂移。
$projectText = Get-Content -LiteralPath $projectFile -Raw
$versionMatch = [regex]::Match($projectText, 'project\(qWindmill\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) {
    throw '无法从 CMakeLists.txt 读取 qWindmill 的 X.Y.Z 版本号。'
}
if ([string]::IsNullOrWhiteSpace($ReleaseId)) {
    $ReleaseId = '{0}-{1}' -f $versionMatch.Groups[1].Value, (Get-Date -Format 'yyyyMMdd-HHmmss')
}

$ctestExe = Join-Path (Split-Path -Parent $CmakeExe) 'ctest.exe'
$compilerExe = Join-Path $MingwDir 'bin\g++.exe'
$deployExe = Join-Path $QtDir 'bin\windeployqt.exe'
$buildDir = Join-Path $projectDir 'build-release'
$releaseDir = Join-Path $projectDir ("dist\qWindmill-{0}-windows-x64" -f $ReleaseId)
$zipPath = "$releaseDir.zip"
$hashPath = "$zipPath.sha256"

foreach ($tool in @(
    @{ Path = $CmakeExe; Name = 'CMake' },
    @{ Path = $ctestExe; Name = 'CTest' },
    @{ Path = $NinjaExe; Name = 'Ninja' },
    @{ Path = $compilerExe; Name = 'MinGW C++ 编译器' },
    @{ Path = $deployExe; Name = 'windeployqt' }
)) {
    Assert-Exists $tool.Path $tool.Name
}
foreach ($output in @($releaseDir, $zipPath, $hashPath)) {
    if (Test-Path -LiteralPath $output) {
        throw "发布输出已存在，未覆盖：$output`n请指定新的 -ReleaseId。"
    }
}

$originalPath = $env:PATH
$originalPluginPath = $env:QT_PLUGIN_PATH
try {
    # Qt、编译器及部署工具必须来自同一套 MinGW Kit。
    $env:PATH = "$(Join-Path $QtDir 'bin');$(Join-Path $MingwDir 'bin');$originalPath"

    # Ninja 是单配置生成器；Release 必须在 CMake 配置阶段指定。
    $configureArgs = @(
        '-S', $projectDir,
        '-B', $buildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
        "-DCMAKE_CXX_COMPILER=$($compilerExe.Replace('\','/'))",
        "-DCMAKE_PREFIX_PATH=$($QtDir.Replace('\','/'))",
        '-DBUILD_TESTING=ON'
    )
    Invoke-Checked '配置 Release' $CmakeExe $configureArgs
    Invoke-Checked '编译 Release' $CmakeExe @('--build', $buildDir, '--parallel', [string]$Jobs)
    Invoke-Checked '运行协议与窗口测试' $ctestExe @('--test-dir', $buildDir, '--output-on-failure')

    $builtExe = Join-Path $buildDir 'qWindmill.exe'
    Assert-Exists $builtExe 'Release 程序'
    Assert-GuiExecutable $builtExe
    New-Item -ItemType Directory -Path $releaseDir | Out-Null
    $releaseExe = Join-Path $releaseDir 'qWindmill.exe'
    Copy-Item -LiteralPath $builtExe -Destination $releaseExe
    Invoke-Checked '收集 Qt/MinGW 运行库' $deployExe @(
        '--release', '--compiler-runtime', '--dir', $releaseDir, $releaseExe
    )

    # 此检查能抓住最常见的漏包：主程序、Qt 核心 DLL、MinGW 运行库和平台插件。
    # 其他插件由 windeployqt 按当前应用依赖自动选择。
    $requiredFiles = @(
        'qWindmill.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
        'Qt6Network.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll',
        'libwinpthread-1.dll', 'platforms\qwindows.dll'
    )
    foreach ($relativePath in $requiredFiles) {
        Assert-Exists (Join-Path $releaseDir $relativePath) "交付文件 $relativePath"
    }

    # 在开发机上暂时移除 Qt/MinGW 的 PATH 后启动打包程序。
    # 这只能验证基本启动，不等于已在另一台干净电脑上验收。
    Write-Host "`n[独立启动检查]"
    $env:PATH = (($originalPath -split ';') | Where-Object {
        $_ -and ($_ -notlike 'C:\Qt\*')
    }) -join ';'
    Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
    $smokeProcess = $null
    try {
        $smokeProcess = Start-Process -FilePath $releaseExe -WorkingDirectory $releaseDir -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 3
        $smokeProcess.Refresh()
        if ($smokeProcess.HasExited) {
            throw "交付程序启动后立即退出（退出码 $($smokeProcess.ExitCode)）。"
        }
        Write-Host '程序在未加入 Qt/MinGW 路径时正常保持运行。'
    }
    finally {
        if ($null -ne $smokeProcess) {
            $smokeProcess.Refresh()
            if (-not $smokeProcess.HasExited) {
                Stop-Process -Id $smokeProcess.Id -Force
            }
        }
        $env:PATH = "$(Join-Path $QtDir 'bin');$(Join-Path $MingwDir 'bin');$originalPath"
    }

    Write-Host "`n[生成 ZIP 与 SHA-256]"
    Compress-Archive -LiteralPath $releaseDir -DestinationPath $zipPath -CompressionLevel Optimal
    Assert-Exists $zipPath 'ZIP 发布包'
    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
    $zipName = Split-Path -Leaf $zipPath
    Set-Content -LiteralPath $hashPath -Value "$hash  $zipName" -Encoding ascii

    Write-Host "`n发布完成"
    Write-Host "目录：$releaseDir"
    Write-Host "ZIP ：$zipPath"
    Write-Host "SHA256：$hash"
    Write-Host "校验文件：$hashPath"
}
finally {
    $env:PATH = $originalPath
    if ($null -eq $originalPluginPath) {
        Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
    }
    else {
        $env:QT_PLUGIN_PATH = $originalPluginPath
    }
}
