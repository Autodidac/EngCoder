@echo off
setlocal
set "ROOT=%~dp0"
if exist "%ROOT%out\build" rmdir /s /q "%ROOT%out\build"
if errorlevel 1 (
  echo Failed to remove "%ROOT%out\build". Close running EngCoder/build processes and retry.
  exit /b 1
)
echo Cleaned generated EngCoder build output.
exit /b 0
