param(
  [string]$Exe = (Join-Path $PSScriptRoot '..\dist\top.exe'),
  [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\scratch\responsive')
)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$configPath = Join-Path $OutputDirectory 'config.json'
$statePath = Join-Path $OutputDirectory 'state.json'
$config = @{width=760;height=360;x=40;y=40;appearance='light';floating=$true;menuBar=$false;motion=$true;processSort='memory';opacity=1}
function Save-Config { [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json)) }
function Invoke-App([string[]]$Arguments) {
  $quoted = @('--config', ('"'+$configPath+'"')) + $Arguments
  $p = Start-Process -FilePath $Exe -ArgumentList $quoted -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(15000) -or $p.ExitCode -ne 0) { throw "App command failed: $Arguments" }
}
function Read-State {
  Invoke-App @('--inspect', ('"'+$statePath+'"'))
  Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ResponsiveWindow {
 public delegate bool EnumProc(IntPtr hwnd, IntPtr arg);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr arg);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int count);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
 public static IntPtr Find(uint pid) {
   IntPtr found=IntPtr.Zero;
   EnumWindows((hwnd,arg)=> { uint p; GetWindowThreadProcessId(hwnd,out p); var text=new System.Text.StringBuilder(256); GetWindowText(hwnd,text,256);
     if(p==pid && text.ToString()=="top") { found=hwnd; return false; } return true; },IntPtr.Zero);
   return found;
 }
}
'@
[ResponsiveWindow]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$instance = $null
try {
  # Layout fixtures only change geometry; the live app below always samples this machine.
  $cases = @()
  $sizes = @(@('standard',760,360),@('compact',560,280),@('tall',360,440),@('small',340,210),@('strip',460,64))
  foreach ($n in @(4,12,32,64)) { foreach ($size in $sizes) {
    $cases += @{n=$n;width=$size[1];height=$size[2]}
  }}
  $inputPath = Join-Path $OutputDirectory 'layouts-input.json'
  $outputPath = Join-Path $OutputDirectory 'layouts.json'
  [IO.File]::WriteAllText($inputPath, ($cases | ConvertTo-Json))
  $p = Start-Process -FilePath $Exe -ArgumentList @('--layout-report',('"'+$inputPath+'"'),('"'+$outputPath+'"')) -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(15000) -or $p.ExitCode -ne 0) { throw 'Layout report failed' }
  $layouts = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
  for ($i=0; $i -lt $cases.Count; $i++) {
    $l=$layouts[$i]; $case=$cases[$i]
    if ($l.tier -ne 'strip' -and @($l.slots).Count -ne $case.n) { throw 'Logical processors missing' }
    foreach ($block in $l.blocks) {
      if ($block[1] -lt 0 -or $block[2] -lt 0 -or $block[1]+$block[3] -gt $case.width+.01 -or $block[2]+$block[4] -gt $case.height+.01) { throw 'Block outside panel' }
    }
  }
  Save-Config
  $instance=Start-Process -FilePath $Exe -ArgumentList @('--config',('"'+$configPath+'"')) -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 4
  $hwnd=[ResponsiveWindow]::Find($instance.Id)
  if ($hwnd -eq [IntPtr]::Zero) { throw 'Native window not found' }
  $results=@()
  foreach ($size in $sizes) {
    $state=Read-State
    $dpi=$state.panel.dpi
    [ResponsiveWindow]::SetWindowPos($hwnd,[IntPtr](-1),50,50,[int]($size[1]*$dpi),[int]($size[2]*$dpi),0x10) | Out-Null
    Start-Sleep -Milliseconds 650
    $state=Read-State
    if ($state.layout.tier -ne 'strip') {
      $coreHits=@($state.panel.hits | Where-Object {$_.key -match '^core:\d+$'})
      if ($coreHits.Count -ne $state.cores.Count) { throw 'Live thread hits missing' }
    }
    if ($state.memoryWatts -ne -1 -or $state.systemWatts -ne -1) { throw 'Unsupported power must be unavailable' }
    foreach ($hit in $state.panel.hits) {
      if ($hit.rect[0] -lt 0 -or $hit.rect[1] -lt 0 -or $hit.rect[2] -gt $size[1]+.1 -or $hit.rect[3] -gt $size[2]+.1) { throw "Hit outside panel: $($size[0]) $($hit.key)" }
    }
    $bmp=Join-Path $OutputDirectory ($size[0]+'.bmp')
    Invoke-App @('--capture',('"'+$bmp+'"'))
    $image=[Drawing.Bitmap]::new($bmp)
    try { $image.Save((Join-Path $OutputDirectory ($size[0]+'.png')),[Drawing.Imaging.ImageFormat]::Png) } finally { $image.Dispose() }
    $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory ($size[0]+'.json')) -Encoding UTF8
    $results+=@{name=$size[0];layout=$state.layout;cores=$state.cores.Count;sensorState=$state.sensorState;diskBusy=$state.diskBusy;cpuWatts=$state.cpuPackageWatts;gpuWatts=$state.gpuWatts}
  }
  # Exercise an actual click on the new system-power region, and check its scope text.
  $state=Read-State
  $hit=$state.panel.hits | Where-Object key -eq 'systemPower' | Select-Object -First 1
  $x=[int](($hit.rect[0]+$hit.rect[2])*.5*$state.panel.dpi)
  $y=[int](($hit.rect[1]+$hit.rect[3])*.5*$state.panel.dpi)
  [ResponsiveWindow]::SendMessage($hwnd,0x202,[IntPtr]::Zero,[IntPtr](($y -shl 16) -bor $x)) | Out-Null
  $detail=Read-State
  if (-not $detail.detailVisible -or $detail.detailMetric -ne 'systemPower') { throw 'Power detail click failed' }
  $detail | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'power-detail.json') -Encoding UTF8
  $config.motion=$false; $config.appearance='dark'; Save-Config
  [ResponsiveWindow]::SetWindowPos($hwnd,[IntPtr](-1),50,50,[int](560*$dpi),[int](280*$dpi),0x10) | Out-Null
  Start-Sleep -Milliseconds 1400
  $a=Read-State; Start-Sleep -Milliseconds 250; $b=Read-State
  if ($a.fanAnimation.angle -ne $b.fanAnimation.angle) { throw 'Motion disabled but fan still moves' }
  Invoke-App @('--capture',('"'+(Join-Path $OutputDirectory 'dark.bmp')+'"'))
  $image=[Drawing.Bitmap]::new((Join-Path $OutputDirectory 'dark.bmp'))
  try { $image.Save((Join-Path $OutputDirectory 'dark.png'),[Drawing.Imaging.ImageFormat]::Png) } finally { $image.Dispose() }
  Invoke-App @('hide'); Start-Sleep -Milliseconds 1200
  $hidden=Read-State
  if ($hidden.samplerDemand -ne 0) { throw 'Hidden panel with no tray must stop sampling' }
  Invoke-App @('show'); Start-Sleep -Milliseconds 1200
  if ((Read-State).samplerDemand -eq 0) { throw 'Sampling did not resume' }
  @{cases=$results;layoutCases=$cases.Count;powerDetail=$true;reducedMotion=$true;hiddenPause=$true;exeSha256=(Get-FileHash $Exe).Hash} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
} catch {
  $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'error.txt') -Encoding UTF8
  exit 1
} finally {
  if ($instance -and -not $instance.HasExited) { Invoke-App @('quit') }
}
