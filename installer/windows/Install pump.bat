@echo off
rem pump: install (the old pump plugins go to a backup, these go in). Asks for administrator rights once.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\pump-install.ps1" -From "%~dp0."
