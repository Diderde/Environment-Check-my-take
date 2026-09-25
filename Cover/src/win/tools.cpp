// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "win/tools.h"

#include <string>
#include <vector>

namespace envdoctor {
namespace {

/// vswhere 的安装位置是固定的（VS 安装器自己放的），不依赖 PATH。
const char* kVswherePath = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";

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
    }
}

RimiUshigome anya_melfissa(YukinaMinato tool, std::chrono::milliseconds timeout) {
    std::string program;
    std::vector<std::string> args;
    kureiji_ollie(tool, &program, &args);
    return nekomata_okayu(program, args, timeout);
}

}  // namespace envdoctor
