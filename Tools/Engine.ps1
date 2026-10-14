$ErrorActionPreference = 'Stop'
$script:ProjectRoot = Split-Path $PSScriptRoot -Parent
$script:ProjectFile = Join-Path $ProjectRoot 'EmberAttractor.uproject'

function Find-Unreal57 {
    if ($env:UE57_ROOT -and (Test-Path (Join-Path $env:UE57_ROOT 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
        return $env:UE57_ROOT
    }
    $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $manifest) {
        $installation = (Get-Content $manifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object AppName -eq 'UE_5.7' | Select-Object -First 1
        if ($installation -and (Test-Path (Join-Path $installation.InstallLocation 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
            return $installation.InstallLocation
        }
    }
    $registry = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.7' -ErrorAction SilentlyContinue
    if ($registry -and $registry.InstalledDirectory) { return $registry.InstalledDirectory }
    throw 'Unreal Engine 5.7 was not found. Install it or set UE57_ROOT to its installation folder.'
}

$script:EngineRoot = Find-Unreal57
