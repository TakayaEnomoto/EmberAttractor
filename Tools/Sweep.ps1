# Scaling sweep and bounds-optimization captures in the lab sandbox (constant workload).
# Each run launches a fresh 1080p game, warms up, captures, and exits; run one at a time.
param(
    [int[]]$GPUCounts = @(100000, 250000, 500000, 1000000, 2000000, 4000000, 6000000, 8000000),
    [int[]]$CPUCounts = @(500, 1000, 2000, 5000, 10000, 20000),
    [int[]]$BoundsCounts = @(5000, 20000),
    [int[]]$PortCounts = @(1000, 5000, 10000, 20000),
    [int]$Seconds = 10,
    [int]$Warmup = 6,
    [switch]$Offscreen
)
$ErrorActionPreference = 'Stop'
$common = @{ Seconds = $Seconds; Warmup = $Warmup; NoTrace = $true; Offscreen = $Offscreen }

& "$PSScriptRoot\Capture.ps1" @common -Mode 0 -Label 'sweep_baseline'
foreach ($count in $GPUCounts) {
    & "$PSScriptRoot\Capture.ps1" @common -Mode 1 -GPUCount $count -Label "sweep_gpu_$count"
}
foreach ($count in $CPUCounts) {
    & "$PSScriptRoot\Capture.ps1" @common -Mode 2 -CPUCount $count -Label "sweep_cpu_$count"
}
# Optimization "before" case: identical CPU workloads with dynamic bounds.
foreach ($count in $BoundsCounts) {
    & "$PSScriptRoot\Capture.ps1" @common -Mode 2 -CPUCount $count -DynamicBounds -Label "bounds_dynamic_$count"
}
# Optimization comparison: the same debris emitter ported to GPU simulation.
foreach ($count in $PortCounts) {
    & "$PSScriptRoot\Capture.ps1" @common -Mode 2 -CPUCount $count -DebrisOnGPU -Label "port_gpu_$count"
}
