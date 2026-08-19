@echo off
rem Builds ed1 with MSVC, which is how it is built on the machine it is meant
rem for. There is no make on that box, and none is needed: seven translation
rem units and one link.
rem
rem   build            builds ed1.exe
rem   build test       builds it, then builds and runs the unit tests
rem   build session    builds it, then drives the editor itself with keystrokes
rem   build check      both
rem
rem Run it from a Developer Command Prompt, or run it from anywhere and let it
rem find vcvars64 itself.
rem
rem The search is pinned to Visual Studio 2022 - the [17.0,18.0) below. A bare
rem "vswhere -latest" reaches past it to a newer Visual Studio if one is
rem installed, which is not the toolset this is built with.
rem
rem _CRT_SECURE_NO_WARNINGS is defined for the same reason cc1's own project
rem defines it: getenv and strerror are standard C++17, and MSVC's objection to
rem them is house policy rather than a defect to go and fix.
setlocal

if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 goto :fail

if not exist src\obj mkdir src\obj

cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:ed1.exe /Fo:src\obj\ ^
   src\main.cpp src\editor.cpp src\buffer.cpp src\compile.cpp ^
   src\indent.cpp src\menu.cpp src\tree.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp src\demangle_win.cpp ^
   src\terminal_common.cpp ^
   src\terminal_win.cpp
if errorlevel 1 goto :fail

if "%1"=="test" goto :unit
if "%1"=="check" goto :unit
if "%1"=="session" goto :session
goto :done

:unit

cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /I src /Fe:test.exe /Fo:src\obj\ ^
   tests\test.cpp src\compile.cpp src\indent.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\buffer.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp
if errorlevel 1 goto :fail
test.exe
if errorlevel 1 goto :fail
if not "%1"=="check" goto :done

:session
cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:session.exe /Fo:src\obj\ tests\session.cpp
if errorlevel 1 goto :fail
session.exe ed1.exe %CC1%
if errorlevel 1 goto :fail

:done
echo built ed1.exe
exit /b 0

:findvcvars
rem vswhere's answer goes through a file rather than a for/f. A for/f with a
rem quoted program AND quoted arguments loses a quote pair to cmd's own parsing,
rem and the version range here has both.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo could not find vswhere.exe & exit /b 1
"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -property installationPath > "%TEMP%\ed1-vspath.txt"
set VSPATH=
set /p VSPATH=<"%TEMP%\ed1-vspath.txt"
del "%TEMP%\ed1-vspath.txt"
if "%VSPATH%"=="" echo could not find Visual Studio 2022 & exit /b 1
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" echo no vcvars64 under %VSPATH% & exit /b 1
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
