@echo off
setlocal
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0migrate_to_clean_english_foundation.ps1" %*
exit /b %errorlevel%
