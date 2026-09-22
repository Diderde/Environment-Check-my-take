import sys
import os
import platform
import subprocess
from datetime import datetime
import time
import importlib.util
import sysconfig
import gc
import glob
import socket
import urllib.request
import urllib.error
import ssl
import locale
import tempfile
import shutil
import threading
import traceback
import json
import re

COMMAND_CACHE = {}
COMMAND_CACHE_LOCK = threading.Lock()

# 【质量缺陷】编码尝试顺序错误：中文Windows下locale(cp936)最先尝试，部分UTF-8字节序列在GBK下也能"成功"解码成乱码；应先严格UTF-8、失败再回退locale/GBK
def decode_output(data):
    if isinstance(data, str):
        return data
    encodings = []
    try:
        pref = locale.getpreferredencoding(False)
        if pref:
            encodings.append(pref)
    except Exception:
        pass
    encodings.extend(["utf-8", "gbk", "cp936"])
    tried = set()
    for enc in encodings:
        if not enc or enc.lower() in tried:
            continue
        tried.add(enc.lower())
        try:
            return data.decode(enc)
        except Exception:
            pass
    return data.decode("utf-8", errors="replace")

def run_command(cmd, shell=False, timeout=5, use_cache=False):
    cache_key = None
    if use_cache:
        if isinstance(cmd, list):
            cache_key = ("list", tuple(cmd), shell, timeout)
        else:
            cache_key = ("str", cmd, shell, timeout)
        with COMMAND_CACHE_LOCK:
            if cache_key in COMMAND_CACHE:
                return COMMAND_CACHE[cache_key]

    try:
        result = subprocess.check_output(
            cmd, shell=shell, stderr=subprocess.STDOUT, timeout=timeout
        )
        output = decode_output(result).strip()
        ret = (True, output)
    except subprocess.TimeoutExpired:
        partial = ""
        try:
            if getattr(sys.exc_info()[1], "output", None):
                partial = decode_output(sys.exc_info()[1].output).strip()
        except Exception:
            partial = ""
        ret = (False, f"命令执行超时{('，部分输出: ' + partial[:200]) if partial else ''}")
    except subprocess.CalledProcessError as e:
        output = ""
        try:
            output = decode_output(e.output).strip() if e.output else str(e)
        except Exception:
            output = str(e)
        ret = (False, output if output else "命令执行失败")
    except FileNotFoundError:
        ret = (False, "命令不存在")
    except Exception as e:
        ret = (False, f"{type(e).__name__}: {str(e)}")

    if use_cache and cache_key is not None:
        with COMMAND_CACHE_LOCK:
            COMMAND_CACHE[cache_key] = ret
    return ret

def safe_execute(func, default_return="检测失败"):
    try:
        return func()
    except Exception as e:
        error_msg = f"{default_return}: {type(e).__name__}: {str(e)[:200]}"
        print(f"[DEBUG] {error_msg}")
        return [error_msg] if isinstance(default_return, list) else error_msg

def cleanup_temp_files(file_list):
    for f in file_list:
        try:
            if os.path.exists(f):
                os.remove(f)
        except Exception:
            pass

def get_memory_info():
    try:
        if platform.system() == "Windows":
            # 【质量缺陷】wmic自Win11 24H2起默认移除，该类系统上此检查必然失败；应改用ctypes GlobalMemoryStatusEx或Get-CimInstance
            success, output = run_command("wmic OS get TotalVisibleMemorySize,FreePhysicalMemory /value", shell=True, timeout=3, use_cache=True)
            if success:
                lines = [l for l in output.split('\n') if '=' in l]
                info = {}
                for line in lines:
                    key, value = line.split('=', 1)
                    info[key.strip()] = int(value.strip()) if value.strip().isdigit() else 0
                total_gb = info.get('TotalVisibleMemorySize', 0) / 1024 / 1024
                free_gb = info.get('FreePhysicalMemory', 0) / 1024 / 1024
                return f"总内存: {total_gb:.2f}GB, 可用: {free_gb:.2f}GB"
        else:
            success, output = run_command("free -h", shell=True, timeout=2, use_cache=True)
            if success and len(output.split('\n')) > 1:
                return output.split('\n')[1]
    except Exception:
        pass
    return "无法获取内存信息"

def get_cpu_info():
    results = []
    try:
        if platform.system() == "Windows":
            # 【质量缺陷】wmic在新版Windows上已默认移除（见get_memory_info注释），CPU检测随之失败
            success, name = run_command("wmic cpu get Name", shell=True, timeout=3, use_cache=True)
            if success:
                names = [l.strip() for l in name.split('\n') if l.strip() and 'Name' not in l]
                for n in names:
                    results.append(f"CPU型号: {n}")
            success, cores = run_command("wmic cpu get NumberOfCores,NumberOfLogicalProcessors", shell=True, timeout=3, use_cache=True)
            if success:
                lines = [l.strip() for l in cores.split('\n') if l.strip() and 'NumberOf' not in l]
                for i, line in enumerate(lines):
                    parts = line.split()
                    if len(parts) == 2:
                        results.append(f"CPU{i+1}物理核心: {parts[0]}, 逻辑核心: {parts[1]}")
        else:
            success, output = run_command("lscpu", shell=True, timeout=3, use_cache=True)
            if success:
                for line in output.split('\n'):
                    if any(k in line for k in ['Model name', 'Architecture', 'CPU(s)', 'Thread']):
                        results.append(line.strip())
    except Exception as e:
        results.append(f"获取CPU信息失败: {type(e).__name__}")
    return results if results else ["无法获取CPU信息"]

def get_gpu_info():
    results = []
    # 【质量缺陷】超时仅3秒：首次调用需初始化驱动常需3-8秒；未走use_cache每次刷新重跑；未装NVIDIA卡或驱动时报"❌不可用"
    success, output = run_command("nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader", shell=True, timeout=3)
    if success:
        for line in output.split('\n'):
            if line.strip():
                results.append(f"✅ NVIDIA GPU: {line.strip()}")
    else:
        results.append("❌ nvidia-smi 不可用或无 NVIDIA 显卡")
    
    try:
        if platform.system() == "Windows":
            # 【质量缺陷】wmic缺失系统上显卡检测必然失败
            success, output = run_command("wmic path win32_VideoController get Name", shell=True, timeout=3, use_cache=True)
            if success:
                gpus = [l.strip() for l in output.split('\n') if l.strip() and 'Name' not in l]
                for g in gpus:
                    results.append(f"系统显卡: {g}")
        else:
            success, output = run_command("lspci | grep -i vga", shell=True, timeout=3, use_cache=True)
            if success:
                for line in output.split('\n'):
                    if 'VGA' in line or '3D' in line:
                        results.append(f"系统显卡: {line.split(':')[-1].strip()}")
    except Exception:
        pass
    return results

