. "$PSScriptRoot\Engine.ps1"
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile -run=BuildEmberLab -unattended -nosplash -nop4 -stdout -FullStdOutLogOutput
if ($LASTEXITCODE -ne 0) { throw "Asset generation failed with exit code $LASTEXITCODE" }
