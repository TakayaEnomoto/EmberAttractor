@echo off
powershell.exe -NoProfile -File "%~dp0Tools\Play.ps1"
if errorlevel 1 pause