def get_battery_status():
    results = []
    try:
        if platform.system() == "Windows":
            # 【质量缺陷】wmic缺失系统上电池检测必然失败
            success_est, est_out = run_command("wmic path win32_battery get EstimatedChargeRemaining /value", shell=True, timeout=3, use_cache=True)
            success_sta, sta_out = run_command("wmic path win32_battery get BatteryStatus /value", shell=True, timeout=3, use_cache=True)
            
            est_val = "未知"
            sta_val = 0
            
            if success_est:
                for line in est_out.split('\n'):
                    if '=' in line:
                        val = line.split('=')[1].strip()
                        if val.isdigit():
                            est_val = val
                            
            if success_sta:
                for line in sta_out.split('\n'):
                    if '=' in line:
                        val = line.split('=')[1].strip()
                        if val.isdigit():
                            sta_val = int(val)
                            
            if est_val != "未知":
                status_map = {1: "放电中", 2: "交流电充电", 3: "已充满", 4: "温度过高", 5: "故障", 11: "部分充电"}
                results.append(f"电池 - 电量: {est_val}%, 状态: {status_map.get(sta_val, '未知')}")
            else:
                results.append("未检测到电池")
        else:
            success, output = run_command("upower -i $(upower -e | grep BAT) | grep -E 'state|percentage'", shell=True, timeout=3, use_cache=True)
            if success:
                results.extend([l.strip() for l in output.split('\n') if l.strip()])
            else:
                bat_path = "/sys/class/power_supply/BAT0"
                if not os.path.exists(bat_path):
                    bat_path = "/sys/class/power_supply/BAT1"
                if os.path.exists(bat_path):
                    cap_file = os.path.join(bat_path, "capacity")
                    stat_file = os.path.join(bat_path, "status")
                    cap = "未知"
                    stat = "未知"
                    if os.path.exists(cap_file):
                        with open(cap_file, 'r') as f:
                            cap = f.read().strip() + "%"
                    if os.path.exists(stat_file):
                        with open(stat_file, 'r') as f:
                            stat = f.read().strip()
                    if cap != "未知" or stat != "未知":
                        results.append(f"电池 - 电量: {cap}, 状态: {stat}")
                    else:
                        results.append("未检测到电池")
                else:
                    results.append("未检测到电池")
    except Exception as e:
        results.append(f"电池检测失败: {type(e).__name__}")
    return results

def get_system_uptime():
    try:
        if platform.system() == "Windows":
            # 【质量缺陷】wmic缺失系统上开机时长必然无法获取
            success, output = run_command("wmic os get LastBootUpTime", shell=True, timeout=3, use_cache=True)
            if success:
                lines = [l.strip() for l in output.split('\n') if l.strip() and 'LastBoot' not in l]
                if lines:
                    boot_str = lines[0].split('.')[0]
                    boot_time = datetime.strptime(boot_str, "%Y%m%d%H%M%S")
                    uptime = datetime.now() - boot_time
                    return f"系统已运行: {uptime.days}天 {uptime.seconds//3600}小时"
        else:
            success, output = run_command("uptime -p", shell=True, timeout=3, use_cache=True)
            if success:
                return output.strip()
            success, output = run_command("cat /proc/uptime", shell=True, timeout=3, use_cache=True)
            if success:
                secs = float(output.split()[0])
                return f"系统已运行: {int(secs//86400)}天 {int((secs%86400)//3600)}小时"
    except Exception as e:
        return f"无法获取运行时间: {type(e).__name__}"
    return "无法获取运行时间"

def get_python_max_memory():
    try:
        import resource
        soft, hard = resource.getrlimit(resource.RLIMIT_AS)
        if soft == -1:
            return "无限制"
        return f"{soft / (1024 ** 3):.2f}GB"
    except Exception as e:
        if platform.system() == "Windows":
            return "无法检测（Windows不支持resource）"
        return f"无法检测: {type(e).__name__}: {str(e)[:80]}"

def test_ssl_certificates():
    try:
        context = ssl.create_default_context()
        cert_paths = ssl.get_default_verify_paths()
        results = [
            f"CA证书文件: {cert_paths.cafile or '未设置'}",
            f"CA证书目录: {cert_paths.capath or '未设置'}"
        ]
        try:
            # 【质量缺陷】响应未用with关闭，靠GC回收socket
            urllib.request.urlopen('https://pypi.org', timeout=3, context=context)
            results.append("HTTPS连接测试: ✅ 正常")
        except Exception as e:
            results.append(f"HTTPS连接测试: ❌ 失败 ({type(e).__name__}: {str(e)[:80]})")
        return results
    except Exception as e:
        return [f"SSL检测失败: {type(e).__name__}: {str(e)[:80]}"]

def get_environment_variables():
    important_vars = ['PYTHONPATH', 'PYTHONHOME', 'VIRTUAL_ENV', 'CONDA_DEFAULT_ENV', 'CONDA_PREFIX', 'PIP_INDEX_URL', 'HTTP_PROXY', 'HTTPS_PROXY', 'NO_PROXY']
    results = []
    for var in important_vars:
        value = os.environ.get(var)
        if value:
            display_value = value if len(value) < 120 else value[:117] + "..."
            results.append(f"{var} = {display_value}")
    if not results:
        results.append("未设置特殊Python环境变量")
    return results

def get_site_packages_info():
    try:
        import site
        user_site = site.getusersitepackages()
        site_packages = site.getsitepackages()
        results = [
            f"用户site-packages: {user_site} ({'✅' if os.path.exists(user_site) else '❌'})",
            "全局site-packages:"
        ]
        for sp in site_packages:
            exists = "✅" if os.path.exists(sp) else "❌"
            try:
                file_count = len([f for f in os.listdir(sp) if not f.startswith('.')])
                results.append(f"  {sp} ({exists}, {file_count}个项目)")
            except Exception:
                results.append(f"  {sp} ({exists}, 无法统计)")
        return results
    except Exception as e:
        return [f"无法获取site-packages信息: {type(e).__name__}: {str(e)[:80]}"]

def get_pip_cache_stats():
    try:
        success, cache_dir = run_command([sys.executable, '-m', 'pip', 'cache', 'dir'], timeout=3, use_cache=True)
        if success and os.path.exists(cache_dir):
            total_size, file_count = 0, 0
            for root, dirs, files in os.walk(cache_dir):
                for f in files:
                    try:
                        total_size += os.path.getsize(os.path.join(root, f))
                        file_count += 1
                    except Exception:
                        pass
            return f"缓存文件数: {file_count}, 总大小: {total_size / (1024 ** 2):.2f}MB"
        return f"缓存目录不存在或不可达: {cache_dir}"
    except Exception as e:
        return f"无法统计缓存: {type(e).__name__}: {str(e)[:80]}"

def get_python_compile_info():
    try:
        results = [
            f"编译器: {platform.python_compiler()}",
            f"构建日期: {platform.python_build()[1]}",
            f"实现: {platform.python_implementation()}"
        ]
        config_vars = sysconfig.get_config_vars()
        if 'CC' in config_vars:
            results.append(f"C编译器: {config_vars['CC']}")
        if 'SIZEOF_VOID_P' in config_vars:
            results.append(f"位数: {config_vars['SIZEOF_VOID_P'] * 8}位")
        return results
    except Exception as e:
        return [f"编译信息获取失败: {type(e).__name__}: {str(e)[:80]}"]

# 【质量缺陷】issues列表初始化后从未写入、恒为空：GUI的"🔴严重问题"分支永远不可达（死分支）
def check_common_issues():
    issues, warnings = [], []
    if platform.system() == "Windows":
        success, paths = run_command("where python", shell=True, timeout=2, use_cache=True)
    else:
        success, paths = run_command("which -a python python3", shell=True, timeout=2, use_cache=True)
    if success:
        python_list = [p for p in paths.split('\n') if p.strip()]
        if len(python_list) > 3:
            warnings.append(f"⚠️ 检测到{len(python_list)}个Python，可能导致混淆")

    success, pip_ver = run_command([sys.executable, '-m', 'pip', '--version'], timeout=3, use_cache=True)
    if success:
        m = re.search(r'pip (\d+)\.', pip_ver)
        if m and int(m.group(1)) < 23:
            ver_str = pip_ver.split()[1] if len(pip_ver.split()) > 1 else ""
            warnings.append(f"⚠️ pip版本较旧 ({ver_str})，建议升级到最新版")

    py_version = sys.version_info
    EOL_MINOR = 8
    SOON_EOL_MINOR = 9
    if py_version.major == 3:
        if py_version.minor <= EOL_MINOR:
            warnings.append(f"⚠️ Python {py_version.major}.{py_version.minor} 已停止支持 (EOL)")
        elif py_version.minor <= SOON_EOL_MINOR:
            warnings.append(f"⚠️ Python {py_version.major}.{py_version.minor} 即将停止支持")

    if sys.prefix == getattr(sys, "base_prefix", sys.prefix) and platform.system() != "Windows":
        if os.access(sys.prefix, os.W_OK):
            warnings.append("⚠️ 正在使用系统Python，建议使用虚拟环境")

    return issues, warnings

