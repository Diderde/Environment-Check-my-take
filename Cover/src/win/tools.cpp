// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "win/tools.h"

#include <string>
#include <vector>

namespace envdoctor {
namespace {

/// vswhere 的安装位置是固定的（VS 安装器自己放的），不依赖 PATH。
const char* kVswherePath = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";

// ------------------------------------------------------------------ 解释器侧固定脚本
//
// 「诊断 Python」这件事没法在 C++ 里凭空做：`sys.prefix` / `sys.path` / `sysconfig` /
// `importlib` 的答案只有解释器自己知道。因此这些检查照旧**调用系统上的解释器**，
// 由下面这些**编译期字面量脚本**取事实；C++ 侧只做解析与判定。
//
// 三条纪律（不这么做就会引入注入面或解析歧义）：
//   ① 脚本里**不出现运行期数据**：需要按库名区分的（冷导入）就一条库一条命令，
//      库名直接写在脚本里，不做"拼参数"；
//   ② 唯一需要运行期数据的探测（镜像可达性，地址来自 pip 配置）走固定环境变量
//      `ENVDOCTOR_PROBE_URL`，由固定脚本自己读 —— 命令行本身仍是字面量；
//   ③ 脚本里避开 `%` `|` `<` `>` `&` `^`：解释器是 `.cmd` 垫片时命令会经 `cmd /C`，
//      cmd 会把这些字符当自己的语法吃掉（`%a%b%` 甚至会被当变量展开）。
//
// 输出一律是 `KEY<TAB>VALUE` 逐行（重复键按出现顺序保留），解析见 checks/python.cpp。

/// 解释器事实清单：版本/实现/可执行文件/前缀、site-packages、GIL、sys.path、
/// 「影子模块」名字集合（标准库 + 常用库，去掉内置与冻结模块）、以及常用库与打包工具的版本。
const char* const kPythonInfoScript = R"PY(import sys, platform, locale
print("version\t" + platform.python_version())
print("major\t" + str(sys.version_info.major))
print("minor\t" + str(sys.version_info.minor))
print("impl\t" + platform.python_implementation())
print("exe\t" + (sys.executable or ""))
print("prefix\t" + (sys.prefix or ""))
print("base_prefix\t" + (getattr(sys, "base_prefix", sys.prefix) or ""))
try:
    import sysconfig
    purelib = sysconfig.get_paths().get("purelib") or ""
except Exception:
    purelib = ""
print("purelib\t" + purelib)
try:
    enabled = sys._is_gil_enabled
except AttributeError:
    gil = "na"
else:
    gil = "1" if enabled() else "0"
print("gil\t" + gil)
print("encoding\t" + ((getattr(sys.stdout, "encoding", "") or "").lower() or "未知"))
print("preferred\t" + ((locale.getpreferredencoding(False) or "").lower() or "未知"))
for entry in sys.path:
    print("path\t" + str(entry))
names = set(getattr(sys, "stdlib_module_names", frozenset()))
names.update(("numpy", "pandas", "requests", "flask", "django", "fastapi", "cv2", "sklearn",
              "PIL", "torch", "pytest", "yaml", "dotenv", "setuptools", "pip", "wheel",
              "psycopg2", "pymysql", "redis", "pymongo", "matplotlib", "scipy", "httpx"))
names -= set(sys.builtin_module_names)
try:
    from importlib.machinery import FrozenImporter
except ImportError:
    shadow = [n for n in names if n.isidentifier()]
else:
    shadow = []
    for n in names:
        if not n.isidentifier():
            continue
        try:
            if FrozenImporter.find_spec(n, None) is not None:
                continue
        except Exception:
            pass
        shadow.append(n)
for n in shadow:
    print("shadow\t" + n)
import importlib.util, importlib.metadata
libs = (("numpy", "numpy"), ("pandas", "pandas"), ("requests", "requests"),
        ("cv2", "opencv-python"), ("sklearn", "scikit-learn"), ("PIL", "pillow"),
        ("flask", "flask"), ("django", "django"), ("fastapi", "fastapi"),
        ("pymysql", "pymysql"), ("psycopg2", "psycopg2-binary"), ("redis", "redis"),
        ("pymongo", "pymongo"), ("torch", "torch"), ("matplotlib", "matplotlib"),
        ("scipy", "scipy"))
pkgs = (("PyInstaller", "pyinstaller"), ("nuitka", "nuitka"), ("cx_Freeze", "cx-freeze"))
for group, pairs in (("libs", libs), ("pkg", pkgs)):
    for mod, dist in pairs:
        try:
            if importlib.util.find_spec(mod) is None:
                continue
        except Exception:
            continue
        try:
            ver = importlib.metadata.version(dist)
        except Exception:
            ver = "版本未知"
        print(group + "\t" + mod + "\t" + ver)
)PY";

/// 动态项 `python.import.<库>` 的登记扫描：哪些库在本机解释器里 import 得到。
/// 旧实现用的是 `importlib.util.find_spec`，这里等价地在子进程里问一次。
const char* const kImportScanScript = R"PY(import importlib.util
for name in ("pip", "setuptools", "wheel", "requests", "numpy", "pandas"):
    try:
        found = importlib.util.find_spec(name) is not None
    except Exception:
        found = False
    print(name + "\t" + ("1" if found else "0"))
)PY";

