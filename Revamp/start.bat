@echo off
setlocal
cd /d "%~dp0"

title 环境诊断工具 Revamp 启动器

set "VENV_PY=.venv\Scripts\python.exe"
set "DLL=core\target\release\envdoctor_core.dll"

echo ================================================
echo   环境诊断工具 Revamp 启动器
echo ================================================
echo.
call :loading
rem 5s fake-loading: one phrase per line, filled-block bar (fully unrolled)
echo Spelunking...... [██□□□□□□□□□□□□□□□□□□] 10%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Moving bricks...... [████□□□□□□□□□□□□□□□□] 20%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Enbugging...... [██████□□□□□□□□□□□□□□] 30%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Caveman Debugging...... [████████□□□□□□□□□□□□] 40%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Breading crumbs...... [██████████□□□□□□□□□□] 50%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Rubber Duck Debugging...... [████████████□□□□□□□□] 60%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Bribing the hamster...... [██████████████□□□□□□] 70%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Whatchamacalliting...... [████████████████□□□□] 80%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Flibbertigibbrting...... [██████████████████□□] 90%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Deep Sleeping..... [████████████████████] 100%%
ping -n 1 -w 450 192.0.2.1 >nul
echo Loaded. [████████████████████] 100%%
endlocal
exit /b 0