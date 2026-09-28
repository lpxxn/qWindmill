# 在当前 PowerShell 进程补充 Qt/MinGW 运行库目录，然后启动已构建的 GUI。
# Qt Creator 使用 Kit 启动时会自动配置这些路径；直接双击 build/qWindmill.exe
# 可能因为缺少 Qt6Widgets.dll 等运行库而无法启动。
$qtBin = 'C:\Qt\6.11.2\mingw_64\bin'
$compilerBin = 'C:\Qt\Tools\mingw1310_64\bin'
$app = Join-Path $PSScriptRoot 'build\qWindmill.exe'

if (-not (Test-Path -LiteralPath $app)) {
    throw "请先构建工程，未找到：$app"
}
$env:PATH = "$qtBin;$compilerBin;$env:PATH"
& $app
