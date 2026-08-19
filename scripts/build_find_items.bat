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
del /f /q bin\find_items_next.exe 2>nul

cl.exe /EHsc /std:c++20 /O2 /W4 /permissive- ^
    src\find_items_main.cpp ^
    /Fe:bin\find_items_next.exe ^
    /link user32.lib kernel32.lib

echo.
if errorlevel 1 goto build_failed
if not exist bin\find_items_next.exe goto build_failed
move /y bin\find_items_next.exe bin\find_items.exe >nul
if errorlevel 1 goto build_locked
echo BUILD OK - bin\find_items.exe updated
goto build_end

:build_locked
echo BUILD SUCCEEDED, BUT bin\find_items.exe IS OPEN OR LOCKED.
echo The new build was preserved as bin\find_items_next.exe.
goto build_end

:build_failed
echo BUILD FAILED - the previous find_items.exe was preserved

:build_end
pause
