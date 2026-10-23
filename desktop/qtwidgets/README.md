# AgentDock Qt Desktop

用于 [AgentDock](https://github.com/uvwt/agentdock) Go MCP 核心的原生 **Qt 6 Widgets 桌面控制中心**，包含窗口、托盘、状态检测、Go 核心进程生命周期管理，以及由旧版 Qt MCP 项目移植的 ngrok 管理器/Windows 凭据库适配器。

**不替换 AgentDock 的 MCP 工具或后端架构；Go 服务仍负责 MCP、文件、终端、设备和其他功能。** 本项目不启用 AgentDock 现有 Go 托盘程序或 WinUI 窗口。Qt 应用与原生 AgentDock 核心进程相互独立。

## 使用

1. 准备 `agentdock.exe`：推荐使用本项目根目录 Go 源码构建；需要 Go 1.26.5（Go toolchain 可能自动下载）。
2. 将 `agentdock.exe` 放在 `agentdock-qt.exe` 同目录，或在图形界面指定其绝对路径。
3. 点击 **启动核心**；应用以 `AGENTDOCK_HOST=127.0.0.1`、所选端口（默认 8765）和随机生成的 `AGENTDOCK_AUTH_TOKEN` 启动 Go 核心。随机 token 存储在 Windows Credential Manager。
4. 在客户端使用 `http://127.0.0.1:8765/mcp`，并发送 `Authorization: Bearer <Qt 界面复制的访问令牌>`。实际端口以界面为准。
5. 公网 ngrok：输入 ngrok authtoken（该凭据也存入 Windows Credential Manager）、必要时指定固定域名；在已启动并健康的 **Qt 托管核心**上点击 **启动 ngrok**。复制界面显示的 HTTPS MCP 地址。
6. 关闭窗口只会隐藏到系统托盘；右键托盘可启动/停止核心、隧道或退出。

## 安全

- **禁止把未知/无鉴权的外部 AgentDock 服务直接映射到 ngrok**：只有由本 Qt 应用启动、带安全 token、通过 `/healthz`，并验证 `/mcp` 匿名请求返回 401、正确 Bearer 请求通过鉴权的核心才可以开启公网隧道。
- Qt **不重新实现认证服务器**：`AGENTDOCK_AUTH_TOKEN` 直接交给 AgentDock 的 Go 核心；它使用原生 `internal/auth` 进行 Bearer 校验（同时保留原生 OAuth 及 NexusDock 设备配对能力）。新 Qt 凭据项仅用于安全存储本机托管核心启动所用的令牌。
- ngrok authtoken 与 MCP Bearer token 是**不同**的凭据。ngrok 令牌单独用于建立隧道。
- AgentDock 能操作宿主机文件和命令，不应把访问令牌发布到公开仓库或共享聊天。
- Qt 应用退出会停止它自己启动的 Go 服务和 ngrok；不会结束外部 AgentDock 服务。
- 原版 ngrok manager 会在 Windows 首次使用时从 ngrok 官方下载 Agent；请务必检查网络下载来源和软件版本。
- 在本地服务尚未健康时 ngrok 将保持关闭。HTTPS 公网端点可能需要客户端支持 MCP Bearer 头。
- Windows Credential Manager 中旧 Qt MCP 程序的 **ngrok authtoken** 可直接复用；AgentDock Bearer token 采用新的单独凭据条目。

## 编译

### Windows（Visual Studio 2026 + Qt 6.8.3）

```powershell
cmake -S desktop/qtwidgets -B desktop/qtwidgets/build-win -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build desktop/qtwidgets/build-win --config Release --parallel 4
```

可改用 2022 工具链的 `Visual Studio 17 2022`。使用 `desktop/qtwidgets/build_windows.ps1` 进行构建、`windeployqt` 运行库部署，并在可用时复制旁边的 `agentdock.exe`。

Go 核心构建（Go 1.26.5+）：

```powershell
go build -o agentdock.exe ./cmd/agentdock
```

独立 Qt GUI 的编译**不依赖 Go 编译是否成功**；但要运行完整 AgentDock MCP 服务，必须准备对应的 Go 核心程序。Go 的 Windows 系统安装/服务管理能力属于 AgentDock 上游已有功能，本 Qt 包装器目前只管理自己启动的核心进程。

### 自动化测试与 CI

Windows 打包：`./desktop/qtwidgets/build_windows.ps1`，输出目录 `desktop/qtwidgets/dist/`，包含 `agentdock-qt.exe`、`agentdock.exe`、Qt DLL 和平台插件。**请一起解压，不要只复制 Qt EXE。**

AgentDock 原生鉴权回归：

```powershell
./desktop/qtwidgets/tests/auth_smoke.ps1 -CorePath ./desktop/qtwidgets/dist/agentdock.exe
```

测试在临时的 `AGENTDOCK_HOME` 下运行，不读取或修改个人正式配置，验证健康检查、匿名拒绝、错误令牌拒绝、正确令牌和 MCP JSON-RPC。GitHub Actions `qtwidgets-windows.yml` 可自动构建并上传 Windows 便携 ZIP。

### Linux / macOS

Qt Widgets 桌面源码使用跨平台 Qt API，但当前凭据库持久化和 ngrok 下载路径只完成 Windows 适配。Linux/macOS 未测试，不承诺可直接发行。

## 来源

`src/ngrokmanager.*` 与 `src/credentials.*` 移植自 E7G 之前的 Qt MCP 桌面项目，并在此以 Apache-2.0 仓库许可证发布。请同时遵守上游 [AgentDock](https://github.com/uvwt/agentdock) 的 Apache-2.0 许可证。
