@echo off
call "%~dp0tools\APPLY_ENGLISH_FOUNDATION_MIGRATION.cmd"
if errorlevel 1 exit /b %errorlevel%
setlocal EnableExtensions
pushd "%~dp0"

set "BUILD_ARGUMENT="
if /I "%~1"=="clean" set "BUILD_ARGUMENT=clean"

tasklist /FI "IMAGENAME eq ThreadSpire.exe" 2^>nul | find /I "ThreadSpire.exe" ^>nul
if not errorlevel 1 (
    echo ^> Closing the currently running ThreadSpire instance
    taskkill /IM ThreadSpire.exe /T ^>nul 2^>^&1
    for /L %%I in (1,1,20) do (
        tasklist /FI "IMAGENAME eq ThreadSpire.exe" 2^>nul | find /I "ThreadSpire.exe" ^>nul || goto :existing_closed
        ping 127.0.0.1 -n 2 ^>nul
    )
    taskkill /F /IM ThreadSpire.exe /T ^>nul 2^>^&1
)
:existing_closed

call build.cmd %BUILD_ARGUMENT%
if errorlevel 1 goto :failed

set "THREADSPIRE_EXE="
if exist "out\build\windows-vs2026\bin\Release\ThreadSpire.exe" (
    set "THREADSPIRE_EXE=%CD%\out\build\windows-vs2026\bin\Release\ThreadSpire.exe"
)
if not defined THREADSPIRE_EXE if exist "out\build\windows-vs2022\bin\Release\ThreadSpire.exe" (
    set "THREADSPIRE_EXE=%CD%\out\build\windows-vs2022\bin\Release\ThreadSpire.exe"
)

if not defined THREADSPIRE_EXE (
    echo.
    echo Build completed, but ThreadSpire.exe was not found in a supported preset output directory.
    goto :failed
)

echo.
echo ^> Launching %THREADSPIRE_EXE%
start "ThreadSpire" "%THREADSPIRE_EXE%"
popd
exit /b 0

:failed
echo.
echo ThreadSpire was not launched because the build or validation failed.
popd
exit /b 1