# 【质量缺陷】①重复调用命中sys.modules缓存，耗时数字失真（显示"已缓存"）；②join超时后导入线程仍在后台运行并污染sys.modules；③find_spec对带点名称（如mysql.connector）会先导入父包产生副作用
def check_module_import_speed(module_name, timeout=8):
    result = {"done": False, "success": False, "message": "未知错误"}
    def worker():
        try:
            if importlib.util.find_spec(module_name) is None:
                result.update({"done": True, "success": False, "message": "未安装"})
                return
            already_loaded = module_name in sys.modules
            start = time.time()
            __import__(module_name)
            elapsed = (time.time() - start) * 1000
            suffix = " (已缓存)" if already_loaded else ""
            result.update({"done": True, "success": True, "message": f"{elapsed:.2f}ms{suffix}"})
        except Exception as e:
            result.update({"done": True, "success": False, "message": f"导入错误: {type(e).__name__}"})
    t = threading.Thread(target=worker, daemon=True)
    t.start()
    t.join(timeout)
    if not result["done"]:
        return False, "导入超时"
    return result["success"], result["message"]

def get_pip_mirror_config():
    success, output = run_command([sys.executable, '-m', 'pip', 'config', 'list'], timeout=3, use_cache=True)
    if success and output:
        mirrors = [line for line in output.split('\n') if 'index-url' in line.lower()]
        if mirrors:
            return mirrors[0]
    return "使用默认源"

def get_python_startup_time():
    try:
        start = time.time()
        subprocess.run([sys.executable, '-c', 'pass'], capture_output=True, timeout=3)
        return f"{(time.time() - start) * 1000:.2f}ms"
    except Exception as e:
        return f"测试失败: {type(e).__name__}"

def get_disk_space(path):
    try:
        total, used, free = shutil.disk_usage(path)
        return f"总:{total // 2**30}GB 已用:{used // 2**30}GB({(used/total)*100:.1f}%) 可用:{free // 2**30}GB"
    except Exception as e:
        return f"无法获取: {type(e).__name__}"

def check_venv_config():
    results = []
    pyvenv_cfg = os.path.join(sys.prefix, 'pyvenv.cfg')
    if os.path.exists(pyvenv_cfg):
        results.append("✅ pyvenv.cfg存在")
        try:
            with open(pyvenv_cfg, 'r', encoding='utf-8', errors='replace') as f:
                results.extend([f"  {l.strip()}" for l in f.readlines()])
        except Exception:
            pass
    else:
        results.append("❌ pyvenv.cfg不存在")
    return results

def check_pip_config():
    results = []
    locs = []
    if platform.system() == "Windows":
        locs = [os.path.expanduser(r"~\pip\pip.ini"), os.path.expanduser(r"~\AppData\Roaming\pip\pip.ini"), r"C:\ProgramData\pip\pip.ini"]
    else:
        locs = [os.path.expanduser("~/.pip/pip.conf"), os.path.expanduser("~/.config/pip/pip.conf"), "/etc/pip.conf"]
    
    found = False
    for loc in locs:
        if os.path.exists(loc):
            found = True
            results.append(f"✅ 找到配置: {loc}")
            try:
                # 【质量缺陷】固定UTF-8读取：pip.ini常为GBK/ANSI编码，中文注释会显示为乱码
                with open(loc, 'r', encoding='utf-8', errors='replace') as f:
                    results.extend([f"  {l.strip()}" for l in f.readlines()])
            except Exception:
                pass
    if not found:
        results.append("❌ 未找到pip配置文件")
    return results

def check_dev_tools():
    results = []
    success, git_ver = run_command(['git', '--version'], timeout=3, use_cache=True)
    if success:
        results.append(f"✅ Git: {git_ver}")
        success, git_user = run_command("git config user.name", shell=True, timeout=2, use_cache=True)
        success_email, git_email = run_command("git config user.email", shell=True, timeout=2, use_cache=True)
        results.append(f"  Git用户: {git_user if success else '未配置'} ({git_email if success_email else '无邮箱'})")
    else:
        results.append("❌ Git 未安装")
    
    success, docker_ver = run_command(['docker', '--version'], timeout=3, use_cache=True)
    if success:
        results.append(f"✅ Docker: {docker_ver}")
        success, docker_ps = run_command("docker ps -q", shell=True, timeout=3)
        success_img, docker_img = run_command("docker images -q", shell=True, timeout=3)
        run_c = len(docker_ps.split('\n')) if success and docker_ps.strip() else 0
        img_c = len(docker_img.split('\n')) if success_img and docker_img.strip() else 0
        results.append(f"  运行中容器: {run_c}, 本地镜像: {img_c}")
    else:
        results.append("❌ Docker 未安装或未启动")

    vscode_config = os.path.expanduser("~/.vscode") if platform.system() != "Windows" else os.path.expanduser(r"~\AppData\Roaming\Code")
    results.append(f"{'✅' if os.path.exists(vscode_config) else '❌'} VSCode 配置目录")

    pycharm_dirs = glob.glob(os.path.expanduser("~/.PyCharm*")) if platform.system() != "Windows" else glob.glob(os.path.expanduser(r"~\AppData\Roaming\JetBrains\PyCharm*"))
    if pycharm_dirs:
        results.append("✅ 检测到 PyCharm 配置目录")
    else:
        results.append("❌ PyCharm 配置目录不存在")
    return results

def check_other_runtimes():
    results = []
    runtimes = [
        ("Node.js", "node -v"), ("npm", "npm -v"), 
        ("Java", "java -version"), ("Go", "go version"), 
        ("Rust", "rustc --version"), ("GCC", "gcc --version"), 
        ("G++", "g++ --version"), ("Make", "make --version")
    ]
    for name, cmd in runtimes:
        success, output = run_command(cmd, shell=True, timeout=3, use_cache=True)
        if success:
            first_line = output.split('\n')[0].strip()
            results.append(f"✅ {name}: {first_line}")
        else:
            results.append(f"❌ {name}: 未安装")
    return results

def check_ssh_keys():
    results = []
    ssh_dir = os.path.expanduser("~/.ssh")
    if not os.path.exists(ssh_dir):
        return ["❌ ~/.ssh 目录不存在"]
    
    results.append(f"✅ SSH目录存在: {ssh_dir}")
    keys = glob.glob(os.path.join(ssh_dir, "*.pub"))
    if keys:
        results.append("公钥列表:")
        for k in keys:
            try:
                mtime = datetime.fromtimestamp(os.path.getmtime(k)).strftime('%Y-%m-%d')
                results.append(f"  {os.path.basename(k)} (创建于: {mtime})")
            except Exception:
                results.append(f"  {os.path.basename(k)}")
    else:
        results.append("❌ 未找到 .pub 公钥文件")
    
    if os.path.exists(os.path.join(ssh_dir, "known_hosts")):
        results.append("✅ known_hosts 文件存在")
    else:
        results.append("❌ known_hosts 文件不存在")
    return results

def check_package_managers():
    results = []
    for name, cmd in [("Conda", "conda --version"), ("Poetry", "poetry --version"), ("Pipenv", "pipenv --version")]:
        success, output = run_command(cmd, shell=True, timeout=3, use_cache=True)
        if success:
            results.append(f"✅ {name}: {output.split('\n')[0]}")
        else:
            results.append(f"❌ {name}: 未安装")
    return results

