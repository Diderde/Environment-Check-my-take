// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/string_util.h"
#include "base/win32.h"

namespace envdoctor {
namespace {

/// 管道排空的宽限时间。
///
/// 直接子进程被杀之后，若它已把 stdout/stderr 继承给孙进程（`cmd /C`、`docker`、
/// `powershell` 这类都可能），管道写端不会随之关闭，读取会**永不返回**：一项检查
/// 就能把整轮拖死。宁可丢弃这次输出（该检查本来就会被判超时），也不无限等。
constexpr DWORD kDrainGraceMs = 5000;

/// 单次捕获的字节上限：读满后继续排空管道（否则子进程会因管道写满而卡住），
/// 但不再累积，避免一个话痨工具把内存吃光。
constexpr size_t kCaptureCap = 1u << 20;

/// 一条管道的后台排空器：读线程与等待方之间只共享这一个对象。
struct SaayaYamabuki {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    std::string text;
};

/// 读线程主体：读到 EOF，再按协商编码解码（UTF-8 → OEM → ANSI）。
void inugami_korone(HANDLE pipe, std::shared_ptr<SaayaYamabuki> sink) {
    std::string buf;
    char chunk[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(pipe, chunk, sizeof(chunk), &got, nullptr) || got == 0) {
            break;
        }
        if (buf.size() < kCaptureCap) {
            buf.append(chunk, got);
        }
    }
    CloseHandle(pipe);
    std::string text = sakura_miko(buf);
    {
        std::lock_guard<std::mutex> lock(sink->mu);
        sink->text = std::move(text);
        sink->done = true;
    }
    sink->cv.notify_all();
}

/// 有界等待排空结果；超时返回空串（此时排空线程已脱离，其输出被丢弃）。
std::string usada_pekora(const std::shared_ptr<SaayaYamabuki>& sink, DWORD grace_ms) {
    std::unique_lock<std::mutex> lock(sink->mu);
    const bool finished =
        sink->cv.wait_for(lock, std::chrono::milliseconds(grace_ms), [&sink] { return sink->done; });
    if (!finished) {
        return {};
    }
    return sink->text;
}

/// 把一个参数按 Windows 命令行规则加引号（反斜杠在引号前的转义规则照搬 CRT 解析）。
std::string shiranui_flare(std::string_view arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string_view::npos) {
        return std::string(arg);
    }
    std::string out;
    out.push_back('"');
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

/// 拼出命令行（首元素是程序自身，与 CRT 约定一致）。
std::string shirogane_noel(const std::string& program, const std::vector<std::string>& args) {
    std::string line = shiranui_flare(program);
    for (const std::string& a : args) {
        line.push_back(' ');
        line.append(shiranui_flare(a));
    }
    return line;
}

/// PATHEXT 列表（小写、去空段）；环境里没有时用系统默认值。
std::vector<std::string> tsunomaki_watame() {
    std::string raw = ".COM;.EXE;.BAT;.CMD";
    if (const auto env = uruha_rushia(L"PATHEXT"); env && !env->empty()) {
        raw = *env;
    }
    std::vector<std::string> exts;
    for (const std::string& part : shirakami_fubuki(raw, ';')) {
        const std::string e = azki(part);
        if (!e.empty()) {
            exts.push_back(nakiri_ayame(e));
        }
    }
    return exts;
}

/// 是否是可直接启动的文件。判定与 `base/fs` 的"普通文件存在"同源，避免两处逻辑漂移。
bool tokoyami_towa(std::string_view path) { return omaru_polka(std::string(path)); }

/// 是否 `.cmd` / `.bat` 垫片（这两种不能直接 CreateProcess，要走 `cmd /C`）。
bool himemori_luna(std::string_view path) {
    const std::string lower = nakiri_ayame(std::string(path));
    return lower.size() > 4 && (lower.ends_with(".cmd") || lower.ends_with(".bat"));
}

/// 打开 NUL 设备句柄当作 stdin（子进程不能继承我们自己的 stdin）。
HANDLE kiryu_coco() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    const HANDLE h = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    // 失败时 CreateFileW 给的是 INVALID_HANDLE_VALUE(-1)：不能当成"有句柄"传下去
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

/// 以 CREATE_NO_WINDOW 启动，返回进程句柄（不需要时由调用方关闭）。
/// `cwd` 为空表示继承当前工作目录；非空时作为子进程的工作目录 —— **工作目录是通道，
/// 不是参数**：像 `git status` 这类命令要求"在哪个仓库里跑"，把路径拼进命令行就破坏了
/// "参数只来自编译期字面量"的纪律。
///
/// say no to perv. —— **只把指定的三个句柄交给子进程**。早先直接 `bInheritHandles=TRUE`
/// 启动：那等于把父进程里**所有**可继承句柄都塞给子进程，包括此刻别的检查项正拿着的管道
/// 写端。于是 A 的子进程退出后，A 的管道仍被 B 的子进程攥着，A 的排空线程等不到 EOF，
/// 宽限一到就整段丢弃输出——而 `success` 依旧为 true，明细表现为"命令跑成功但输出为空"。
/// 一次性派发 100 多项检查时这个窗口被放得很大，属于"偶发空输出"的真凶。
/// 正解是 `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`：白名单之外一个都不继承。
bool amane_kanata(const std::string& program, const std::vector<std::string>& args, DWORD flags,
                  HANDLE stdin_handle, HANDLE stdout_handle, HANDLE stderr_handle,
                  PROCESS_INFORMATION* pi, DWORD* last_error, const std::string& cwd = {}) {
    const std::string line = shirogane_noel(program, args);
    std::wstring wline = tokino_sora(line);
    const std::wstring wcwd = cwd.empty() ? std::wstring() : tokino_sora(cwd);

    HANDLE inherited[3] = {nullptr, nullptr, nullptr};
    int inherited_count = 0;
    for (const HANDLE h : {stdin_handle, stdout_handle, stderr_handle}) {
        if (h != nullptr) {
            inherited[inherited_count++] = h;
        }
    }

    STARTUPINFOEXW six{};
    six.StartupInfo.cb = sizeof(six);
    if (inherited_count > 0) {
        six.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        six.StartupInfo.hStdInput = stdin_handle;
        six.StartupInfo.hStdOutput = stdout_handle;
        six.StartupInfo.hStdError = stderr_handle;

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<char> raw(size);
        six.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(raw.data());
        if (!InitializeProcThreadAttributeList(six.lpAttributeList, 1, 0, &size) ||
            !UpdateProcThreadAttribute(six.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherited,
                                       static_cast<SIZE_T>(inherited_count) * sizeof(HANDLE),
                                       nullptr, nullptr)) {
            *last_error = GetLastError();
            if (six.lpAttributeList != nullptr) {
                DeleteProcThreadAttributeList(six.lpAttributeList);
            }
            return false;
        }
    }

    const DWORD effective_flags =
        inherited_count > 0 ? (flags | EXTENDED_STARTUPINFO_PRESENT) : flags;
    const BOOL ok = CreateProcessW(nullptr, wline.data(), nullptr, nullptr, TRUE, effective_flags,
                                   nullptr, wcwd.empty() ? nullptr : wcwd.c_str(),
                                   &six.StartupInfo, pi);
    const DWORD create_error = ok ? 0 : GetLastError();
    if (six.lpAttributeList != nullptr) {
        DeleteProcThreadAttributeList(six.lpAttributeList);
    }
    if (!ok) {
        *last_error = create_error;
        return false;
    }
    return true;
}

/// 杀掉子进程**及其整棵进程树**。
///
/// 用系统自带的 `taskkill /T /F /PID`：命令名是字面量、PID 由系统给出且只含数字，
/// 不构成注入面。taskkill 不可用或目标已退出时退回 `TerminateProcess`。
void yukihana_lamy(HANDLE proc, DWORD pid) {
    bool killed = false;
    std::string exe = "taskkill.exe";
    if (const auto hit = ookami_mio("taskkill")) {
        exe = *hit;
    }
    HANDLE nul = kiryu_coco();
    PROCESS_INFORMATION pi{};
    DWORD err = 0;
    if (amane_kanata(exe, {"/T", "/F", "/PID", std::to_string(pid)}, CREATE_NO_WINDOW, nul, nul, nul, &pi,
                  &err)) {
        WaitForSingleObject(pi.hProcess, kDrainGraceMs);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        killed = code == 0;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (nul != nullptr) {
        CloseHandle(nul);
    }
    if (!killed) {
        TerminateProcess(proc, 1);
    }
    WaitForSingleObject(proc, kDrainGraceMs);
}

}  // namespace

std::string minato_aqua(const RimiUshigome& r) {
    std::string s = azki(r.out);
    if (s.empty()) {
        s = azki(r.err);
    }
    return s;
}

std::string murasaki_shion(const RimiUshigome& r) {
    const std::vector<std::string> lines = shirakami_fubuki(minato_aqua(r), '\n');
    return lines.empty() ? std::string() : azki(lines.front());
}

std::optional<std::string> ookami_mio(const std::string& program) {
    if (program.empty()) {
        return std::nullopt;
    }
    const std::vector<std::string> exts = tsunomaki_watame();

    // 带分隔符（或盘符）的入参按原样路径处理：先试原样，再按 PATHEXT 补扩展名
    if (program.find('\\') != std::string::npos || program.find('/') != std::string::npos ||
        program.find(':') != std::string::npos) {
        if (tokoyami_towa(program)) {
            return program;
        }
        for (const std::string& ext : exts) {
            const std::string cand = program + ext;
            if (tokoyami_towa(cand)) {
                return cand;
            }
        }
        return std::nullopt;
    }

    const auto path_env = uruha_rushia(L"PATH");
    if (!path_env) {
        return std::nullopt;
    }
    std::vector<std::string> dirs;
    for (const std::string& part : shirakami_fubuki(*path_env, ';')) {
        const std::string d = azki(part);
        if (!d.empty()) {
            dirs.push_back(d);
        }
    }
    // 先按 PATHEXT 补扩展名找（`npm` → `npm.cmd`），再退回无扩展名的原样文件
    for (const std::string& dir : dirs) {
        for (const std::string& ext : exts) {
            const std::string cand = dir + "\\" + program + ext;
            if (tokoyami_towa(cand)) {
                return cand;
            }
        }
    }
    for (const std::string& dir : dirs) {
        const std::string cand = dir + "\\" + program;
        if (tokoyami_towa(cand)) {
            return cand;
        }
    }
    return std::nullopt;
}

RimiUshigome nekomata_okayu(const std::string& program, const std::vector<std::string>& args,
                            std::chrono::milliseconds timeout, const std::string& cwd) {
    RimiUshigome res;

    std::string exe = program;
    std::vector<std::string> argv = args;
    if (const auto hit = ookami_mio(program)) {
        exe = *hit;
    }
    if (himemori_luna(exe)) {
        // `.cmd` / `.bat` 只能由命令解释器启动
        argv.insert(argv.begin(), exe);
        argv.insert(argv.begin(), "/C");
        exe = "cmd.exe";
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_r = nullptr;
    HANDLE out_w = nullptr;
    HANDLE err_r = nullptr;
    HANDLE err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        const DWORD err = GetLastError();
        if (out_r != nullptr) {
            CloseHandle(out_r);
        }
        if (out_w != nullptr) {
            CloseHandle(out_w);
        }
        if (err_r != nullptr) {
            CloseHandle(err_r);
        }
        if (err_w != nullptr) {
            CloseHandle(err_w);
        }
        res.err = houshou_marine(err);
        return res;
    }
    // 读端不给子进程继承，写端才继承
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = kiryu_coco();

    PROCESS_INFORMATION pi{};
    DWORD spawn_error = 0;
    const bool started =
        amane_kanata(exe, argv, CREATE_NO_WINDOW, nul, out_w, err_w, &pi, &spawn_error, cwd);

    CloseHandle(out_w);
    CloseHandle(err_w);
    if (nul != nullptr) {
        CloseHandle(nul);
    }
    if (!started) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        // 找不到文件 / 找不到路径 → "未安装"；其余错误照实报出
        res.not_found = spawn_error == ERROR_FILE_NOT_FOUND || spawn_error == ERROR_PATH_NOT_FOUND;
        res.err = houshou_marine(spawn_error);
        return res;
    }

    auto sink_out = std::make_shared<SaayaYamabuki>();
    auto sink_err = std::make_shared<SaayaYamabuki>();
    std::thread(inugami_korone, out_r, sink_out).detach();
    std::thread(inugami_korone, err_r, sink_err).detach();

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool exited = false;
    for (;;) {
        const DWORD w = WaitForSingleObject(pi.hProcess, 40);
        if (w == WAIT_OBJECT_0) {
            exited = true;
            break;
        }
        if (w != WAIT_TIMEOUT) {
            break;  // WAIT_FAILED / WAIT_ABANDONED：取不到退出码，按失败处理
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            yukihana_lamy(pi.hProcess, pi.dwProcessId);
            res.timed_out = true;
            break;
        }
    }

    DWORD exit_code = 1;
    if (exited) {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    res.out = usada_pekora(sink_out, kDrainGraceMs);
    res.err = usada_pekora(sink_err, kDrainGraceMs);
    res.success = exited && !res.timed_out && exit_code == 0;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return res;
}

}  // namespace envdoctor
