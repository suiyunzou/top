param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist-ftop'))
$ErrorActionPreference = 'Stop'
$FtopPython = Join-Path $PSScriptRoot '.venv-ftop\Scripts\python.exe'
if (!(Test-Path -LiteralPath $FtopPython)) {
    python -m venv (Join-Path $PSScriptRoot '.venv-ftop')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create build environment.' }
}
& $FtopPython -m pip install -r (Join-Path $PSScriptRoot 'requirements-ftop-build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Cannot install build dependencies.' }
& $FtopPython (Join-Path $PSScriptRoot 'packaging\make-icon.py')
if ($LASTEXITCODE -ne 0) { throw 'Cannot generate icon.' }
$FtopOriginalPath = $env:PATH
try {
    # Third-party toolchains can put incompatible system-named DLLs on PATH.
    # Let Qt hooks supply Qt paths; resolve Windows ICU and API sets from Windows.
    $env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    & $FtopPython -m PyInstaller --clean --noconfirm --distpath $OutputDirectory --workpath (Join-Path $PSScriptRoot 'scratch\build-ftop') (Join-Path $PSScriptRoot 'packaging\ftop.spec')
    if ($LASTEXITCODE -ne 0) { throw 'Executable build failed.' }
} finally { $env:PATH = $FtopOriginalPath }
& $FtopPython (Join-Path $PSScriptRoot 'packaging\package-release.py') $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Cannot assemble release archives.' }
Write-Output "Built EXEs in $OutputDirectory"
