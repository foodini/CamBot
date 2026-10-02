@echo off
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe at "%VSWHERE%" - is Visual Studio installed? > "%~dp0build_log.txt"
    type "%~dp0build_log.txt"
    exit /b 1
)

set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo Could not locate MSBuild.exe via vswhere. > "%~dp0build_log.txt"
    type "%~dp0build_log.txt"
    exit /b 1
)

echo Using MSBuild: %MSBUILD%
echo Using MSBuild: %MSBUILD% > "%~dp0build_log.txt"
echo. >> "%~dp0build_log.txt"

"%MSBUILD%" "%~dp0CamBot.sln" /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /nologo >> "%~dp0build_log.txt" 2>&1

echo.
echo ===== Build finished. Full output in build_log.txt =====
echo.
findstr /C:"error" /C:"Error" "%~dp0build_log.txt"
echo.
echo (full log: %~dp0build_log.txt)
endlocal
