param([Parameter(Mandatory=$true)][string]$Serial,[switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$FtopTests = Join-Path $PSScriptRoot 'scratch\tests'
New-Item -ItemType Directory -Force -Path $FtopTests | Out-Null
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Tests }
& javac -encoding UTF-8 -d $FtopTests (Join-Path $PSScriptRoot 'src\dev\ftop\widget\Metrics.java') (Join-Path $PSScriptRoot 'src\dev\ftop\widget\ProcessMetrics.java') (Join-Path $PSScriptRoot 'tests\MetricsTest.java')
if ($LASTEXITCODE -ne 0) { throw 'JVM test compilation failed' }
& java -cp $FtopTests dev.ftop.widget.MetricsTest
if ($LASTEXITCODE -ne 0) { throw 'JVM tests failed' }
foreach ($FtopApk in @('ftop-xiaomi.apk','ftop-xiaomi-tests.apk')) {
    & adb -s $Serial install --no-incremental -r (Join-Path $PSScriptRoot ('dist\' + $FtopApk))
    if ($LASTEXITCODE -ne 0) { throw "Install failed: $FtopApk" }
}
$FtopOutput = (& adb -s $Serial shell am instrument -w dev.ftop.widget.tests/dev.ftop.widget.tests.DeviceTests | Out-String)
$FtopOutput | Set-Content -LiteralPath (Join-Path $FtopTests 'device-instrumentation.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or $FtopOutput -notmatch '13 device checks passed') { throw $FtopOutput }
Write-Output $FtopOutput
& adb -s $Serial pull /sdcard/Android/data/dev.ftop.widget/files/device-tests.json (Join-Path $FtopTests ($Serial + '-device-tests.json'))
if ($LASTEXITCODE -ne 0) { throw 'Cannot collect device result' }
& python (Join-Path $PSScriptRoot 'tests\ui_smoke.py') --serial $Serial
if ($LASTEXITCODE -ne 0) { throw 'UI tests failed' }
$FtopProcessOutput = (& adb -s $Serial shell am instrument -w dev.ftop.widget.tests/dev.ftop.widget.tests.ProcessTests | Out-String)
$FtopProcessOutput | Set-Content -LiteralPath (Join-Path $FtopTests 'process-instrumentation.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or $FtopProcessOutput -notmatch 'process checks passed') { throw $FtopProcessOutput }
Write-Output $FtopProcessOutput
& adb -s $Serial pull /sdcard/Android/data/dev.ftop.widget/files/process-tests (Join-Path $FtopTests $Serial)
if ($LASTEXITCODE -ne 0) { throw 'Cannot collect process test evidence' }
