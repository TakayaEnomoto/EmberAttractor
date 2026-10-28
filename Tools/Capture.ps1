param(
    [ValidateRange(0,3)][int]$Mode = 3,
    [ValidateRange(1,600)][int]$Seconds = 20,
    [ValidateRange(0,16000000)][int]$GPUCount = 100000,
    [ValidateRange(0,100000)][int]$CPUCount = 500,
    [ValidateRange(0,120)][int]$Warmup = 10,
    [switch]$DynamicBounds,
    [switch]$DebrisOnGPU,
    [switch]$NoTrace,
    [string]$Label = '',
    [switch]$Offscreen
)
. "$PSScriptRoot\Engine.ps1"
$captureRoot = Join-Path $ProjectRoot 'Saved\Profiling'
New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
if (-not $Label) { $Label = "mode${Mode}" }
$traceFile = Join-Path $captureRoot "Ember_${Label}_${stamp}.utrace"
$runLog = Join-Path $captureRoot "Ember_${Label}_${stamp}.log"
$arguments = @(
    $ProjectFile, '/Game/EmberLab/L_EmberLab', '-game', '-windowed',
    '-ResX=1920', '-ResY=1080', '-ForceRes', '-NoVSync', '-NoSplash', '-unattended', '-nosound',
    '-EmberProfile', "-EmberMode=$Mode", "-EmberSeconds=$Seconds", "-EmberWarmup=$Warmup",
    "-EmberGPUCount=$GPUCount", "-EmberCPUCount=$CPUCount",
    '-EmberAutoAttract', '-statnamedevents', '-csvGpuStats', '-csvCategories=Particles',
    '-ExecCmds=stat Niagara,stat GPU,fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=450,30',
    "-abslog=$runLog"
)
# Insights traces are ~300 MB each; sweeps can skip them and keep CSV/stat/log evidence.
if (-not $NoTrace) { $arguments += @('-trace=cpu,gpu,frame,bookmark,counters', "-tracefile=$traceFile") }
if ($DynamicBounds) { $arguments += '-EmberDynamicBounds' }
if ($DebrisOnGPU) { $arguments += '-EmberDebrisOnGPU' }
if ($Offscreen) { $arguments += '-RenderOffscreen' }
$quotedArguments = $arguments | ForEach-Object { '"' + $_ + '"' }
$process = Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $quotedArguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Capture failed with exit code $($process.ExitCode); inspect $runLog" }
if (-not $NoTrace) { Write-Output "Trace: $traceFile" }
Write-Output "Log: $runLog"
