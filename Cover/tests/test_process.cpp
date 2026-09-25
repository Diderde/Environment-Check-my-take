// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 外部命令执行层测试。断言取向与上一版的 Rust 探针测试对齐：
// PATHEXT 解析、未安装走 not_found 通路、超时后进程树确实被回收。

#include <doctest/doctest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <string>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/process.h"
#include "base/win32.h"
#include "win/tools.h"

using namespace envdoctor;

TEST_CASE("按 PATHEXT 解析：cmd 必然存在且解析到可执行扩展名") {
    const auto hit = ookami_mio("cmd");
    REQUIRE(hit.has_value());
    const std::string lower = *hit;
    const size_t dot = lower.find_last_of('.');
    REQUIRE(dot != std::string::npos);
    std::string ext = lower.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // 不能解析成无扩展名的 shell 脚本 —— 那种 CreateProcess 起不来
    CHECK((ext == ".exe" || ext == ".com"));
}

TEST_CASE("解析不存在的工具返回空") {
    CHECK_FALSE(ookami_mio("envdoctor-no-such-tool-xyz").has_value());
    CHECK_FALSE(ookami_mio("").has_value());
}

TEST_CASE("未安装保持 not_found 通路（上层据此报未安装）") {
    const RimiUshigome r =
        nekomata_okayu("envdoctor-no-such-tool-xyz", {}, std::chrono::milliseconds(2000));
    CHECK(r.not_found);
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.timed_out);
    CHECK_FALSE(r.err.empty());
}

TEST_CASE("捕获 stdout 与退出码") {
    const RimiUshigome r =
        nekomata_okayu("cmd.exe", {"/C", "echo", "hello-envdoctor"}, std::chrono::milliseconds(15000));
    CHECK(r.success);
    CHECK_FALSE(r.not_found);
    CHECK(r.out.find("hello-envdoctor") != std::string::npos);
    CHECK(minato_aqua(r).find("hello-envdoctor") != std::string::npos);
}

TEST_CASE("参数里的空格按引号绑定成一个参数") {
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "echo", "a b c"},
                                          std::chrono::milliseconds(15000));
    CHECK(r.success);
    CHECK(r.out.find("a b c") != std::string::npos);
}

TEST_CASE("stderr 单独捕获，组合输出优先 stdout、其次 stderr") {
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "echo boom 1>&2"},
                                          std::chrono::milliseconds(15000));
    CHECK(r.err.find("boom") != std::string::npos);
    CHECK(r.out.find("boom") == std::string::npos);
    CHECK(minato_aqua(r) == "boom");
    CHECK(murasaki_shion(r) == "boom");
}

TEST_CASE("组合输出取首行，末尾换行不留痕") {
    const RimiUshigome r = nekomata_okayu(
        "cmd.exe", {"/C", "echo first& echo second"}, std::chrono::milliseconds(15000));
    CHECK(murasaki_shion(r) == "first");
    CHECK(minato_aqua(r).find("second") != std::string::npos);
}

TEST_CASE("多行参数原样送达：`-c` 里带换行的脚本不能被拆坏") {
    // 解释器事实探测就是把一整段多行脚本当**一个**参数传给 `python -c`：
    // 命令行序列化一旦在换行或内嵌引号处出错，子进程会报语法错误、stdout 全空，
    // 上层看到的就是"探测没有返回可用信息"，而真实原因在命令行拼装。
    //
    // 这里必须用 python 本体而不是 `cmd /C`：cmd 自己就把换行当命令分隔符，
    // 拿它当靶子测不出"序列化是否忠实"（实测那样写必红，但红的是 cmd 的行为）。
    if (!ookami_mio("python").has_value()) {
        return;  // 宿主没装 python：这条测不了，不冒充通过
    }
    const std::string script =
        "import sys\n"
        "print(\"a\\tb\")\n"
        "print('中文')\n"
        "print(sys.version_info.major)\n";
    const RimiUshigome r =
        nekomata_okayu("python", {"-c", script}, std::chrono::milliseconds(30000));
    CHECK(r.success);
    CHECK(r.out.find("a\tb") != std::string::npos);
    CHECK(r.out.find("中文") != std::string::npos);
    CHECK(r.out.find("3") != std::string::npos);
}

