param([string]$Command = 'show')
$FtopDirectory = $PSScriptRoot
$FtopPython = Join-Path $FtopDirectory '.venv-ftop\Scripts\python.exe'
if (!(Test-Path -LiteralPath $FtopPython)) {
    python -m venv (Join-Path $FtopDirectory '.venv-ftop')
    & $FtopPython -m pip install -r (Join-Path $FtopDirectory 'requirements-ftop.txt')
    if ($LASTEXITCODE -ne 0) { throw 'ftop dependencies could not be installed.' }
}
if ($Command -in @('show', 'toggle')) {
    Start-Process -FilePath (Join-Path $FtopDirectory '.venv-ftop\Scripts\pythonw.exe') -ArgumentList @(('"' + (Join-Path $FtopDirectory 'ftop_win.py') + '"'), $Command) -WindowStyle Hidden
} else {
    & $FtopPython (Join-Path $FtopDirectory 'ftop_win.py') $Command
}
