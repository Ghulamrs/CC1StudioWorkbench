@echo off
rem Builds WinConsole with MSVC, which is how it is built on the machine it is
rem meant for. There is no make on that box, and none is needed: a couple of
rem dozen translation units and one link.
rem
rem RStudio.exe is this project's console editor on Windows - the same source as
rem ed1 on Linux and macOS, over the Windows half of the terminal, and named for
rem the machine it runs on so that the three variants can be told apart where
rem they are installed. See "The three variants" in the README.
rem
rem   build            builds RStudio.exe
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

if not exist obj mkdir obj

cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:RStudio.exe /Fo:obj\ ^
   src\main.cpp src\editor.cpp src\buffer.cpp src\compile.cpp ^
   src\indent.cpp src\menu.cpp src\tree.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp src\demangle_win.cpp ^
   src\path.cpp src\process.cpp src\debugger.cpp src\settings.cpp src\about.cpp src\help.cpp ^
   src\shalimar\channel.cpp src\shalimar\session.cpp ^
   src\terminal_common.cpp ^
   src\terminal_win.cpp
if errorlevel 1 goto :fail

if "%1"=="solution" goto :solution
if "%1"=="product" goto :product
if "%1"=="test" goto :unit
if "%1"=="check" goto :unit
if "%1"=="session" goto :session
goto :done

:solution
rem The three programs as one solution: cc1, shc, and this editor's console
rem half, with the editor depending on both compilers so a change to one and
rem the change to the editor that goes with it are a single build.
rem
rem Run from here rather than by calling msbuild directly, because msbuild is
rem on PATH only after vcvars64.bat - which the top of this file has already
rem found. One place knows where Visual Studio is.
rem
rem RStudio.sln reaches ..\Compiler-C and ..\Compiler-S, so all three have to
rem be checked out beside each other on this machine.
msbuild RStudio.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /m
if errorlevel 1 goto :fail
echo built the solution
exit /b 0

:product
rem The product, as against the build: one directory holding what you would
rem actually run, away from the project space it was compiled in. Both Windows
rem variants land here side by side - the console one and the window - and
rem is copied when msbuild has made it and passed over when it has not.
set PRODUCT=%USERPROFILE%\cc1-studio
if not exist "%PRODUCT%\bin" mkdir "%PRODUCT%\bin"
if not exist "%PRODUCT%\examples" mkdir "%PRODUCT%\examples"
copy /y RStudio.exe "%PRODUCT%\bin\" >nul
if exist winforms\x64\Release\RStudioGui.exe copy /y winforms\x64\Release\RStudioGui.exe "%PRODUCT%\bin\" >nul
copy /y README.md "%PRODUCT%\" >nul
copy /y examples\*.c "%PRODUCT%\examples\" >nul
copy /y examples\*.cpp "%PRODUCT%\examples\" >nul
echo RStudio is in %PRODUCT%
goto :done


:unit

cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /I src /I winforms /Fe:test.exe /Fo:obj\ ^
   tests\test.cpp src\compile.cpp src\indent.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\buffer.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp ^
   src\demangle_win.cpp src\path.cpp src\process.cpp src\debugger.cpp ^
   src\settings.cpp src\about.cpp src\help.cpp ^
   src\shalimar\channel.cpp src\shalimar\session.cpp ^
   winforms\bridge.cpp
if errorlevel 1 goto :fail
test.exe
if errorlevel 1 goto :fail
if not "%1"=="check" goto :done

:session
rem Its own object directory. src\shalimar\session.cpp and tests\session.cpp
rem both become session.obj under one /Fo, and the two builds would take it in
rem turns to overwrite each other's - which works, right up until the day
rem something links both.
if not exist obj\harness mkdir obj\harness
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /I src /Fe:session.exe /Fo:obj\harness\ tests\session.cpp src\path.cpp
if errorlevel 1 goto :fail
session.exe RStudio.exe %CC1%
if errorlevel 1 goto :fail

:done
echo built RStudio.exe
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
