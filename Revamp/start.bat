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
echo.

rem ---------- 1. 系统 Python ----------
set "PY_EXE="
where py >nul 2>nul && set "PY_EXE=py"
if not defined PY_EXE where python >nul 2>nul && set "PY_EXE=python"
if not defined PY_EXE (
    echo [X] 未找到系统 Python，请先安装 Python 3.10 以上版本并加入 PATH。
    goto fail
)
echo [OK] 系统 Python: %PY_EXE%

rem ---------- 2. 虚拟环境 ----------
if exist "%VENV_PY%" (
    echo [OK] 虚拟环境 .venv 已存在
    goto check_deps
)
echo [?] 未找到虚拟环境 .venv，是否现在创建并安装依赖？需要联网。
set "ANS="
set /p ANS=创建? [Y/n]: 
if /i "%ANS%"=="n" goto fail
%PY_EXE% -m venv .venv
if errorlevel 1 (
    echo [X] 创建虚拟环境失败。
    goto fail
)
"%VENV_PY%" -m pip install -U pip
"%VENV_PY%" -m pip install -e "app[gui,tui]"
if errorlevel 1 (
    echo [X] 依赖安装失败，请检查网络后重试。
    goto fail
)
echo [OK] 虚拟环境已就绪

:check_deps
rem ---------- 3. 依赖完整性 ----------
"%VENV_PY%" -c "import typer, textual" >nul 2>nul
if errorlevel 1 goto deps_missing
"%VENV_PY%" -c "import PySide6" >nul 2>nul
if errorlevel 1 goto deps_missing
echo [OK] Python 依赖完整
goto check_core

:deps_missing
echo [?] Python 依赖不完整，是否补齐？需要联网。
set "ANS="
set /p ANS=安装? [Y/n]: 
if /i "%ANS%"=="n" goto fail
"%VENV_PY%" -m pip install -e "app[gui,tui]"
if errorlevel 1 (
    echo [X] 依赖安装失败。
    goto fail
)
echo [OK] 依赖已补齐

:check_core
rem ---------- 4. Rust 核心 ----------
if exist "%DLL%" (
    echo [OK] Rust 核心 DLL 已存在
    goto menu
)
echo [?] 未找到核心 DLL（core\target\release），是否现在构建？需要 cargo。
set "ANS="
set /p ANS=构建? [Y/n]: 
if /i "%ANS%"=="n" goto fail
where cargo >nul 2>nul
if errorlevel 1 (
    echo [X] 未找到 cargo。请先安装 Rust 工具链。
    goto fail
)
pushd core
cargo build --release
if errorlevel 1 (
    popd
    echo [X] 构建失败。
    goto fail
)
popd
echo [OK] 核心构建完成

:menu
rem ---------- 5. 选择界面 ----------
echo.
echo 请选择界面:
echo   [1] GUI  - PySide6 窗口（可展开收缩树）
echo   [2] TUI  - Textual 终端界面（可展开收缩树）
echo   [3] CLI  - 命令行报告
set "CHOICE=3"
set /p CHOICE=选择 [1/2/3, 回车默认 3]: 
if "%CHOICE%"=="1" goto run_gui
if "%CHOICE%"=="2" goto run_tui
goto run_cli

:run_gui
"%VENV_PY%" -c "from envdoctor.gui import main; main()"
goto end

:run_tui
"%VENV_PY%" -m envdoctor.tui
goto end

:run_cli
"%VENV_PY%" -m envdoctor run -E %*
goto end

:fail
echo.
echo 启动中止。
pause
exit /b 1

:end
echo.
pause
exit /b 0

:loading
rem 5s fake-loading: one phrase per line, filled-block progress bar (unrolled)
setlocal
set "PHRASES=Spelunking......;Moving bricks......;Enbugging......;Caveman Debugging......;Breading crumbs......;Rubber Duck Debugging......;Bribing the hamster......;Whatchamacalliting......;Flibbertigibbrting......;Deep Sleeping....."
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
