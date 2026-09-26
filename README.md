# Environment Check (my take)

Windows 开发环境诊断工具：跑一轮，给出可行动的环境问题清单 —— 装了哪些工具链、环境变量有没有坑、
网络与镜像通不通、本地项目仓库健不健康、系统上的 Python 解释器状态如何。

单一可执行文件，**不依赖 DLL、不依赖 Python 运行时**。三种界面同一套引擎：命令行、终端界面、图形界面。

## 构建

需要 Visual Studio 2022（含 C++ 工具集）与 CMake 3.24+。

```bash
cmake -S Cover -B Cover/build -G "Visual Studio 17 2022" -A x64
cmake --build Cover/build --config Release
```

依赖（{fmt}、doctest、FTXUI、Dear ImGui、WIL）由 CMake 在配置期从上游拉取，版本钉死在
`Cover/CMakeLists.txt`，仓库内不含其源码。只要命令行、不要两个界面时加 `-DENVDECTOR_WITH_UI=OFF`。

## 使用

双击仓库根目录的 `start.bat` 也行：找不到构建产物时它会引导构建（可只建命令行，快一些），
然后从菜单选图形界面 / 终端界面 / 全量诊断 / 列出检查项。启动器自身的文字是纯 ASCII
（bat 的解析依赖当前代码页，混入非 ASCII 在 UTF-8 终端下会被读坏），程序界面仍是中文。

```bash
envdoctor                                   # 全量诊断，按类别折叠显示（无参即跑，等同于 run）
envdoctor --list-checks                     # 列出全部检查项
envdoctor run -E                            # 展开全部明细
envdoctor run -c python -c network -e network  # 选类别并展开其中一类
envdoctor run --require git,node            # 声明必备工具：缺失记 FAIL（逗号或重复传参均可）
envdoctor run --json report.json --txt report.md
envdoctor run --net-full                    # 启用公网 IP 检查（默认关闭，见"隐私"）
envdoctor run --scan-root D:\code --scan-root E:\work   # 本地项目巡检（可重复；不给则 projects.* 记 skip）
envdoctor tui                               # 终端界面：R 运行 / C 取消 / E 全部 / X 只看问题 / S 存 JSON / Q 退出
envdoctor gui                               # 图形界面：状态芯片过滤、搜索、只看问题、导出 JSON
```

退出码：`0` 干净；`1` 有 fail 项或引擎自身异常；`2` 用法错误（含全是未知类别的 `-c`、导出写失败）；
`130` 运行中 Ctrl+C。**引擎自己挂掉不会伪装成"环境没问题"**，可直接用于 CI 与脚本。

`--timeout SECONDS`（默认 25，下限 1）是**单项**预算；**整轮**预算 = 单项预算 × 检查项数。各检查项
自身的子进程超时是各自固定的（如工具链探针 10s、NTP 15s、netsh 8s）。

`--scan-root` 启用 `projects.*` 三项**工作区巡检**（项目清点 / 仓库健康度 / 跨项目依赖）：它诊断的是
**工作区**而不是机器，所以扫描范围必须显式给出，不给就三项一律 `skip`（既不动文件系统也不调用 git）。
扫描有全局预算、深度与仓库数上限，被截断时明细里标注"为下界"；三项共享同一次扫描。

## 报告契约

- `report_version` = 1；顶层 8 个键；每条结果恰好 8 个字段（`id`/`title`/`category`/`status`/`detail`/`hint`/`duration_ms`/`error`）；
- 状态只有 6 个取值：`ok` / `warn` / `fail` / `skip` / `info` / `timeout`；
- 结果按 `(category, id)` 稳定排序；`detail` 一行一条；
- `--json` 与 Markdown 导出统一 UTF-8、CRLF、无 BOM，JSON 末尾不追加换行；
- 控制台输出按当前代码页编码（重定向时用系统 ANSI 代码页），中文 Windows 下把 emoji/箭头降级为 ASCII 图标，
  因此 `envdoctor run > report.txt` 与管道不会因编码异常中断。

"取不到"与"没有"在类型上分开：读不到配置、探针超时、命令没跑起来，一律记 `skip` 并说明原因，
不写成"未安装/未配置"这类结论。

## 隐私

- 默认不发起任何包含设备信息的外发请求；公网 IP 查询需显式 `--net-full`；
- 报告里不出现用户名与序列化凭据：代理地址、`PIP_INDEX_URL` 等含 `user:pass@` 的值统一打码
  （按**最后一个** `@` 切分）；
- 路径中的用户主目录统一写作 `%USERPROFILE%`；用例名/邮箱/主机名/内网地址一律不进报告
  （hosts 只报自定义记录条数；git 身份只报"是否已配置"）；
- `projects.*` 巡检只报**编号与统计**：仓库按"扫描根顺序 + 根内路径字典序"编号（可复现），
  只有依赖名会回显，git 远端 URL 从不进入报告；`git status` 附加 `--no-optional-locks`，巡检全程不修改任何仓库状态。

## 平台适配（Windows）

- 工具名按 **PATHEXT** 解析（`npm` 实际是 `npm.cmd`），`.cmd`/`.bat` 垫片经 `cmd /C` 启动；
- 子进程超时用 `taskkill /T` 收掉**整棵进程树**，并给输出排空加上界 —— 孙进程继承管道写端时不会挂死；
- 子进程**只继承指定的三个句柄**（`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`）：并发派发上百项检查时，
  不会出现"别的检查攥着我的管道、我的输出被丢弃"；
- 所有子进程附加 `CREATE_NO_WINDOW`；需要固定输出编码的命令（netsh/powercfg/wevtutil）前置 `chcp 65001`。

## 测试

```bash
Cover/build/Release/envdoctor_tests.exe     # 单元测试（不依赖宿主装没装某个工具）
```

## 许可

MIT，见 `LICENSE`。第三方依赖清单见 [`THIRD_PARTY_LICENSES.md`](./THIRD_PARTY_LICENSES.md)（全部为 MIT 兼容许可）。

---

## 原件存档 / Original archive

> [`Original File.py`](./Original%20File.py)

这是我的早期作品之一，那时候我还是一个甚至会在全局环境里进行开发的傻子。

当时我常常抱怨自己的环境问题，所以有了这个，毫无疑问，这是一坨彻彻底底的狗屎。

所以，我觉得有必要把这个原件公开，希望作为一个反面教材来提供学习。

This is one of my early works, from a time when I was still a fool who would do development right inside the global environment.
It is kept here as a cautionary tale.
