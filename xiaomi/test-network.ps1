param([Parameter(Mandatory=$true)][string]$Serial,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
if (!$Serial.StartsWith('emulator-')) { throw 'UI and permission tests run on an emulator only.' }
$FtopEvidence=Join-Path $PSScriptRoot 'scratch\network-tests'
New-Item -ItemType Directory -Force -Path $FtopEvidence | Out-Null
& javac -encoding UTF-8 -d $FtopEvidence (Join-Path $PSScriptRoot 'src\dev\ftop\widget\NetworkCounters.java') (Join-Path $PSScriptRoot 'tests\NetworkCountersTest.java')
if ($LASTEXITCODE -ne 0) {throw 'Network unit test compilation failed'}
& java -cp $FtopEvidence dev.ftop.widget.NetworkCountersTest
if ($LASTEXITCODE -ne 0) {throw 'Network counter checks failed'}
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Tests }
foreach($name in @('ftop-xiaomi','ftop-xiaomi-tests')) {
    & adb -s $Serial install --no-incremental -r (Join-Path $PSScriptRoot ('dist\'+$name+'.apk'))
    if ($LASTEXITCODE -ne 0) {throw "Cannot install $name"}
}
function Test-Network([bool]$Denied) {
    $FtopArgs=@('-s',$Serial,'shell','am','instrument','-w')
    if ($Denied) {$FtopArgs+=@('-e','denied','true')}
    $FtopArgs+='dev.ftop.widget.tests/dev.ftop.widget.NetworkTests'
    $FtopResult=(& adb @FtopArgs | Out-String)
    $FtopResult | Set-Content -LiteralPath (Join-Path $FtopEvidence ('network-denied-'+$Denied+'.txt')) -Encoding UTF8
    if ($FtopResult -notmatch 'network checks passed') {throw $FtopResult}
    Write-Output $FtopResult
}
& python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state allow
if ($LASTEXITCODE -ne 0) {throw 'Start Shizuku on the emulator first'}
Test-Network $false
try {
    & python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state deny
    if ($LASTEXITCODE -ne 0) {throw 'Cannot revoke test authorization'}
    Test-Network $true
} finally { & python (Join-Path $PSScriptRoot 'tests\shizuku_permission.py') --serial $Serial --state allow }
