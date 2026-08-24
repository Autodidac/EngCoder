@echo off
setlocal
set "R=%LOCALAPPDATA%\ThreadSpire"
if exist "%R%\state\MODEL_STORAGE.txt" type "%R%\state\MODEL_STORAGE.txt"
echo.
echo Data root: %R%
explorer.exe "%R%"
