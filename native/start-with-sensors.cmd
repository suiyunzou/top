@echo off
setlocal
set "TOP_SENSOR_EXE=%~dp0top.exe"
if not exist "%TOP_SENSOR_EXE%" set "TOP_SENSOR_EXE=%~dp0dist\top.exe"
powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; $exe=$env:TOP_SENSOR_EXE; if(-not (Test-Path -LiteralPath $exe)) {throw 'top.exe not found'}; Start-Process -FilePath $exe -ArgumentList 'show' -Verb RunAs -WindowStyle Hidden"
endlocal
