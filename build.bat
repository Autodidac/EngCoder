@echo off
setlocal
call "%~dp0auto.bat" -NoTests -NoRun %*
exit /b %ERRORLEVEL%
