@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: cl.exe is not on PATH and vswhere.exe was not found.
    echo Open a Developer Command Prompt for Visual Studio and run this script again.
    exit /b 1
)

set "VSDIR="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR (
    echo error: no Visual Studio installation with the C++ toolset was found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo error: failed to initialize the MSVC environment.
    exit /b 1
)

:build
if not exist build mkdir build
cl /nologo /W4 /O2 /std:c11 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:build\ ^
   /Fe:build\antc.exe ^
   src\main.c src\parse.c src\sim.c src\codegen.c src\util.c
if errorlevel 1 exit /b 1
del /q build\*.obj >nul 2>nul
echo Built build\antc.exe
