@echo off
setlocal EnableExtensions
pushd "%~dp0"

set "BUILD_ARGUMENT="
if /I "%~1"=="clean" set "BUILD_ARGUMENT=clean"

for %%P in (EngCoder.exe ThreadSpire.exe) do (
    tasklist /FI "IMAGENAME eq %%P" 2^>nul | find /I "%%P" ^>nul
    if not errorlevel 1 (
        echo ^> Closing the currently running %%P
        taskkill /F /IM %%P /T ^>nul 2^>^&1
    )
)

call "%~dp0tools\apply-english-foundation-migration.cmd"
if errorlevel 1 goto :failed

call build.cmd %BUILD_ARGUMENT%
if errorlevel 1 goto :failed

set "ENGCODER_EXE="
if exist "out\build\windows-vs2026\bin\Release\EngCoder.exe" (
    set "ENGCODER_EXE=%CD%\out\build\windows-vs2026\bin\Release\EngCoder.exe"
)
if not defined ENGCODER_EXE if exist "out\build\windows-vs2022\bin\Release\EngCoder.exe" (
    set "ENGCODER_EXE=%CD%\out\build\windows-vs2022\bin\Release\EngCoder.exe"
)

if not defined ENGCODER_EXE (
    echo.
    echo Build completed, but EngCoder.exe was not found in a supported preset output directory.
    goto :failed
)

echo.
echo ^> Launching %ENGCODER_EXE%
start "EngCoder" "%ENGCODER_EXE%"
popd
exit /b 0

:failed
echo.
echo EngCoder was not launched because the build or validation failed.
popd
exit /b 1
