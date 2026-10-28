param([Parameter(Mandatory=$true)][string]$Serial,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
if (!$Serial.StartsWith('emulator-')) { throw 'This control suite only runs on an emulator. It stops dev.ftop.fixture.' }
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Tests }
$FtopEvidence=Join-Path $PSScriptRoot ('scratch\tests\'+$Serial)
New-Item -ItemType Directory -Force -Path $FtopEvidence | Out-Null
foreach ($FtopName in @('ftop-xiaomi','ftop-xiaomi-tests','ftop-fixture')) {
    & adb -s $Serial install --no-incremental -r (Join-Path $PSScriptRoot ('dist\'+$FtopName+'.apk'))
    if ($LASTEXITCODE -ne 0) { throw "Cannot install $FtopName" }
}
& adb -s $Serial shell am start -n dev.ftop.fixture/.FixtureActivity
& adb -s $Serial push (Join-Path $PSScriptRoot 'dist\ftop-xiaomi.apk') /data/local/tmp/ftop-probe-main.apk
& adb -s $Serial push (Join-Path $PSScriptRoot 'dist\ftop-xiaomi-tests.apk') /data/local/tmp/ftop-probe-tests.apk
$FtopProbe=(& adb -s $Serial shell 'CLASSPATH=/data/local/tmp/ftop-probe-main.apk:/data/local/tmp/ftop-probe-tests.apk app_process /system/bin dev.ftop.widget.ControlProbe' | Out-String)
$FtopProbe | Set-Content -LiteralPath (Join-Path $FtopEvidence 'control-probe.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or $FtopProbe -notmatch '19 control checks passed') {throw $FtopProbe}
Write-Output $FtopProbe
# Use the manager's actual grant. A plain pm revoke does not reliably revoke Shizuku's cached authorization.
& python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state allow
if ($LASTEXITCODE -ne 0) {throw 'Start Shizuku on the emulator first'}
$FtopUi=(& adb -s $Serial shell am instrument -w dev.ftop.widget.tests/dev.ftop.widget.FeatureTests | Out-String)
$FtopUi | Set-Content -LiteralPath (Join-Path $FtopEvidence 'feature-instrumentation.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or $FtopUi -notmatch 'feature checks passed') {throw $FtopUi}
Write-Output $FtopUi
& adb -s $Serial pull /sdcard/Android/data/dev.ftop.widget/files/feature-tests $FtopEvidence
try {
    & python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state deny
    if ($LASTEXITCODE -ne 0) {throw 'Cannot revoke test authorization'}
    $FtopPermissions=(& adb -s $Serial shell am instrument -w dev.ftop.widget.tests/dev.ftop.widget.PermissionTests | Out-String)
    $FtopPermissions | Set-Content -LiteralPath (Join-Path $FtopEvidence 'permission-tests.txt') -Encoding UTF8
    if ($FtopPermissions -notmatch '3 permission checks passed') {throw $FtopPermissions}
    Write-Output $FtopPermissions
} finally { & python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state allow }
