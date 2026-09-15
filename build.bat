@echo off
rem Builds TrueCrimeDualSense.asi (32-bit) and the ds_test.exe controller check.
rem Needs Visual Studio (any edition) with the "Desktop development with C++" workload.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio with C++ tools & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /EHsc /MT /W3 /LD /Fo:build\ src\tcla_dualsense.cpp /Fe:build\TrueCrimeDualSense.dll /link hid.lib setupapi.lib xinput.lib user32.lib gdi32.lib winmm.lib || exit /b 1
copy /y build\TrueCrimeDualSense.dll build\TrueCrimeDualSense.asi >nul
cl /nologo /O2 /EHsc /MT /W3 /Isrc /Fo:build\ tools\ds_test.cpp /Fe:build\ds_test.exe /link hid.lib setupapi.lib || exit /b 1
echo.
echo BUILD OK: build\TrueCrimeDualSense.asi
