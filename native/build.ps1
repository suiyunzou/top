param([string]$Compiler = 'C:\tools\mingw64\bin\g++.exe', [string]$OutputDirectory = '', [switch]$SkipSelfTest)
$ErrorActionPreference = 'Stop'
$FtopBuildDir = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $PSScriptRoot 'dist' }
$FtopObjectDir = Join-Path $PSScriptRoot 'scratch\build'
New-Item -ItemType Directory -Force -Path $FtopObjectDir | Out-Null
New-Item -ItemType Directory -Force -Path $FtopBuildDir | Out-Null
$FtopCompilerDir = Split-Path -Parent $Compiler
$FtopInstaller = Join-Path $PSScriptRoot 'vendor\pawnio\PawnIO_setup.exe'
if ((Get-FileHash -LiteralPath $FtopInstaller -Algorithm SHA256).Hash -ne '1F519A22E47187F70A1379A48CA604981C4FCF694F4E65B734AAA74A9FBA3032' -or
    (Get-AuthenticodeSignature -LiteralPath $FtopInstaller).Status -ne 'Valid') {
    throw 'Bundled official PawnIO 2.2.0 installer hash/signature verification failed.'
}
if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'vendor\pawnio\IntelMSR.bin') -Algorithm SHA256).Hash -ne 'D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F') {
    throw 'Bundled signed IntelMSR module hash verification failed.'
}
if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'vendor\pawnio\IntelMCHBAR.bin') -Algorithm SHA256).Hash -ne '3F82B832D99B4AAC37D2A20FDB7C9BAA2A3BC0488612C9019C9484EB0E8A6EAE') {
    throw 'Bundled signed IntelMCHBAR module hash verification failed.'
}
& (Join-Path $FtopCompilerDir 'windres.exe') (Join-Path $PSScriptRoot 'src\app.rc') -O coff -o (Join-Path $FtopObjectDir 'app-res.o')
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
& $Compiler -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-cast-function-type -static -static-libgcc -static-libstdc++ -municode -mwindows -s (Join-Path $PSScriptRoot 'src\main.cpp') (Join-Path $FtopObjectDir 'app-res.o') -o (Join-Path $FtopBuildDir 'top.exe') -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lpdh -lpsapi -liphlpapi -lversion -lwbemuuid -lwintrust -ltdh
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed' }
if (!$SkipSelfTest) {
    $FtopTest = Start-Process -FilePath (Join-Path $FtopBuildDir 'top.exe') -ArgumentList '--self-test' -Wait -PassThru -WindowStyle Hidden
    if ($FtopTest.ExitCode -ne 0) { throw 'Native self-test failed' }
    Write-Output 'Built top.exe. Native logic checks passed.'
} else {
    Write-Output 'Built top.exe. Self-test deferred.'
}
