@echo off
rem ============================================================================
rem envdoctor one-click launcher.
rem
rem Text in THIS file is ASCII-only on purpose: cmd parses batch text using the
rem active code page, so any non-ASCII here breaks as soon as the console is not
rem cp936 (e.g. under a UTF-8 terminal). The program itself prints Chinese and
rem adapts to the console code page, so nothing user-visible is lost.
rem
rem Flow is goto-based (no parenthesised blocks): "set" inside a block is not
rem visible to %VAR% in the same block without delayed expansion.
rem ============================================================================
setlocal
set "HERE=%~dp0"
title envdoctor

:locate
set "EXE="
if exist "%HERE%bin\envdoctor.exe" set "EXE=%HERE%bin\envdoctor.exe"
if not defined EXE if exist "%HERE%Cover\build\Release\envdoctor.exe" set "EXE=%HERE%Cover\build\Release\envdoctor.exe"
if not defined EXE if exist "%HERE%Cover\build-ci\Release\envdoctor.exe" set "EXE=%HERE%Cover\build-ci\Release\envdoctor.exe"
if not defined EXE goto first_build

:menu
cls
echo ============================================================
echo   envdoctor - Windows dev environment diagnostics
echo ============================================================
echo   program: %EXE%
echo.
echo   [1] Graphical interface
echo   [2] Terminal interface
echo   [3] Full diagnosis (command line, folded by category)
echo   [4] List all checks
echo   [5] Rebuild
echo   [0] Quit
echo.
rem "choice" instead of "set /p": it reads a keypress, so there is no value to parse and no
rem trailing CR to trip over (a CR inside the value would split the next "if" line and cmd
rem would answer "The syntax of the command is incorrect."). When this script runs without a
rem console, choice fails and errorlevel is 255, which the first check turns into a clean
rem exit instead of a spin.
choice /c 123450 /n /m "Choose: "
if errorlevel 6 goto bye
if errorlevel 5 goto build_ui
if errorlevel 4 goto run_list
if errorlevel 3 goto run_cli
if errorlevel 2 goto run_tui
if errorlevel 1 goto run_gui
goto bye

rem ---------------------------------------------------------------------------
rem Switch the console to UTF-8 before launching: the interfaces lay themselves
rem out in UTF-8, and so does the program's own output. Every line from here to
rem the matching restore below stays ASCII (see the note at the top).
rem ---------------------------------------------------------------------------
:run_gui
call :to_utf8
"%EXE%" gui
goto after_run

:run_tui
call :to_utf8
"%EXE%" tui
goto after_run

:run_cli
call :to_utf8
"%EXE%" run
goto after_run

:run_list
call :to_utf8
"%EXE%" --list-checks
goto after_run

:after_run
set "RC=%ERRORLEVEL%"
call :restore_cp
echo.
echo [exit code: %RC%]
pause
goto menu

:bye
endlocal
exit /b 0

:to_utf8
for /f "tokens=1,* delims=:" %%a in ('chcp') do set "CPTMP=%%b"
for /f "tokens=1" %%c in ("%CPTMP%") do set "OLDCP=%%c"
chcp 65001 >nul
exit /b 0

:restore_cp
if defined OLDCP chcp %OLDCP% >nul
exit /b 0

rem ---------------------------------------------------------------------------
rem Build: prefer the cmake on PATH, else the one bundled with Visual Studio
rem (a Build Tools-only install keeps it off PATH).
rem ---------------------------------------------------------------------------
:first_build
cls
echo envdoctor.exe not found (never built here).
echo.
echo   source tree: %HERE%Cover
echo   dependencies (fmt / doctest / ftxui / Dear ImGui / WIL) are fetched by CMake.
echo.
echo   [1] Build now (with graphical and terminal interfaces)
echo   [2] Build now (command line only, faster)
echo   [0] Quit
echo.
choice /c 120 /n /m "Choose: "
if errorlevel 3 goto bye
if errorlevel 2 goto build_cli_only
if errorlevel 1 goto build_ui
goto bye

:build_ui
rem Clear UIFLAG here, NOT in :build: the CLI-only path jumps into :build with it set.
set "UIFLAG="
goto build

:build_cli_only
set "UIFLAG=-DENVDECTOR_WITH_UI=OFF"
goto build

:build
set "CMAKE="
where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE=cmake"
if defined CMAKE goto have_cmake
set "VSCMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%VSCMAKE%" set "CMAKE=%VSCMAKE%"
if not defined CMAKE set "VSCMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE if exist "%VSCMAKE%" set "CMAKE=%VSCMAKE%"

:have_cmake
if not defined CMAKE goto no_cmake
echo.
echo [1/2] configuring...
"%CMAKE%" -S "%HERE%Cover" -B "%HERE%Cover\build" -G "Visual Studio 17 2022" -A x64 %UIFLAG%
if errorlevel 1 goto build_fail
echo.
echo [2/2] building...
"%CMAKE%" --build "%HERE%Cover\build" --config Release
if errorlevel 1 goto build_fail
echo.
echo build finished.
pause
goto locate

:no_cmake
echo.
echo cmake not found. Install Visual Studio 2022 with the "Desktop development
echo with C++" workload, then run this again.
echo   https://visualstudio.microsoft.com/downloads/
pause
goto bye

:build_fail
echo.
echo Build failed. Usual causes: no C++ toolset, dependencies unreachable,
echo or security software blocking the compiler (retry usually helps).
pause
goto bye
