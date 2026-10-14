param(
    [ValidateRange(0,3)][int]$Mode = 3,
    [ValidateRange(1,600)][int]$Seconds = 20,
    [switch]$Offscreen
)
. "$PSScriptRoot\Engine.ps1"
$captureRoot = Join-Path $ProjectRoot 'Saved\Profiling'
New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$traceFile = Join-Path $captureRoot "Ember_mode${Mode}_${stamp}.utrace"
$runLog = Join-Path $captureRoot "Ember_mode${Mode}_${stamp}.log"
$arguments = @(
    $ProjectFile, '/Game/EmberLab/L_EmberLab', '-game', '-windowed',
    '-ResX=1920', '-ResY=1080', '-ForceRes', '-NoVSync', '-NoSplash', '-unattended', '-nosound',
    '-EmberProfile', "-EmberMode=$Mode", "-EmberSeconds=$Seconds", '-EmberWarmup=10',
    '-EmberAutoAttract', '-trace=cpu,gpu,frame,bookmark,counters', "-tracefile=$traceFile",
    '-statnamedevents', '-csvGpuStats', '-csvCategories=Particles',
    '-ExecCmds=stat Niagara,stat GPU,fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=450,30',
    "-abslog=$runLog"
)
if ($Offscreen) { $arguments += '-RenderOffscreen' }
$quotedArguments = $arguments | ForEach-Object { '"' + $_ + '"' }
$process = Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $quotedArguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Capture failed with exit code $($process.ExitCode); inspect $runLog" }
Write-Output "Trace: $traceFile"
Write-Output "Log: $runLog"
