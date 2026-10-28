param([string]$Compiler = 'C:\tools\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$SituationRoot = Split-Path -Parent $PSScriptRoot
$SituationOutput = Join-Path $SituationRoot 'scratch\situation-test'
New-Item -ItemType Directory -Force -Path $SituationOutput | Out-Null
& (Join-Path $SituationRoot 'build.ps1') -Compiler $Compiler -OutputDirectory (Join-Path $SituationOutput 'build')
& $Compiler -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-cast-function-type -static -static-libgcc -static-libstdc++ (Join-Path $PSScriptRoot 'situation.cpp') (Join-Path $SituationRoot 'scratch\build\app-res.o') -o (Join-Path $SituationOutput 'situation-test.exe') -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -ldxgi -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lpdh -lpsapi -liphlpapi -lversion -lwbemuuid -lwintrust
if ($LASTEXITCODE -ne 0) { throw 'Situation test compilation failed' }
$SituationTest = Start-Process -FilePath (Join-Path $SituationOutput 'situation-test.exe') -ArgumentList ('"' + $SituationOutput + '"') -Wait -PassThru -WindowStyle Hidden
Get-Content -LiteralPath (Join-Path $SituationOutput 'result.json')
if ($SituationTest.ExitCode -ne 0) { throw 'Situation test failed' }
