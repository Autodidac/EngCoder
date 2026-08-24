@echo off
setlocal
choice /C YN /N /M "Quarantine current internal weights and reset to clean English Foundation? [Y/N] "
if errorlevel 2 exit /b 0
call "%~dp0tools\APPLY_ENGLISH_FOUNDATION_MIGRATION.cmd" -Force
exit /b %errorlevel%
