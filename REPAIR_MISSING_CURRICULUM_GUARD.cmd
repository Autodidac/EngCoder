@echo off
setlocal EnableExtensions

set "SOURCE=%~dp0resources\curriculum_guard.py"
set "DESTINATION=%LOCALAPPDATA%\ThreadSpire\backend\curriculum_guard.py"

if not exist "%SOURCE%" (
    echo Repair failed: "%SOURCE%" was not found.
    exit /b 1
)

if not exist "%LOCALAPPDATA%\ThreadSpire\backend" (
    mkdir "%LOCALAPPDATA%\ThreadSpire\backend" >nul 2>&1
    if errorlevel 1 (
        echo Repair failed: unable to create the backend directory.
        exit /b 1
    )
)

copy /Y "%SOURCE%" "%DESTINATION%" >nul
if errorlevel 1 (
    echo Repair failed: unable to write "%DESTINATION%".
    exit /b 1
)

echo Repaired: "%DESTINATION%"
echo ThreadSpire can retry backend verification now. Restart the app if the current retry does not recover.
exit /b 0
