@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0..\"
pushd "%ROOT%"
REM Locate Visual Studio using vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "!VSWHERE!" (
    echo [!] vswhere.exe not found.
    echo     Install Visual Studio 2022 or add the Visual Studio Installer component.
    goto :nocpp
)

for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
    if not defined VCVARS set "VCVARS=%%i"
)

if not defined VCVARS (
    echo [!] No Visual Studio installation with the MSVC C++ workload found.
    goto :nocpp
)

if not defined VCVARS goto :nocpp

REM (vcvars internally shells out to vswhere and can print a harmless "not found" line - silence it)
call "!VCVARS!" >nul 2>nul
if errorlevel 1 echo [!] failed to init the MSVC environment & exit /b 1

if not exist "bin" mkdir bin
del /f /q bin\autoplay_next.exe 2>nul

cl.exe /EHsc /std:c++20 /O2 /W4 /permissive- ^
    src\autoplay_main.cpp ^
    /Fe:bin\autoplay_next.exe ^
    /link user32.lib gdi32.lib kernel32.lib

echo.
if errorlevel 1 goto build_failed
if not exist bin\autoplay_next.exe goto build_failed

move /y bin\autoplay_next.exe bin\autoplay.exe >nul
if errorlevel 1 (
    echo BUILD SUCCEEDED, BUT bin\autoplay.exe IS OPEN OR LOCKED.
    echo The new build was preserved as bin\autoplay_next.exe.
    goto build_end
)

echo BUILD OK - autoplay.exe updated
goto build_end

:build_failed
echo BUILD FAILED - the previous autoplay.exe was preserved

:build_end
pause
exit /b 0

:nocpp
echo [!] MSVC C++ toolset not found.
echo     Open the Visual Studio Installer and add the "Desktop development with C++" workload.
endlocal
exit /b 1