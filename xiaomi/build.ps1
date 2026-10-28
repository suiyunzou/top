param(
    [string]$Sdk = 'C:\tools\AndroidSDK',
    [string]$Jdk = 'C:\tools\jdk17',
    [switch]$Tests
)
$ErrorActionPreference = 'Stop'
$FtopRoot = $PSScriptRoot
. (Join-Path $FtopRoot 'dependencies.ps1')
$FtopBuild = Join-Path $FtopRoot ('scratch\build-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$FtopDist = Join-Path $FtopRoot 'dist'
$FtopTools = Join-Path $Sdk 'build-tools\36.0.0'
$FtopAndroid = Join-Path $Sdk 'platforms\android-35\android.jar'
$FtopJava = Join-Path $Jdk 'bin\java.exe'
$FtopKey = Join-Path $FtopRoot '.local\ftop-local.p12'
foreach ($FtopDir in @($FtopBuild,$FtopDist,(Split-Path -Parent $FtopKey))) { New-Item -ItemType Directory -Force -Path $FtopDir | Out-Null }
function Invoke-FtopTool([string]$Exe,[string[]]$ArgsList) {
    & $Exe @ArgsList
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Exe" }
}
if (!(Test-Path -LiteralPath $FtopKey)) {
    # Local sideload identity, never put the private signing key into release archives.
    Invoke-FtopTool (Join-Path $Jdk 'bin\keytool.exe') @('-genkeypair','-keystore',$FtopKey,'-storepass','ftop-local','-keypass','ftop-local','-alias','ftop','-dname','CN=ftop Local Development','-keyalg','RSA','-keysize','2048','-validity','10000','-storetype','PKCS12')
}
function Build-FtopApk([string]$Name,[string]$Manifest,[string[]]$SourceRoots,[bool]$IncludeRes,[string]$ExtraClasspath) {
    $FtopWork = Join-Path $FtopBuild $Name
    New-Item -ItemType Directory -Force -Path (Join-Path $FtopWork 'gen'),(Join-Path $FtopWork 'classes'),(Join-Path $FtopWork 'dex') | Out-Null
    $FtopLinkArgs = @('link','-o',(Join-Path $FtopWork 'unsigned.apk'),'--manifest',$Manifest,'-I',$FtopAndroid,'--java',(Join-Path $FtopWork 'gen'),'--auto-add-overlay')
    if ($IncludeRes) {
        Invoke-FtopTool (Join-Path $FtopTools 'aapt2.exe') @('compile','--dir',(Join-Path $FtopRoot 'res'),'-o',(Join-Path $FtopWork 'resources.zip'))
        $FtopLinkArgs += (Join-Path $FtopWork 'resources.zip')
    }
    Invoke-FtopTool (Join-Path $FtopTools 'aapt2.exe') $FtopLinkArgs
    $FtopSources = @($SourceRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Filter '*.java' -Recurse -File })
    $FtopSources += @(Get-ChildItem -LiteralPath (Join-Path $FtopWork 'gen') -Filter '*.java' -Recurse -File)
    $FtopArgFile = Join-Path $FtopWork 'sources.txt'
    $FtopSources | ForEach-Object { '"' + $_.FullName.Replace('\','/') + '"' } | Set-Content -LiteralPath $FtopArgFile -Encoding UTF8
    # javac argument files must not have a UTF-8 BOM (Windows PowerShell adds it).
    [IO.File]::WriteAllText($FtopArgFile, [IO.File]::ReadAllText($FtopArgFile).TrimStart([char]0xFEFF), [Text.UTF8Encoding]::new($false))
    $FtopClasspath = $FtopAndroid + ';' + ($FtopDependencyJars -join ';')
    if ($ExtraClasspath) { $FtopClasspath += ';' + $ExtraClasspath }
    Invoke-FtopTool (Join-Path $Jdk 'bin\javac.exe') @('-encoding','UTF-8','--release','8','-classpath',$FtopClasspath,'-d',(Join-Path $FtopWork 'classes'),('@'+$FtopArgFile))
    Invoke-FtopTool (Join-Path $Jdk 'bin\jar.exe') @('cf',(Join-Path $FtopWork 'classes.jar'),'-C',(Join-Path $FtopWork 'classes'),'.')
    $FtopDexArgs = @('-cp',(Join-Path $FtopTools 'lib\d8.jar'),'com.android.tools.r8.D8','--release','--min-api','31','--lib',$FtopAndroid,'--output',(Join-Path $FtopWork 'dex'),(Join-Path $FtopWork 'classes.jar'))
    if ($Name -eq 'ftop-xiaomi') { $FtopDexArgs += $FtopDependencyJars }
    Invoke-FtopTool $FtopJava $FtopDexArgs
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $FtopZip = [IO.Compression.ZipFile]::Open((Join-Path $FtopWork 'unsigned.apk'),[IO.Compression.ZipArchiveMode]::Update)
    try { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($FtopZip,(Join-Path $FtopWork 'dex\classes.dex'),'classes.dex',[IO.Compression.CompressionLevel]::Optimal) | Out-Null } finally { $FtopZip.Dispose() }
    Invoke-FtopTool (Join-Path $FtopTools 'zipalign.exe') @('-f','4',(Join-Path $FtopWork 'unsigned.apk'),(Join-Path $FtopWork 'aligned.apk'))
    Invoke-FtopTool $FtopJava @('-jar',(Join-Path $FtopTools 'lib\apksigner.jar'),'sign','--ks',$FtopKey,'--ks-pass','pass:ftop-local','--out',(Join-Path $FtopDist ($Name+'.apk')),(Join-Path $FtopWork 'aligned.apk'))
    Invoke-FtopTool $FtopJava @('-jar',(Join-Path $FtopTools 'lib\apksigner.jar'),'verify','--verbose',(Join-Path $FtopDist ($Name+'.apk')))
}
Build-FtopApk 'ftop-xiaomi' (Join-Path $FtopRoot 'AndroidManifest.xml') @((Join-Path $FtopRoot 'src')) $true ''
if ($Tests) {
    Build-FtopApk 'ftop-xiaomi-tests' (Join-Path $FtopRoot 'tests\AndroidManifest.xml') @((Join-Path $FtopRoot 'tests\android')) $false (Join-Path $FtopBuild 'ftop-xiaomi\classes')
    Build-FtopApk 'ftop-fixture' (Join-Path $FtopRoot 'tests\fixture\AndroidManifest.xml') @((Join-Path $FtopRoot 'tests\fixture\src')) $false ''
}
Get-ChildItem -LiteralPath $FtopDist -Filter '*.apk' | Select-Object Name,Length
