@echo off
setlocal
call "%~dp0auto.bat" -NoRun %*
exit /b %ERRORLEVEL%
