. "$PSScriptRoot\Engine.ps1"
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" EmberAttractorEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
