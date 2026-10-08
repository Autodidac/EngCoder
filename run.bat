@echo off
setlocal
call "%~dp0auto.bat" -NoTests %*
exit /b %ERRORLEVEL%
