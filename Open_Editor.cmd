@echo off
powershell.exe -NoProfile -File "%~dp0Tools\OpenEditor.ps1"
if errorlevel 1 pause