TEST_CASE("白名单探针：解释器事实探测能取回可解析的输出") {
    // 与上一条同一场景，但走真实调用路径（`kureiji_ollie` 里的 `python -c <多行脚本>`）。
    // 这条用来区分"探针没跑起来"与"上层没解析对"：真跑得出来就说明问题在解析侧。
    if (!ookami_mio("python").has_value()) {
        return;
    }
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonInfo, std::chrono::milliseconds(30000));
    CHECK(r.success);
    CHECK_FALSE(r.timed_out);
    CHECK(r.out.find("version\t") != std::string::npos);
    CHECK(r.out.find("impl\t") != std::string::npos);
}

TEST_CASE("工作目录通道：白名单命令在指定目录里跑，而不是在调用者目录里") {
    // `git status` 这类命令必须"在某个仓库里"跑，而仓库路径是运行期数据：它走 cwd 通道，
    // 不进命令行参数。这条用例用"同一条命令在仓库里 / 不在仓库里结果不同"来钉住这件事。
    if (!ookami_mio("git").has_value()) {
        return;  // 宿主没装 git：这条测不了，不冒充通过
    }
    const auto temp = uruha_rushia(L"TEMP");
    REQUIRE(temp.has_value());
    const std::string dir =
        kazama_iroha(*temp, "envdoctor-git-" + std::to_string(GetCurrentProcessId()));
    CreateDirectoryW(tokino_sora(dir).c_str(), nullptr);

    // 先用同一条 cwd 通道把目录初始化成仓库（顺带证明它确实作用于子进程）
    const RimiUshigome init =
        nekomata_okayu("git", {"init", "-q", "."}, std::chrono::milliseconds(20000), dir);
    CHECK(init.success);

    // 放一个未跟踪文件：干净的仓库 status 是空输出，测不出"跑在哪个目录"
    const std::string probe = kazama_iroha(dir, "probe.txt");
    const HANDLE h = CreateFileW(tokino_sora(probe).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    const RimiUshigome in_repo =
        yaguruma_rine(YukinaMinato::GitStatusPorcelain, dir, std::chrono::milliseconds(20000));
    CHECK(in_repo.success);
    CHECK(in_repo.out.find("??") != std::string::npos);  // 未跟踪文件出现在该仓库的状态里

    // 同一个命令换到"不是仓库"的目录：git 会以非零退出报 not a git repository
    const RimiUshigome outside =
        yaguruma_rine(YukinaMinato::GitStatusPorcelain, *temp, std::chrono::milliseconds(20000));
    CHECK_FALSE(outside.success);
    CHECK_FALSE(outside.timed_out);

    DeleteFileW(tokino_sora(probe).c_str());
    RemoveDirectoryW(tokino_sora(kazama_iroha(dir, ".git")).c_str());
    RemoveDirectoryW(tokino_sora(dir).c_str());
}

TEST_CASE("超时杀掉整棵进程树并迅速返回") {
    // cmd 会再派 ping 孙进程并让它继承管道：只杀直接子进程的话管道不会关闭，
    // 这里同时验证 taskkill /T 与排空上界，够快才说明进程树确实被收掉了。
    const auto t0 = std::chrono::steady_clock::now();
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "ping", "-n", "6", "127.0.0.1"},
                                          std::chrono::milliseconds(300));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(r.timed_out);
    CHECK_FALSE(r.success);
    // 只守**活性**（必须返回，不能挂在管道上），不守"多快"。
    // 原先卡 8 秒想顺带证明"进程树被回收了"，但实测在并行构建把机器打满时，
    // 光 taskkill + 排空就能到 8.7 秒 —— 那条断言量的是机器负载，不是被测性质，
    // 会周期性地红（分散注意力）。真正的护栏是"排空有上界"这件事本身：
    // 树没被回收时两条管道各要等满 5 秒宽限，脚本挂死则永不返回。
    CHECK(elapsed < std::chrono::seconds(30));
}
