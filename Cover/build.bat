@echo off
rem Build the C++ implementation (Cover). Requires Visual Studio 2022 Build Tools.
rem Usage: build.bat [test]   -- with "test", run the test binary afterwards.
rem
rem Note: goto-flow instead of parenthesised blocks on purpose -- "set" inside a
rem block is not visible to "%VAR%" in the same block (needs delayed expansion),
rem and "%ProgramFiles(x86)%" inside a block can break parsing.
setlocal
set "HERE=%~dp0"
set "CMAKE="

where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE=cmake"
if defined CMAKE goto have_cmake

set "VS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
set "VS_CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%VS_CMAKE%" goto no_cmake
set "CMAKE=%VS_CMAKE%"

:have_cmake
echo [i] cmake: %CMAKE%
"%CMAKE%" -S "%HERE%." -B "%HERE%build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail
"%CMAKE%" --build "%HERE%build" --config Release
if errorlevel 1 goto fail

if /i not "%~1"=="test" exit /b 0
"%HERE%build\Release\envdoctor_tests.exe"
if errorlevel 1 goto fail
exit /b 0

:no_cmake
echo [X] cmake not found. Install Visual Studio 2022 Build Tools.
exit /b 1

:fail
echo [X] build failed.
exit /b 1
