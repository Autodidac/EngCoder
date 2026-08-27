@echo off
call "%~dp0tools\apply-english-foundation-migration.cmd"
if errorlevel 1 exit /b %errorlevel%
setlocal EnableExtensions
pushd "%~dp0"

set "CLEAN=0"
if /I "%~1"=="clean" set "CLEAN=1"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PRESET=windows-vs2022"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do (
        if "%%I"=="2026" set "PRESET=windows-vs2026"
        if "%%I"=="2022" set "PRESET=windows-vs2022"
    )
)

if "%CLEAN%"=="1" (
    echo ^> Removing generated output
    if exist out rmdir /s /q out
)

echo ^> Configuring %PRESET%
cmake --preset %PRESET%
if errorlevel 1 goto :failed

echo ^> Building Release
cmake --build --preset %PRESET%-release --parallel
if errorlevel 1 goto :failed

echo ^> Running tests
ctest --preset %PRESET%-release
if errorlevel 1 goto :failed

echo.
echo Built: %CD%\out\build\%PRESET%\bin\Release\EngCoder.exe
popd
exit /b 0

:failed
echo.
echo Build failed.
popd
exit /b 1
