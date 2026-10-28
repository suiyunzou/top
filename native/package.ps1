param(
    [string]$AppPath = (Join-Path $PSScriptRoot 'dist\top.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'scratch\package')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $AppPath -PathType Leaf)) { throw 'Build the application first.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$FtopName = 'top-windows-x64'
$FtopRelease = Join-Path $OutputDirectory $FtopName
if (Test-Path -LiteralPath $FtopRelease) { throw 'Staging exists. Choose a new OutputDirectory.' }
New-Item -ItemType Directory -Path $FtopRelease | Out-Null
Copy-Item -LiteralPath $AppPath -Destination (Join-Path $FtopRelease 'top.exe')
foreach ($FtopFile in @('README.md','TELEMETRY.md','start-with-sensors.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $FtopFile) -Destination $FtopRelease
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $FtopRelease -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vendor\pawnio\COPYING') -Destination (Join-Path $FtopRelease 'licenses\PawnIO-Modules-LGPL.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vendor\pawnio\README.md') -Destination (Join-Path $FtopRelease 'licenses\PawnIO-NOTICE.md')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$FtopStream = [IO.File]::Open((Join-Path $FtopRelease 'source.zip'), [IO.FileMode]::CreateNew)
$FtopArchive = [IO.Compression.ZipArchive]::new($FtopStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($FtopPart in @('src','vendor','assets','licenses','tests','build.ps1','package.ps1','start-with-sensors.cmd','README.md','TELEMETRY.md','.clang-format')) {
        $FtopSource = Join-Path $PSScriptRoot $FtopPart
        if (-not (Test-Path -LiteralPath $FtopSource)) { continue }
        $FtopFiles = if (Test-Path -LiteralPath $FtopSource -PathType Container) {
            Get-ChildItem -LiteralPath $FtopSource -Recurse -File | Where-Object { $_.FullName -notmatch '[\\/](__pycache__|scratch)[\\/]' }
        } else { Get-Item -LiteralPath $FtopSource }
        foreach ($FtopFile in $FtopFiles) {
            $FtopRelative = $FtopFile.FullName.Substring($PSScriptRoot.Length + 1).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($FtopArchive, $FtopFile.FullName, $FtopRelative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
} finally { $FtopArchive.Dispose(); $FtopStream.Dispose() }
$FtopManifest = foreach ($FtopFile in (Get-ChildItem -LiteralPath $FtopRelease -Recurse -File | Sort-Object FullName)) {
    $FtopHash = (Get-FileHash -LiteralPath $FtopFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $FtopRelative = $FtopFile.FullName.Substring($FtopRelease.Length + 1).Replace('\','/')
    "$FtopHash  $FtopRelative"
}
$FtopManifest | Set-Content -LiteralPath (Join-Path $FtopRelease 'SHA256SUMS.txt') -Encoding ascii
$FtopZip = Join-Path $OutputDirectory ($FtopName + '.zip')
Compress-Archive -LiteralPath $FtopRelease -DestinationPath $FtopZip
Get-Item -LiteralPath $FtopZip | Select-Object FullName,Length