/// 冷导入测速：**全新子进程**里计时，避免缓存失真与线程污染（旧实现为此专门起子进程）。
/// 每库一条独立命令（库名内联为字面量），不做运行期拼参。
///
/// 注意：这六条必须各自是一整段字面量。早先想用一个宏把"公共脚本 + 库名"拼起来，
/// 结果宏的行连接把整段脚本并成一行（`import time` 与 `start = ...` 之间没有换行），
/// Python 直接语法报错 → 六个导入项全部误判成"导入失败"。
const char* const kColdImportPip = R"PY(import time
start = time.perf_counter()
try:
    __import__("pip")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

const char* const kColdImportSetuptools = R"PY(import time
start = time.perf_counter()
try:
    __import__("setuptools")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

const char* const kColdImportWheel = R"PY(import time
start = time.perf_counter()
try:
    __import__("wheel")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

const char* const kColdImportRequests = R"PY(import time
start = time.perf_counter()
try:
    __import__("requests")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

const char* const kColdImportNumpy = R"PY(import time
start = time.perf_counter()
try:
    __import__("numpy")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

const char* const kColdImportPandas = R"PY(import time
start = time.perf_counter()
try:
    __import__("pandas")
    print((time.perf_counter() - start) * 1000)
except Exception:
    print(-1)
)PY";

/// 证书与 TLS 探测：CA 来源（Windows 上 cafile/capath 为空是正常形态）、
/// 一次真实握手、以及失败时的异常类名（报告里逐字用它，见旧实现的 detail 文案）。
const char* const kSslProbeScript = R"PY(import ssl, time, urllib.error, urllib.request
print("openssl\t" + ssl.OPENSSL_VERSION)
try:
    paths = ssl.get_default_verify_paths()
    print("ca\t" + (paths.cafile or paths.capath or ""))
except Exception as exc:
    print("caerr\t" + type(exc).__name__)
start = time.perf_counter()
try:
    with urllib.request.urlopen("https://pypi.org", timeout=8):
        pass
except urllib.error.URLError as exc:
    reason = getattr(exc, "reason", exc)
    cert = "1" if isinstance(reason, ssl.SSLCertVerificationError) else "0"
    print("urlerr\t" + type(reason).__name__ + "\t" + cert)
except Exception as exc:
    print("exc\t" + type(exc).__name__)
else:
    print("ok\t" + format((time.perf_counter() - start) * 1000, ".0f"))
)PY";

/// 镜像可达性探测：地址（可能带凭据）由调用方经 `ENVDOCTOR_PROBE_URL` 传入，
/// 请求用真实地址、报告只出现脱敏形态 —— 与旧实现一致（旧实现是进程内 urlopen）。
const char* const kUrlProbeScript = R"PY(import os, time, urllib.request
base = (os.environ.get("ENVDOCTOR_PROBE_URL") or "").rstrip("/")
start = time.perf_counter()
try:
    with urllib.request.urlopen(base + "/simple/", timeout=8):
        pass
except Exception as exc:
    print("err\t" + type(exc).__name__)
else:
    print("ok\t" + format((time.perf_counter() - start) * 1000, ".0f"))
)PY";

}  // namespace

