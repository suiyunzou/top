# Official Shizuku artifacts, pinned to published 13.1.5 bytes. No arbitrary archive extraction.
$FtopDependencyRoot = Join-Path $PSScriptRoot 'scratch\dependencies'
New-Item -ItemType Directory -Force $FtopDependencyRoot | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$FtopDependencyHashes = @{
    api='4DEF9BDE498EF8626614C2FC5DB9AF4749C86F16F6C33E3F5658D35E70BAB59B'
    provider='B0F18CD9812464EC171C53CAC93A819FE411718A3965C311F01EB4DE265381B3'
    shared='4659642C9339BE0A26E9C65BB8648F7AD6D8F4A465F557993CCBC78802381635'
    aidl='33FE7191CDD69FCB66D649264F3B0C47ACB2F3D6343AFC05B98DBBFF6F221963'
}
$FtopDependencyJars = @()
foreach ($FtopModule in @('api','provider','shared','aidl')) {
    $FtopArchive = Join-Path $FtopDependencyRoot "$FtopModule-13.1.5.aar"
    if (!(Test-Path -LiteralPath $FtopArchive)) {
        Invoke-WebRequest -Uri "https://repo.maven.apache.org/maven2/dev/rikka/shizuku/$FtopModule/13.1.5/$FtopModule-13.1.5.aar" -OutFile $FtopArchive
    }
    if ((Get-FileHash -LiteralPath $FtopArchive -Algorithm SHA256).Hash -ne $FtopDependencyHashes[$FtopModule]) { throw "Dependency hash mismatch: $FtopModule" }
    $FtopJar = Join-Path $FtopDependencyRoot "$FtopModule.jar"
    $FtopArchiveReader = [IO.Compression.ZipFile]::OpenRead($FtopArchive)
    try { [IO.Compression.ZipFileExtensions]::ExtractToFile($FtopArchiveReader.GetEntry('classes.jar'), $FtopJar, $true) }
    finally { $FtopArchiveReader.Dispose() }
    $FtopDependencyJars += $FtopJar
}
