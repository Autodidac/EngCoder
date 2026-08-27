@echo off
setlocal
choice /C YN /N /M "Quarantine current internal weights and reset to clean English Foundation? [Y/N] "
if errorlevel 2 exit /b 0
call "%~dp0..\apply-english-foundation-migration.cmd" -Force
exit /b %errorlevel%