std::string airani_iofifteen(YukinaMinato tool) {
    switch (tool) {
        case YukinaMinato::Git:
            return "git";
        case YukinaMinato::Node:
            return "node";
        case YukinaMinato::Npm:
            return "npm";
        case YukinaMinato::Java:
            return "java";
        case YukinaMinato::Go:
            return "go";
        case YukinaMinato::Rustc:
            return "rustc";
        case YukinaMinato::Cargo:
            return "cargo";
        case YukinaMinato::Gcc:
            return "gcc";
        case YukinaMinato::Gxx:
            return "g++";
        case YukinaMinato::Make:
            return "make";
        case YukinaMinato::DotNet:
            return "dotnet";
        case YukinaMinato::Python:
            return "python";
        case YukinaMinato::Nvcc:
            return "nvcc";
        case YukinaMinato::Vswhere:
            return "vswhere";
        case YukinaMinato::Kubectl:
            return "kubectl";
        case YukinaMinato::Clang:
            return "clang";
        case YukinaMinato::Clangxx:
            return "clang++";
        case YukinaMinato::Cmake:
            return "cmake";
        case YukinaMinato::Ninja:
            return "ninja";
        case YukinaMinato::Lua:
            return "lua";
        case YukinaMinato::Luajit:
            return "luajit";
        case YukinaMinato::Luarocks:
            return "luarocks";
        case YukinaMinato::Mvn:
            return "mvn";
        case YukinaMinato::Gradle:
            return "gradle";
        // say no to perv. —— FFmpeg 是表驱动检查，漏了这一行会让 `--require ffmpeg`
        // 恒比空串、静默失效（CLI 侧校验用的是检查项 id，所以不报警告）。
        case YukinaMinato::Ffmpeg:
            return "ffmpeg";
        // 下列都是"解释器与包管理"检查用的复合命令：它们由各自的检查按检查项 id 判定，
        // 不参与表驱动的 required 匹配，故统一返回空串（与 default 同义，写出来是为了
        // 让"新增命令有没有 required 标识"这件事在代码里可见，而不是靠 default 兜）。
        case YukinaMinato::PythonInfo:
        case YukinaMinato::PythonStartupPing:
        case YukinaMinato::WherePython:
        case YukinaMinato::PyLauncherList:
        case YukinaMinato::PythonPipVersion:
        case YukinaMinato::PythonPipListFreeze:
        case YukinaMinato::PythonPipListOutdated:
        case YukinaMinato::PythonPipConfigList:
        case YukinaMinato::PythonPipCacheDir:
        case YukinaMinato::PythonPipCheck:
        case YukinaMinato::PythonImportScan:
        case YukinaMinato::PythonImportPip:
        case YukinaMinato::PythonImportSetuptools:
        case YukinaMinato::PythonImportWheel:
        case YukinaMinato::PythonImportRequests:
        case YukinaMinato::PythonImportNumpy:
        case YukinaMinato::PythonImportPandas:
        case YukinaMinato::PythonSslProbe:
        case YukinaMinato::PythonUrlProbe:
        case YukinaMinato::GitConfigIdentity:
        case YukinaMinato::GitConfigKeys:
            return "";
        default:
            // 复合检查（Javac/VswhereVc/NetshState/Docker* 等）不参与表驱动的 required 匹配
            return "";
    }
}

