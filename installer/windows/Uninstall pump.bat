@echo off
rem pump: remove every pump plugin. Asks for administrator rights once.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\pump-install.ps1" -From "%~dp0." -Uninstall
