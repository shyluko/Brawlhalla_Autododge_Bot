@echo off
echo Building release build of Brawlhalla Autododge...
where g++ >nul 2>&1
if errorlevel 1 (
    echo g++ was not found on PATH.
    echo Add your MinGW-w64 bin directory to PATH, then rerun this script.
    pause
    exit /b 1
)
where objcopy >nul 2>&1
if errorlevel 1 (
    echo objcopy was not found on PATH.
    echo Add the MinGW bin directory containing objcopy to PATH, then rerun this script.
    pause
    exit /b 1
)
for /f "delims=" %%I in ('where g++') do set "GXX=%%I"
for /f "delims=" %%I in ('where objcopy') do set "OBJCOPY=%%I"
set "IMGUI=third_party\imgui-1.92.9b\imgui-1.92.9b"
if not exist "bin" mkdir bin
"%GXX%" -O2 -g -DNDEBUG src\main.cpp src\logger.cpp src\gui.cpp "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" "%IMGUI%\imgui_demo.cpp" "%IMGUI%\backends\imgui_impl_win32.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" -I. -Isrc -Ithird_party -I"%IMGUI%" -I"%IMGUI%\backends" -o bin\brawlhalla_autododge.exe -std=c++17 -lws2_32 -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -lshell32 -lwinmm
if %errorlevel% == 0 (
    echo Creating debug-symbol output for release packaging...
    "%OBJCOPY%" --only-keep-debug "bin\brawlhalla_autododge.exe" "bin\brawlhalla_autododge.exe.debug"
    "%OBJCOPY%" --add-gnu-debuglink="bin\brawlhalla_autododge.exe.debug" "bin\brawlhalla_autododge.exe"
    echo Release build successful!
    echo Starting program...
    bin\brawlhalla_autododge.exe
) else (
    echo Build failed!
    pause
)