void kureiji_ollie(YukinaMinato tool, std::string* program, std::vector<std::string>* args) {
    const auto set = [program, args](const char* prog, std::vector<std::string> argv) {
        *program = prog;
        *args = std::move(argv);
    };

    switch (tool) {
        case YukinaMinato::Git:
            set("git", {"--version"});
            break;
        case YukinaMinato::Node:
            set("node", {"--version"});
            break;
        case YukinaMinato::Npm:
            set("npm", {"-v"});
            break;
        case YukinaMinato::Java:
            set("java", {"-version"});
            break;
        case YukinaMinato::Go:
            set("go", {"version"});
            break;
        case YukinaMinato::Rustc:
            set("rustc", {"--version"});
            break;
        case YukinaMinato::Cargo:
            set("cargo", {"--version"});
            break;
        case YukinaMinato::Gcc:
            set("gcc", {"--version"});
            break;
        case YukinaMinato::Gxx:
            set("g++", {"--version"});
            break;
        case YukinaMinato::Make:
            set("make", {"--version"});
            break;
        case YukinaMinato::DotNet:
            set("dotnet", {"--version"});
            break;
        case YukinaMinato::Python:
            set("python", {"--version"});
            break;
        case YukinaMinato::NetshState:
            set("cmd.exe", {"/C", "chcp", "65001>nul&netsh", "advfirewall", "show", "allprofiles",
                            "state"});
            break;
        case YukinaMinato::W32tmAliyun:
            set("w32tm", {"/stripchart", "/computer:ntp.aliyun.com", "/samples:1", "/dataonly"});
            break;
        case YukinaMinato::PowershellGpu:
            set("powershell", {"-NoProfile", "-Command",
                               "[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
                               "Get-CimInstance Win32_VideoController | "
                               "Select-Object -ExpandProperty Name"});
            break;
        case YukinaMinato::CurlIpify:
            set("curl", {"-s", "-m", "8", "https://api.ipify.org"});
            break;
        case YukinaMinato::Docker:
            set("docker", {"--version"});
            break;
        case YukinaMinato::DockerInfo:
            set("docker", {"info", "--format", "{{.ServerVersion}}"});
            break;
        case YukinaMinato::DockerImages:
            set("docker", {"images", "-q"});
            break;
        case YukinaMinato::DockerPs:
            set("docker", {"ps", "-q"});
            break;
        case YukinaMinato::Podman:
            set("podman", {"--version"});
            break;
        case YukinaMinato::Conda:
            set("conda", {"--version"});
            break;
        case YukinaMinato::Poetry:
            set("poetry", {"--version"});
            break;
        case YukinaMinato::Pipenv:
            set("pipenv", {"--version"});
            break;
        case YukinaMinato::Nvcc:
            set("nvcc", {"--version"});
            break;
        case YukinaMinato::Vswhere:
            // 必须带 -products *：默认只找 Community/Professional/Enterprise，
            // 只装 Build Tools 的机器上会"退出码 0 + 空输出"，被当成"已安装但版本解析失败"。
            set(kVswherePath, {"-latest", "-products", "*", "-property", "installationVersion"});
            break;
        case YukinaMinato::Kubectl:
            set("kubectl", {"version", "--client"});
            break;
        case YukinaMinato::PowerCfgActive:
            set("cmd.exe", {"/C", "chcp", "65001>nul&powercfg", "/getactivescheme"});
            break;
        case YukinaMinato::WevtutilSystemErrors:
            set("cmd.exe", {"/C", "chcp", "65001>nul&wevtutil", "qe", "System",
                            "/q:*[System[(Level=2)]]", "/c:50", "/rd:true", "/f:XML"});
            break;
        case YukinaMinato::DockerCompose:
            set("docker", {"compose", "version", "--short"});
            break;
        case YukinaMinato::PsDiskHealth:
            set("powershell", {"-NoProfile", "-Command",
                               "[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
                               "Get-CimInstance Win32_DiskDrive | ForEach-Object { "
                               "\"$($_.Model)|$($_.Status)\" }"});
            break;
        case YukinaMinato::Ffmpeg:
            set("ffmpeg", {"-version"});
            break;
        case YukinaMinato::Clang:
            set("clang", {"--version"});
            break;
        case YukinaMinato::Clangxx:
            set("clang++", {"--version"});
            break;
        case YukinaMinato::Cmake:
            set("cmake", {"--version"});
            break;
        case YukinaMinato::Ninja:
            set("ninja", {"--version"});
            break;
        case YukinaMinato::Lua:
            set("lua", {"-v"});
            break;
        case YukinaMinato::Luajit:
            set("luajit", {"-v"});
            break;
        case YukinaMinato::Luarocks:
            set("luarocks", {"--version"});
            break;
        case YukinaMinato::Javac:
            set("javac", {"-version"});
            break;
        case YukinaMinato::Mvn:
            set("mvn", {"-version"});
            break;
        case YukinaMinato::Gradle:
            set("gradle", {"--version"});
            break;
        case YukinaMinato::VswhereVc:
            set(kVswherePath, {"-latest", "-products", "*", "-requires",
                               "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                               "installationVersion"});
            break;
        case YukinaMinato::PythonInfo:
            set("python", {"-c", kPythonInfoScript});
            break;
        case YukinaMinato::PythonStartupPing:
            set("python", {"-c", "pass"});
            break;
        // `where.exe` 与 `py` 都按"字面量程序名 + 由 nekomata_okayu 解析绝对路径"处理：
        // 裸名字交给 CreateProcess 的搜索顺序（含当前工作目录）会让被扫描目录里的
        // 同名 exe 被执行，这正是旧实现在 Windows 上先 shutil.which 的原因。
        case YukinaMinato::WherePython:
            set("where.exe", {"python"});
            break;
        case YukinaMinato::PyLauncherList:
            set("py", {"-0p"});
            break;
        case YukinaMinato::PythonPipVersion:
            set("python", {"-m", "pip", "--version"});
            break;
        case YukinaMinato::PythonPipListFreeze:
            set("python", {"-m", "pip", "list", "--format=freeze"});
            break;
        case YukinaMinato::PythonPipListOutdated:
            set("python", {"-m", "pip", "list", "--outdated", "--format=json"});
            break;
        case YukinaMinato::PythonPipConfigList:
            set("python", {"-m", "pip", "config", "list"});
            break;
        case YukinaMinato::PythonPipCacheDir:
            set("python", {"-m", "pip", "cache", "dir"});
            break;
        case YukinaMinato::PythonPipCheck:
            set("python", {"-m", "pip", "check"});
            break;
        case YukinaMinato::PythonImportScan:
            set("python", {"-c", kImportScanScript});
            break;
        case YukinaMinato::PythonImportPip:
            set("python", {"-c", kColdImportPip});
            break;
        case YukinaMinato::PythonImportSetuptools:
            set("python", {"-c", kColdImportSetuptools});
            break;
        case YukinaMinato::PythonImportWheel:
            set("python", {"-c", kColdImportWheel});
            break;
        case YukinaMinato::PythonImportRequests:
            set("python", {"-c", kColdImportRequests});
            break;
        case YukinaMinato::PythonImportNumpy:
            set("python", {"-c", kColdImportNumpy});
            break;
        case YukinaMinato::PythonImportPandas:
            set("python", {"-c", kColdImportPandas});
            break;
        case YukinaMinato::PythonSslProbe:
            set("python", {"-c", kSslProbeScript});
            break;
        case YukinaMinato::PythonUrlProbe:
            // 目标地址不在参数里：脚本自己读 `ENVDOCTOR_PROBE_URL`（运行期数据不进命令行）。
            set("python", {"-c", kUrlProbeScript});
            break;
        case YukinaMinato::GitConfigIdentity:
            // 只查身份与换行这两类键：**不查值**（值属于用户身份信息，取出来就有落进报告的风险）。
            set("git", {"config", "--get-regexp", "^(user\\.(name|email)|core\\.autocrlf)$"});
            break;
        case YukinaMinato::GitConfigKeys:
            set("git", {"config", "--get-regexp",
                        "^(http\\.proxy|https\\.proxy|http\\.sslbackend|"
                        "http\\..*\\.schannelcheckrevoke|core\\.longpaths|core\\.autocrlf)$"});
            break;
        case YukinaMinato::GitStatusPorcelain:
            // 只读、且刻意带 `--no-optional-locks`：诊断工具不该在别人的仓库里留下索引锁。
            // 仓库目录不在这里 —— 走 `yaguruma_rine` 的工作目录通道（-C/路径都是运行期数据）。
            set("git", {"--no-optional-locks", "status", "--porcelain", "-z"});
            break;
    }
}

RimiUshigome anya_melfissa(YukinaMinato tool, std::chrono::milliseconds timeout) {
    std::string program;
    std::vector<std::string> args;
    kureiji_ollie(tool, &program, &args);
    return nekomata_okayu(program, args, timeout);
}

RimiUshigome yaguruma_rine(YukinaMinato tool, const std::string& cwd, std::chrono::milliseconds timeout) {
    std::string program;
    std::vector<std::string> args;
    kureiji_ollie(tool, &program, &args);
    return nekomata_okayu(program, args, timeout, cwd);
}

}  // namespace envdoctor