def check_permissions():
    results = []
    python_dir = sys.prefix
    results.append(f"Python目录写入权限: {'✅ 可写' if os.access(python_dir, os.W_OK) else '❌ 不可写'}")
    
    try:
        import site
        sp = site.getsitepackages()[0]
        results.append(f"site-packages写入权限: {'✅ 可写' if os.access(sp, os.W_OK) else '❌ 不可写'}")
    except Exception as e:
        results.append(f"site-packages写入权限: 无法检测 ({type(e).__name__})")

    if platform.system() == "Windows":
        try:
            import ctypes
            is_admin = ctypes.windll.shell32.IsUserAnAdmin()
            results.append(f"管理员权限: {'✅ 是' if is_admin else '❌ 否'}")
        except Exception:
            results.append("管理员权限: 无法检测")
    else:
        try:
            is_root = os.geteuid() == 0
            results.append(f"Root权限: {'✅ 是' if is_root else '❌ 否'}")
        except Exception:
            results.append("Root权限: 无法检测")
    return results

def test_disk_io_performance():
    temp_file = None
    try:
        test_dir = tempfile.gettempdir()
        temp_file = os.path.join(test_dir, f'.py_diag_io_{os.getpid()}_{int(time.time()*1000)}')
        data = b'x' * (1024 * 1024)
        
        start = time.time()
        with open(temp_file, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        write_time = (time.time() - start) * 1000
        
        # 【质量缺陷】1MB写完立刻读回，几乎全命中页缓存，读取耗时无参考价值
        start = time.time()
        with open(temp_file, 'rb') as f:
            f.read()
        read_time = (time.time() - start) * 1000
        
        return f"临时目录 1MB 读写 -> 写入: {write_time:.2f}ms, 读取: {read_time:.2f}ms"
    except Exception as e:
        return f"测试失败: {type(e).__name__}"
    finally:
        if temp_file: cleanup_temp_files([temp_file])

def check_temp_space():
    try:
        temp_dir = tempfile.gettempdir()
        total, used, free = shutil.disk_usage(temp_dir)
        return f"临时目录: {temp_dir}, 可用: {free / (1024**3):.2f}GB"
    except Exception as e:
        return f"无法检测: {type(e).__name__}"

def detect_orphan_packages():
    try:
        import importlib.metadata
        installed = {dist.metadata["Name"].lower().replace('_', '-') for dist in importlib.metadata.distributions()}
        required = set()
        for dist in importlib.metadata.distributions():
            reqs = dist.requires
            if reqs:
                for r in reqs:
                    match = re.match(r'^([A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?)', r.strip())
                    if match:
                        req_name = match.group(1).lower().replace('_', '-')
                        required.add(req_name)
        # 【质量缺陷】用户主动pip install的顶层包天然"无依赖声明"，必然大量上榜，易被误读为"无用包"
        orphans = [pkg for pkg in installed if pkg not in required]
        if orphans:
            return [f"无依赖声明的顶层包（共{len(orphans)}个，不等同于无用包）:", ", ".join(orphans)]
        return [f"未发现无依赖声明的包（检查了全部{len(installed)}个包）"]
    except Exception as e:
        return [f"包依赖分析失败: {type(e).__name__}: {str(e)[:80]}"]

def detect_outdated_packages():
    try:
        # 【质量缺陷】timeout=15对默认源经常不够；失败与"未检测到"在下方合并为同一句话，看起来像"环境已是最新"
        success, output = run_command([sys.executable, '-m', 'pip', 'list', '--outdated', '--format=columns'], timeout=15)
        if success:
            lines = output.split('\n')
            if len(lines) >= 3:
                outdated = [l.split()[0] for l in lines[2:] if l.strip()]
                if outdated:
                    return [f"过时包（共{len(outdated)}个）:", ", ".join(outdated)]
            return ["所有包都是最新的"]
        return ["未检测到过时包或检测超时"]
    except Exception as e:
        return [f"过时包检测失败: {type(e).__name__}"]

# 【质量缺陷】自由线程模式并非错误却用❌表达，误导；且仅捕获AttributeError一种异常
def get_gil_state():
    try:
        enabled = sys._is_gil_enabled()
        return f"GIL启用: {'✅' if enabled else '❌ (自由线程模式)'}"
    except AttributeError:
        return "GIL启用: ✅ (CPython默认，不支持 _is_gil_enabled)"

def get_gc_config():
    try:
        return [
            f"GC启用: {'✅' if gc.isenabled() else '❌'}",
            f"阈值: {gc.get_threshold()}",
            f"计数: {gc.get_count()}"
        ]
    except Exception as e:
        return [f"GC配置获取失败: {type(e).__name__}"]

def get_recursion_limit():
    return f"递归限制: {sys.getrecursionlimit()}"

def get_import_hooks():
    try:
        hooks = [str(type(h).__name__) for h in sys.meta_path]
        return f"导入钩子: {', '.join(hooks)}"
    except Exception as e:
        return f"导入钩子获取失败: {type(e).__name__}"

def get_loaded_modules():
    try:
        modules = list(sys.modules.keys())
        result = [f"已加载模块数: {len(modules)}", "全部模块:"]
        result.extend([f"  {i+1}. {m}" for i, m in enumerate(modules)])
        return result
    except Exception as e:
        return [f"已加载模块获取失败: {type(e).__name__}"]

def analyze_filesystem():
    results = []
    try:
        import site
        sp = site.getsitepackages()[0]
        pyc_count, pycache_size, egg_info_count = 0, 0, 0
        # 【质量缺陷】整树遍历site-packages无文件数上限，包多的环境（如conda base）可达数十秒
        for root, dirs, files in os.walk(sp):
            for d in dirs:
                if d.endswith('.egg-info') or d.endswith('.dist-info'):
                    egg_info_count += 1
            if os.path.basename(root) == '__pycache__':
                for f in files:
                    try:
                        pycache_size += os.path.getsize(os.path.join(root, f))
                    except Exception:
                        pass
            for f in files:
                if f.endswith('.pyc'): pyc_count += 1
        results.append(f".pyc文件数: {pyc_count}")
        results.append(f"__pycache__总大小: {pycache_size / (1024**2):.2f}MB")
        results.append(f".egg-info/.dist-info目录数: {egg_info_count}")
    except Exception as e:
        results.append(f"文件系统分析失败: {type(e).__name__}: {str(e)[:80]}")
    return results

def check_log_files():
    results = []
    log_locations = []
    if platform.system() == "Windows":
        # 【质量缺陷】TEMP缺失时退化为相对路径*.log，会扫到当前目录
        log_locations = [os.path.join(os.environ.get('TEMP', ''), '*.log')]
    else:
        log_locations = ['/var/log/python*.log', os.path.expanduser('~/.python*.log')]
        
    found_logs = []
    for pattern in log_locations:
        found_logs.extend(glob.glob(pattern))
        
    if found_logs:
        total_size = sum(os.path.getsize(f) for f in found_logs if os.path.exists(f))
        results.append(f"找到 {len(found_logs)} 个日志文件, 总大小: {total_size / (1024**2):.2f}MB")
        results.extend([f"  {f}" for f in found_logs])
    else:
        results.append("未找到Python相关日志文件")
    return results

def check_dns_resolution():
    results = []
    for domain in ['pypi.org', 'pypi.tuna.tsinghua.edu.cn', 'mirrors.aliyun.com']:
        try:
            start = time.time()
            socket.gethostbyname(domain)
            results.append(f"✅ {domain}: {(time.time() - start)*1000:.2f}ms")
        except Exception as e:
            results.append(f"❌ {domain}: 解析失败 ({type(e).__name__})")
    return results

def test_pypi_mirror_speed():
    results = []
    mirrors = [
        ('官方源', 'https://pypi.org/simple'), ('清华源', 'https://pypi.tuna.tsinghua.edu.cn/simple'),
        ('阿里源', 'https://mirrors.aliyun.com/pypi/simple'), ('腾讯源', 'https://mirrors.cloud.tencent.com/pypi/simple')
    ]
    for name, url in mirrors:
        try:
            start = time.time()
            # 【质量缺陷】部分镜像对HEAD /simple返回403/405，会被误报"❌失败"
            req = urllib.request.Request(url, method='HEAD')
            urllib.request.urlopen(req, timeout=5)
            results.append(f"✅ {name}: {(time.time() - start)*1000:.2f}ms")
        except Exception as e:
            results.append(f"❌ {name}: 失败 ({type(e).__name__})")
    return results

def check_proxy_settings():
    results = []
    found = False
    for var in ['HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'NO_PROXY']:
        value = os.environ.get(var) or os.environ.get(var.lower())
        if value:
            found = True
            results.append(f"✅ {var}: {value}")
    if not found:
        results.append("❌ 未检测到代理配置")
    return results

def check_firewall_status():
    results = []
    sys_name = platform.system()
    if sys_name == "Windows":
        success, output = run_command("netsh advfirewall show allprofiles state", shell=True, timeout=5)
        if success:
            results.append("Windows防火墙状态:")
            for line in output.split('\n'):
                # 【质量缺陷】'ON'/'OFF'子串匹配过松，可能误命中无关行
                if line.strip() and ('状态' in line or 'State' in line or 'ON' in line or 'OFF' in line):
                    results.append(f"  {line.strip()}")
        else:
            results.append("❌ 无法检测防火墙状态")
    elif sys_name == "Linux":
        success, output = run_command("ufw status", shell=True, timeout=3)
        results.append(f"UFW状态: {output if success else '未安装或无权限'}")
    else:
        results.append(f"⚠️ {sys_name} 平台暂不支持自动检测防火墙状态")
    return results

def get_system_encoding():
    return [
        f"系统默认编码: {sys.getdefaultencoding()}",
        f"文件系统编码: {sys.getfilesystemencoding()}",
        f"标准输入编码: {getattr(sys.stdin, 'encoding', None)}",
        f"标准输出编码: {getattr(sys.stdout, 'encoding', None)}",
        f"首选locale编码: {locale.getpreferredencoding(False)}"
    ]

def get_process_info():
    results = []
    try:
        import psutil
        process = psutil.Process(os.getpid())
        results.append(f"进程优先级: {process.nice()}")
        results.append(f"CPU占用: {process.cpu_percent(interval=0.1):.2f}%")
        mem_info = process.memory_info()
        results.append(f"内存占用: {mem_info.rss / (1024**2):.2f}MB")
        results.append(f"线程数: {process.num_threads()}")
    except ImportError:
        results.append("❌ psutil未安装，无法获取详细进程信息")
        results.append(f"进程ID: {os.getpid()}")
    except Exception as e:
        results.append(f"进程信息获取失败: {type(e).__name__}")
    return results

def check_database_drivers():
    results = []
    try:
        import sqlite3
        results.append(f"✅ SQLite: {sqlite3.sqlite_version}")
    except Exception:
        results.append("❌ SQLite 不可用")
    
    # 【质量缺陷】"mysql.connector"经find_spec会先导入父包mysql（副作用），未安装时报"导入错误"而非"未安装"
    for driver in ['pymysql', 'mysql.connector', 'psycopg2', 'redis', 'pymongo']:
        success, msg = check_module_import_speed(driver)
        results.append(f"{'✅' if success else '❌'} {driver} {f'-> {msg}' if success else ''}")
    return results

def check_web_frameworks():
    results = []
    for name, module in [('Flask', 'flask'), ('Django', 'django'), ('FastAPI', 'fastapi'), ('Tornado', 'tornado')]:
        success, speed = check_module_import_speed(module)
        if success:
            try:
                mod = __import__(module)
                ver = getattr(mod, '__version__', '未知')
                results.append(f"✅ {name}: {ver} ({speed})")
            except Exception:
                results.append(f"✅ {name}: ({speed})")
        else:
            results.append(f"❌ {name}: {speed}")
            
    results.append("Web服务器:")
    for name, module in [('uWSGI', 'uwsgi'), ('Gunicorn', 'gunicorn'), ('Waitress', 'waitress')]:
        success, msg = check_module_import_speed(module)
        results.append(f"{'✅' if success else '❌'} {name} {f'({msg})' if success else ''}")
    return results

def check_scientific_libraries():
    results = []
    for name, module in [('NumPy', 'numpy'), ('Pandas', 'pandas'), ('Matplotlib', 'matplotlib'), ('SciPy', 'scipy'), ('Scikit-learn', 'sklearn')]:
        success, speed = check_module_import_speed(module)
        if success:
            try:
                mod = __import__(module)
                ver = getattr(mod, '__version__', '未知')
                results.append(f"✅ {name}: {ver} ({speed})")
            except Exception:
                results.append(f"✅ {name}: ({speed})")
        else:
            results.append(f"❌ {name}: {speed}")

    try:
        import torch
        cuda = torch.cuda.is_available()
        if cuda:
            results.append(f"✅ PyTorch CUDA: 是 (设备数: {torch.cuda.device_count()})")
        else:
            results.append("❌ PyTorch CUDA: 否")
    except Exception:
        pass
        
    success, speed = check_module_import_speed('cv2')
    if success:
        try:
            import cv2
            results.append(f"✅ OpenCV: {cv2.__version__} ({speed})")
        except Exception:
            pass
    else:
        results.append(f"❌ OpenCV: {speed}")
    return results

def check_security_vulnerabilities():
    results = []
    success, msg = check_module_import_speed('safety')
    if success:
        results.append(f"✅ safety库可用 ({msg})")
        # 【质量缺陷】safety发现漏洞时以非零码退出→run_command返回失败→永远报"扫描执行失败"（越不安全越像失败，漏洞JSON分支成死代码）；safety 3.x已改用scan命令
        success, output = run_command([sys.executable, '-m', 'safety', 'check', '--json'], timeout=20)
        if success:
            try:
                vulns = json.loads(output)
                if isinstance(vulns, list):
                    vuln_list = vulns
                elif isinstance(vulns, dict):
                    vuln_list = vulns.get("vulnerabilities", [])
                else:
                    vuln_list = []

                if vuln_list:
                    results.append(f"⚠️ 发现 {len(vuln_list)} 个安全漏洞")
                else:
                    results.append("✅ 未发现已知安全漏洞")
            except Exception:
                results.append("⚠️ 漏洞扫描返回非标准JSON")
        else:
            results.append("⚠️ 漏洞扫描执行失败")
    else:
        results.append("❌ safety库未安装")
    return results

# 【质量缺陷】名为"冲突检查"却只打印sys.path，无任何冲突判定；PYTHONPATH未设置本属常态却被标❌
def check_python_path_conflicts():
    results = ["sys.path顺序:"]
    for i, p in enumerate(sys.path, 1):
        results.append(f"  {i}. {p}")
        
    pythonpath = os.environ.get('PYTHONPATH')
    if pythonpath:
        results.append(f"PYTHONPATH: {pythonpath}")
    else:
        results.append("❌ PYTHONPATH未设置")
    return results

def check_virtual_environment():
    results = []
    in_venv = sys.prefix != getattr(sys, "base_prefix", sys.prefix)
    results.append(f"虚拟环境: {'✅ 是' if in_venv else '❌ 否'}")
    if in_venv:
        results.append(f"环境路径: {sys.prefix}")
        if os.path.exists(os.path.join(sys.prefix, 'conda-meta')):
            results.append("类型: Conda环境")
        elif os.path.exists(os.path.join(sys.prefix, 'pyvenv.cfg')):
            results.append("类型: venv环境")
    return results

def check_windows_env_details():
    if platform.system() != "Windows":
        return ["仅支持Windows系统"]
    results = []
    vars_to_check = ["PATH", "PATHEXT", "PYTHONHOME", "PYTHONPATH", "PYTHONIOENCODING", "PYTHONUTF8"]
    for var in vars_to_check:
        value = os.environ.get(var)
        if value:
            results.append(f"✅ {var}:")
            if var == "PATH":
                for item in value.split(";"):
                    if item.strip(): results.append(f"  {item.strip()}")
            else:
                results.append(f"  {value}")
    return results if results else ["（未检测到以上环境变量）"]

def check_user_host_info():
    results = []
    try: 
        results.append(f"当前用户: {os.getlogin()}")
    except Exception:
        user = os.environ.get('USER') or os.environ.get('USERNAME', '未知')
        results.append(f"当前用户: {user}")
    results.append(f"主机名: {socket.gethostname()}")
    results.append(f"当前工作目录: {os.getcwd()}")
    results.append(f"用户主目录: {os.path.expanduser('~')}")
    return results

def check_temp_dir_permissions():
    results = []
    try:
        temp_root = tempfile.gettempdir()
        results.append(f"临时目录: {temp_root}")
        test_dir = os.path.join(temp_root, f"py_diag_test_{os.getpid()}")
        os.makedirs(test_dir, exist_ok=True)
        results.append("✅ 可创建临时子目录")
        test_file = os.path.join(test_dir, "test.txt")
        with open(test_file, "w") as f: f.write("test")
        results.append("✅ 可读写临时文件")
        os.remove(test_file)
        os.rmdir(test_dir)
    except Exception as e:
        results.append(f"❌ 临时目录权限检测失败: {type(e).__name__}")
    return results

def check_basic_network_connectivity():
    results = []
    for name, url in [("PyPI", "https://pypi.org"), ("Python官网", "https://www.python.org"), ("GitHub", "https://github.com")]:
        try:
            start = time.time()
            urllib.request.urlopen(url, timeout=5)
            results.append(f"✅ {name}: 可访问 ({(time.time() - start)*1000:.2f}ms)")
        except Exception as e:
            results.append(f"❌ {name}: 不可访问 ({type(e).__name__})")
    return results

def check_windows_store_alias_issue():
    if platform.system() != "Windows":
        return ["仅支持Windows系统"]
    results = []
    # 【质量缺陷】WindowsApps目录默认就在用户PATH里，本检查几乎恒输出"⚠️可能干扰"（常态误报）；子串匹配亦会误命中相似路径；%LOCALAPPDATA%缺失时expandvars保留字面量导致恒"未检测到"
    local_apps = os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WindowsApps")
    path_env = os.environ.get("PATH", "")
    if local_apps.lower() in path_env.lower():
        results.append("⚠️ PATH 中包含 WindowsApps，可能存在 Store Python 干扰")
    else:
        results.append("✅ PATH 中未检测到 WindowsApps")
    return results

def check_packaging_tools():
    results = []
    for name, module in [("PyInstaller", "PyInstaller"), ("Nuitka", "nuitka"), ("cx_Freeze", "cx_Freeze")]:
        success, msg = check_module_import_speed(module)
        if success:
            results.append(f"✅ {name}: {msg}")
        else:
            results.append(f"❌ {name}: {msg}")
    return results

def check_registry_full():
    if platform.system() != "Windows":
        return ["仅支持Windows系统"]
    results = []
    try:
        import winreg
        results.append("【注册表 - Python安装】")
        paths = [
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Python\PythonCore"),
            (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Python\PythonCore")
        ]
        found = False
        for hive, path in paths:
            try:
                # 【质量缺陷】未加KEY_WOW64_64KEY，32位Python只能看到WOW6432Node视图，漏掉64位安装
                key = winreg.OpenKey(hive, path)
                num = winreg.QueryInfoKey(key)[0]
                for i in range(num):
                    ver = winreg.EnumKey(key, i)
                    found = True
                    results.append(f"  ✅ Python {ver}")
                    try:
                        # 【质量缺陷】未CloseKey，句柄依赖GC回收
                        install_key = winreg.OpenKey(key, f"{ver}\\InstallPath")
                        install_path, _ = winreg.QueryValueEx(install_key, "")
                        results.append(f"     路径: {install_path}")
                    except Exception:
                        pass
                winreg.CloseKey(key)
            except FileNotFoundError:
                pass
        if not found:
            results.append("  ❌ 注册表中未找到Python安装信息")
    except ImportError:
        results.append("❌ winreg模块不可用")
    except Exception as e:
        results.append(f"❌ 注册表检查失败: {type(e).__name__}")
    return results

def check_ip_addresses():
    results = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        results.append(f"✅ 本机内网IP: {local_ip}")
    except Exception as e:
        results.append(f"❌ 获取内网IP失败: {type(e).__name__}")
    
    try:
        start = time.time()
        # 【质量缺陷】把公网IP查询发给第三方服务并将结果写入报告/导出，存在隐私泄露面
        req = urllib.request.Request('https://api.ipify.org?format=json')
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode())
            results.append(f"✅ 公网IP: {data.get('ip')} ({(time.time()-start)*1000:.0f}ms)")
    except Exception as e:
        results.append(f"❌ 获取公网IP失败: {type(e).__name__}")
    return results

def check_common_ports():
    results = []
    ports_to_check = {
        22: "SSH", 80: "HTTP", 443: "HTTPS", 
        3306: "MySQL", 5432: "PostgreSQL", 6379: "Redis", 
        27017: "MongoDB", 8080: "HTTP-Alt", 8888: "Jupyter"
    }
    for port, name in ports_to_check.items():
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            result = sock.connect_ex(("127.0.0.1", port))
            if result == 0:
                results.append(f"⚠️ 端口 {port} ({name}): 已被占用")
            else:
                results.append(f"✅ 端口 {port} ({name}): 空闲")
            sock.close()
        except Exception:
            results.append(f"❌ 端口 {port} ({name}): 检测失败")
    return results

def check_path_validity():
    results = []
    path_env = os.environ.get("PATH", "")
    sep = ";" if platform.system() == "Windows" else ":"
    paths = path_env.split(sep)
    
    invalid_count = 0
    valid_count = 0
    for p in paths:
        # 【质量缺陷】只strip不去引号：PATH中带引号的条目会被误判"不存在"
        p = p.strip()
        if not p:
            continue
        if os.path.exists(p):
            valid_count += 1
        else:
            invalid_count += 1
            results.append(f"  ❌ 不存在: {p}")
    
    results.insert(0, f"PATH有效性检查: 有效 {valid_count} 个, 无效 {invalid_count} 个")
    return results

def check_hosts_file():
    results = []
    hosts_path = r"C:\Windows\System32\drivers\etc\hosts" if platform.system() == "Windows" else "/etc/hosts"
    if os.path.exists(hosts_path):
        try:
            # 【质量缺陷】固定UTF-8读取，hosts中非UTF-8编码的中文注释显示为乱码
            with open(hosts_path, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
            custom_entries = [l.strip() for l in lines if l.strip() and not l.strip().startswith('#')]
            results.append(f"✅ Hosts文件存在: {hosts_path} (注意: 可能包含敏感内网映射)")
            if custom_entries:
                # 【质量缺陷】内网映射等敏感条目会随报告展示并导出，仅有一句内联提醒、无脱敏或开关
                results.append(f"  自定义解析记录 ({len(custom_entries)}条，仅展示前10条):")
                results.extend([f"    {e}" for e in custom_entries[:10]])
            else:
                results.append("  无自定义解析记录")
        except Exception as e:
            results.append(f"❌ 读取Hosts失败: {type(e).__name__} (可能需要管理员权限)")
    else:
        results.append("❌ Hosts文件不存在")
    return results

def check_timezone_and_time():
    results = []
    try:
        tz_name = time.tzname
        is_dst = time.localtime().tm_isdst
        if is_dst == 1:
            utc_offset = time.altzone
        elif is_dst == 0:
            utc_offset = time.timezone
        else:
            utc_offset = time.timezone
            results.append("⚠️ 夏令时状态未知，时区偏移可能不准确")
            
        offset_hours = -utc_offset / 3600.0
        results.append(f"时区: {tz_name[0]} (UTC{'+' if offset_hours>=0 else ''}{offset_hours})")
        
        try:
            # 【质量缺陷】start赋值后从未使用（死代码）
            start = time.time()
            req = urllib.request.Request("https://www.baidu.com", method='HEAD')
            with urllib.request.urlopen(req, timeout=3) as resp:
                server_time_str = resp.headers['Date']
                # 【质量缺陷】%Z只匹配GMT/UTC，Date头为+0000等形式时解析失败落入"无法校验"；datetime.utcnow()自3.12起已弃用
                server_time = datetime.strptime(server_time_str, '%a, %d %b %Y %H:%M:%S %Z')
                local_time = datetime.utcnow()
                diff = abs((server_time - local_time).total_seconds())
                if diff < 5:
                    results.append(f"✅ 系统时间同步: 正常 (误差 {diff:.1f}秒)")
                else:
                    results.append(f"⚠️ 系统时间可能不准: 误差 {diff:.1f}秒")
        except Exception:
            results.append("⚠️ 无法校验系统时间同步状态")
    except Exception as e:
        results.append(f"时区检测失败: {type(e).__name__}")
    return results

def check_core_python_tools_detail():
    results = []
    for tool in ["pip", "setuptools", "wheel"]:
        try:
            mod = __import__(tool)
            version = getattr(mod, "__version__", "未知版本")
            file_path = getattr(mod, "__file__", "未知路径")
            results.append(f"✅ {tool} (版本: {version}, 路径: {file_path})")
        except ImportError:
            results.append(f"❌ {tool} 未安装")
        except Exception as e:
            results.append(f"❌ {tool} 检测失败: {type(e).__name__}")
    return results

def show_gui():
    import tkinter as tk
    from tkinter import scrolledtext, messagebox

    root = tk.Tk()
    root.title("环境诊断工具")
    root.geometry("1280x900")

    title_frame = tk.Frame(root, bg="#2c3e50", height=70)
    title_frame.pack(fill=tk.X)
    title_frame.pack_propagate(False)
    tk.Label(title_frame, text="🐍 环境诊断工具", font=("微软雅黑", 20, "bold"), bg="#2c3e50", fg="white").pack(pady=15)

    progress_frame = tk.Frame(root, bg="#ecf0f1", height=50)
    progress_frame.pack(fill=tk.X)
    progress_frame.pack_propagate(False)
    progress_label = tk.Label(progress_frame, text="准备就绪", font=("微软雅黑", 10, "bold"), bg="#ecf0f1", fg="#34495e")
    progress_label.pack(pady=3)
    progress_percent = tk.Label(progress_frame, text="0%", font=("微软雅黑", 9), bg="#ecf0f1", fg="#7f8c8d")
    progress_percent.pack()

    text_frame = tk.Frame(root)
    text_frame.pack(padx=20, pady=15, fill=tk.BOTH, expand=True)
    text_area = scrolledtext.ScrolledText(text_frame, font=("Consolas", 10), wrap=tk.WORD, bg="#f8f9fa")
    text_area.pack(fill=tk.BOTH, expand=True)
    text_area.tag_config("section_title", font=("微软雅黑", 14, "bold"), foreground="#2c3e50", spacing1=10, spacing3=5)

    btn_frame = tk.Frame(root)
    btn_frame.pack(pady=12)

    def get_pip_status():
        success, pip_ver = run_command([sys.executable, '-m', 'pip', '--version'], timeout=3, use_cache=True)
        return [
            f"{'✅ ' + pip_ver if success else '❌ pip不可用: ' + pip_ver}",
            f"镜像源: {get_pip_mirror_config()}",
            f"缓存: {get_pip_cache_stats()}"
        ]

    def get_installed_packages():
        success, pkg_result = run_command([sys.executable, '-m', 'pip', 'list', '--format=freeze'], timeout=12, use_cache=True)
        if not success: return [f"❌ 获取已安装包失败: {pkg_result}"]
        pkgs = [p for p in pkg_result.split('\n') if p.strip()]
        result = [f"包总数: {len(pkgs)}", "全部包:"]
        result.extend([f"  {i+1}. {p}" for i, p in enumerate(pkgs)])
        return result

    def get_key_libraries():
        results = []
        for module_name, display_name in [('pip', 'pip'), ('setuptools', 'setuptools'), ('wheel', 'wheel'), ('requests', 'requests'), ('tkinter', 'tkinter')]:
            ok, msg = check_module_import_speed(module_name)
            results.append(f"{'✅' if ok else '❌'} {display_name} ({msg})")
        return results

    checks = [
        ("系统信息与硬件", lambda: [
            f"操作系统: {platform.system()} {platform.release()}",
            f"系统版本: {platform.version()}",
            f"处理器架构: {platform.machine()}",
            f"系统内存: {get_memory_info()}",
            f"磁盘空间: {get_disk_space(sys.prefix)}",
            f"运行时间: {get_system_uptime()}",
            "",
            "CPU信息:", *get_cpu_info(),
            "", 
            "GPU信息:", *get_gpu_info(),
            "",
            "电池状态:", *get_battery_status()
        ]),
        ("用户和主机信息", check_user_host_info),
        ("系统时区与时间", check_timezone_and_time),
        ("网络与IP配置", check_ip_addresses),
        ("常用端口占用", check_common_ports),
        ("Hosts文件解析", check_hosts_file),
        ("PATH有效性检查", check_path_validity),
        ("系统编码", get_system_encoding),
        ("进程信息", get_process_info),
        ("Python解释器信息", lambda: [
            f"版本: {sys.version}",
            f"可执行文件: {sys.executable}",
            f"安装前缀: {sys.prefix}",
            f"启动耗时: {get_python_startup_time()}",
            "", "编译信息:", *[f"  {l}" for l in get_python_compile_info()]
        ]),
        ("Python可用最大内存", lambda: [get_python_max_memory()]),
        ("虚拟环境详情", check_virtual_environment),
        ("虚拟环境配置", check_venv_config),
        ("Python路径分析", check_python_path_conflicts),
        ("Windows Store别名干扰", check_windows_store_alias_issue),
        ("pip状态", get_pip_status),
        ("核心工具详情", lambda: [
            *[f"  {l}" for l in check_core_python_tools_detail()]
        ]),
        ("pip配置文件", check_pip_config),
        ("site-packages", get_site_packages_info),
        ("环境变量", get_environment_variables),
        ("Windows环境变量详情", check_windows_env_details),
        ("SSL证书", test_ssl_certificates),
        ("DNS解析速度", check_dns_resolution),
        ("基础网络连通性", check_basic_network_connectivity),
        ("PyPI镜像源速度测试", test_pypi_mirror_speed),
        ("代理配置", check_proxy_settings),
        ("防火墙状态", check_firewall_status),
        ("开发工具链 (Git/Docker等)", check_dev_tools),
        ("其他语言运行时", check_other_runtimes),
        ("SSH密钥配置", check_ssh_keys),
        ("包管理工具", check_package_managers),
        ("打包工具", check_packaging_tools),
        ("权限检查", check_permissions),
        ("磁盘I/O性能", lambda: [f"I/O测试: {test_disk_io_performance()}", f"临时空间: {check_temp_space()}"]),
        ("临时目录权限", check_temp_dir_permissions),
        ("数据库驱动", check_database_drivers),
        ("Web框架", check_web_frameworks),
        ("科学计算库", check_scientific_libraries),
        ("安全漏洞检测", check_security_vulnerabilities),
        ("关键库检查", get_key_libraries),
        ("已安装包", get_installed_packages),
        ("包依赖分析", lambda: [*detect_orphan_packages(), *detect_outdated_packages()]),
        ("GIL和垃圾回收", lambda: [get_gil_state(), *get_gc_config(), get_recursion_limit(), get_import_hooks()]),
        ("已加载模块", get_loaded_modules),
        ("文件系统分析", analyze_filesystem),
        ("日志文件", check_log_files),
        ("注册表检查", check_registry_full),
        ("诊断分析", lambda: (lambda issues, warnings: [
            *([f"🔴 严重问题:", *[f"  {i}" for i in issues]] if issues else []),
            *([f"🟡 警告:", *[f"  {w}" for w in warnings]] if warnings else []),
            *(["✅ 环境状态良好"] if not issues and not warnings else [])
        ])(*check_common_issues()))
    ]

    full_report = []
    # 【质量缺陷】共享状态无代数(generation)/取消机制：诊断中途点"刷新"会串项、重复输出、报告错乱
    current_worker = {"thread": None, "result": None, "error": None}

    def stream_output(lines, index, chunk_size=100, is_final=False):
        if not isinstance(lines, list):
            lines = [str(lines)]
        
        chunk = lines[:chunk_size]
        content = "\n".join(str(line) for line in chunk) + "\n"
        text_area.insert(tk.END, content)
        full_report.extend(str(line) for line in chunk)
        text_area.see(tk.END)
        root.update_idletasks()
        
        if len(lines) > chunk_size:
            root.after(1, lambda: stream_output(lines[chunk_size:], index, chunk_size=chunk_size, is_final=is_final))
        else:
            # 【质量缺陷】safe_execute吞掉所有Exception且不重抛，error恒为None，此分支永不执行——异常堆栈永远不显示，排障能力为零
            if current_worker["error"]:
                err_line = f"[DEBUG] {current_worker['error'].splitlines()[-1][:300]}"
                text_area.insert(tk.END, err_line + "\n")
                full_report.append(err_line)
            text_area.see(tk.END)
            if not is_final:
                root.after(5, lambda: run_next_check(index + 1))

    def worker_runner(func, name):
        try:
            result = safe_execute(func, default_return=[f"❌ {name}检测失败"])
            if not isinstance(result, list):
                result = [str(result)]
            current_worker["result"] = result
            current_worker["error"] = None
        except Exception as e:
            current_worker["result"] = [f"❌ 检测失败: {type(e).__name__}: {str(e)[:200]}"]
            current_worker["error"] = traceback.format_exc()

    def poll_worker(index):
        thread = current_worker["thread"]
        if thread is not None and thread.is_alive():
            root.after(100, lambda: poll_worker(index))
            return
        
        result = current_worker["result"] or ["❌ 未知错误: 检测线程未返回结果"]
        stream_output(result, index)

    def run_next_check(index=0):
        if index >= len(checks):
            progress_label.config(text="✅ 诊断完成")
            progress_percent.config(text="100%")
            footer = ["", "=" * 100, f"报告生成完成 | Python {platform.python_version()}", "=" * 100]
            stream_output(footer, index, is_final=True)
            return

        name, func = checks[index]
        # 【质量缺陷】进度按项数计算，遇到长检测项时进度条会僵在某个百分比数十秒
        percent = int((index / len(checks)) * 100)
        progress_label.config(text=f"🔄 [{index + 1}/{len(checks)}] 正在检测: {name}...")
        progress_percent.config(text=f"{percent}%")
        root.update_idletasks()

        section_header = f"\n【{name}】"
        text_area.insert(tk.END, section_header + "\n", "section_title")
        full_report.append(section_header)

        current_worker["result"] = None
        current_worker["error"] = None
        t = threading.Thread(target=worker_runner, args=(func, name), daemon=True)
        current_worker["thread"] = t
        t.start()
        root.after(100, lambda: poll_worker(index))

    def copy_all():
        root.clipboard_clear()
        root.clipboard_append("\n".join(full_report))
        messagebox.showinfo("成功", "诊断报告已复制到剪贴板")

    def export_txt():
        # 【质量缺陷】导出文件包含公网IP、git身份、hosts映射等敏感信息，明文写入主目录且无脱敏或开关
        filename = os.path.join(os.path.expanduser("~"), f"env_diagnostic_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt")
        try:
            with open(filename, 'w', encoding='utf-8', errors='replace') as f:
                f.write("\n".join(full_report))
            messagebox.showinfo("成功", f"已导出到: {os.path.abspath(filename)}")
        except Exception as e:
            messagebox.showerror("错误", f"导出失败: {type(e).__name__}: {e}")

    # 【质量缺陷】刷新未取消在飞线程与已排队的after回调，也无running标志，会产生双链竞争（串项/重复输出/报告错乱）
    def refresh():
        with COMMAND_CACHE_LOCK:
            COMMAND_CACHE.clear()
        text_area.delete(1.0, tk.END)
        full_report.clear()
        progress_label.config(text="准备就绪")
        progress_percent.config(text="0%")
        header_lines = ["🔄 开始诊断...", f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}", "=" * 100]
        text_area.insert(tk.END, "\n".join(header_lines) + "\n")
        full_report.extend(header_lines)
        root.after(100, run_next_check)

    tk.Button(btn_frame, text="📋 复制全部", command=copy_all, width=14, font=("微软雅黑", 10)).pack(side=tk.LEFT, padx=6)
    tk.Button(btn_frame, text="💾 导出报告", command=export_txt, width=14, font=("微软雅黑", 10)).pack(side=tk.LEFT, padx=6)
    tk.Button(btn_frame, text="🔄 刷新", command=refresh, width=14, font=("微软雅黑", 10)).pack(side=tk.LEFT, padx=6)
    tk.Button(btn_frame, text="❌ 关闭", command=root.destroy, width=14, font=("微软雅黑", 10)).pack(side=tk.LEFT, padx=6)

    init_lines = ["🔄 开始诊断...", f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}", "=" * 100]
    text_area.insert(tk.END, "\n".join(init_lines) + "\n")
    full_report.extend(init_lines)
    root.after(100, run_next_check)
    
    root.mainloop()

if __name__ == "__main__":
    try:
        show_gui()
    except Exception as e:
        error_msg = traceback.format_exc()
        try:
            # 【质量缺陷】写当前工作目录（双击运行时可能是只读位置），提示语也未给出绝对路径
            with open("diagnostic_error.log", "w", encoding="utf-8") as f:
                f.write(error_msg)
        # 【质量缺陷】裸except(E722)：写入失败被静默吞掉，用户却被告知"已记录"
        except:
            pass
        
        try:
            import tkinter as tk
            from tkinter import messagebox
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror("启动失败", f"程序发生严重错误无法启动:\n{str(e)[:500]}\n\n详细错误已记录到 diagnostic_error.log")
        except:
            # 【质量缺陷】pythonw下无stdin，input()抛RuntimeError致静默退出；此处同为裸except(E722)
            input(f"程序发生严重错误:\n{error_msg}\n按回车键退出...")