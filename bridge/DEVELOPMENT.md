# Bridge 开发指南

安装、日常操作和故障恢复见[用户指南](README.md)。会话归属、独占调度和持久化规则以[接入架构](../docs/design/open-device-access.md)为准。

## 从源码运行

Windows 在仓库根目录执行：

```powershell
cd bridge
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -e .
```

macOS：

```bash
cd bridge
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e .
source .venv/bin/activate
```

安装后可用 `quotaframe-bridge` 或 `python -m quotaframe_bridge` 运行控制台形态。macOS 源码菜单栏入口需要显式指定 Release 仓库（源码树不嵌入生产 channel JSON）：

```bash
export QUOTAFRAME_RELEASE_REPO=eMUQI/QuotaFrame
python -m quotaframe_bridge.ui.macos.app
```

这个环境变量只用于源码开发或恢复测试；冻结 `.app` 会在构建时嵌入同一仓库名。Bleak 只在真实 BLE 模式下加载，因此源码测试和 `--mock --dry-run` 不依赖 BLE 设备。

### macOS 源码运行的蓝牙权限

源码控制台和菜单栏入口应从 Terminal.app 或 iTerm 启动，首次使用蓝牙时允许对应终端访问；若曾拒绝，在「系统设置 > 隐私与安全性 > 蓝牙」中开启该终端的权限。缺少蓝牙用途说明或权限归属的启动环境可能导致 CoreBluetooth 终止进程。Bridge 会对明确拒绝的授权状态给出可读错误。

此要求仅适用于源码运行。DMG 安装的应用从 Applications 启动，并授予 QuotaFrame Bridge 蓝牙权限。

`--mock --dry-run` 不访问蓝牙，不需要蓝牙权限。

## Mock 测试

无账户、无设备、无 BLE：

```powershell
python -m quotaframe_bridge --mock --dry-run
```

默认运行一个采集周期并输出两条 JSONL。多周期：

```powershell
python -m quotaframe_bridge --mock --dry-run --cycles 3 --interval 0
```

有任一受支持面板、没有真实账户时，先添加设备（只需一次），再以 Mock 数据运行：

```powershell
python -m quotaframe_bridge --add-device
python -m quotaframe_bridge --mock
```

没有已添加的设备时，不带 `--device-name` / `--name-prefix` 的运行会直接报错并提示使用 `--add-device`。`--list-devices` 列出已添加的设备，`--forget-device <地址>` 删除一条记录。

## CLI 与调度

以下命令使用源码控制台入口；完整参数见 `python -m quotaframe_bridge --help`。

同一系列有多个设备，或只想连接一台设备时，使用完整名称：

```powershell
quotaframe-bridge --device-name "<DEVICE_NAME>"
```

将 `<DEVICE_NAME>` 替换为实际设备的完整蓝牙广播名。

也可只扫描 Waveshare 设备：

```powershell
quotaframe-bridge --name-prefix QF-WS-S3-A216-
```

`--device-name` 和 `--name-prefix` 都会切换到单设备模式；默认按已添加的设备列表连接，不需要设置。

常用参数：

```text
--interval 60             Win-CodexBar 采集周期（秒）
--publish-interval 30     向设备重发最新状态的周期（秒）
--scan-timeout 10
--connect-timeout 60
--source-timeout          一次采集的上限（秒）；默认按平台，Windows 15、macOS 90
--ack-timeout 5
--log-level INFO
```

完成加密连接并通过协议状态校验后，Bridge 会输出：

```text
BLE connected: device=Waveshare Usage Panel secure=true protocol=1 capabilities=usage.v1
```

自动重连通过相同校验后也会再次输出；未完成或校验失败的连接不会输出成功记录。

`--source-timeout` 默认 Windows 15 秒、macOS 90 秒，显式参数优先。`--interval` 从上一次采集完成后计时；`--publish-interval` 独立重发最近快照。手工刷新仍需等待采集完成，新设备连接就绪后立即接收已有快照，无快照时等待首次采集。

## 数据源接口

Bridge 调用所在平台的 CodexBar 结构化接口。两个上游的 JSON 契约不兼容，因此有两个 source adapter：

```text
Windows：codexbar-cli usage -p both --json
macOS：  codexbar usage --provider both --format json
```

随后只保留两个 provider 的 primary/secondary 窗口百分比、重置时间和采样时间，转换成 `usage.v1` 后通过加密 BLE 发送。原始 stdout/stderr、身份、费用、计划和凭据不会写入 BLE 消息或正常日志。

## 更新与发布配置

tag 去掉前导 `v` 后必须与打包版本一致。固件目录独立于 Bridge 自身更新，默认读取配置发布仓库的 `releases/latest/download/manifest.json`；启动及每 24 小时检查一次，按 `firmware_project` 与 `target` 选择候选。只有点击更新才下载镜像，核对大小、SHA-256、应用头、芯片与分区容量。开发入口 `QUOTAFRAME_FIRMWARE_MANIFEST_URL` 仍须满足受控 HTTPS 来源规则，确认框显示实际镜像来源。当前目录 schema 为 `schema_version: 1` 加 `releases` 数组，需要随发布产物一起部署。

Windows 更新下载项按当前运行的 EXE 路径与当前用户安装登记是否一致，区分安装版和便携版；源码运行、安装登记不可读或资产缺失时回退到 Release 页面。macOS 打开 Release 页面。

## 测试

不安装项目也可从仓库根目录执行：

```powershell
$env:PYTHONPATH = "bridge/src"
python -m unittest discover -s bridge/tests -v
```

macOS：

```bash
PYTHONPATH=bridge/src python3 -m unittest discover -s bridge/tests -v
```

必须从仓库根目录运行：部分测试用 `bridge.tests.test_protocol` 的绝对导入共享 fixture。

当前测试包括两个平台的 CodexBar JSON 投影、隐私字段白名单、协议、分包、ACK、状态协商、重连、数据保留、平台目录布局、平台分发和 Mock CLI。

平台专属的测试会自动跳过而不是失败：托盘、注册表自启、Tk 弹窗和 Windows 控制台取码在非 Windows 上跳过，发布脚本测试在没有 PowerShell 时跳过。反过来，配置目录布局、CodexBar 两种方言和平台分发这几组在任一平台都会把两边都跑一遍，因此在 Windows 上开发也覆盖得到 macOS 分支。

## 构建独立可执行文件

以下构建命令均从仓库根目录运行；若当前位于 `bridge/`，先执行 `cd ..`。

不需要用户安装 Python 或创建虚拟环境时，可以打包成单文件 `.exe`：

```powershell
.\bridge\build_exe.ps1 -ReleaseRepository owner/releases
```

脚本会在 `bridge/.build-venv`（不提交到仓库）里安装 `bleak`、`pystray`、`pillow` 和 `pyinstaller`，产物是 `bridge/dist/quotaframe-bridge.exe`。运行形态和控制台限制见[用户指南](README.md#托盘配对和重连)。

安装器构建复用上述 EXE：

```powershell
.\bridge\build_installer.ps1 -Python python
```

脚本在 `bridge/build/tools` 下载并校验固定版本 Inno Setup，以便携模式准备编译器，然后生成 `bridge/dist/quotaframe-bridge-windows-v<version>-setup.exe`。也可通过 `-Iscc <path>` 使用本机编译器。安装器不会默认开启自启动，升级保留已有选择；卸载仅清理指向本次安装的自启项，保留用户配置、设备记录与依赖缓存。`bridge/test_installer.ps1` 可在独立 AppId、测试自启值和项目构建目录下验证安装、覆盖升级与卸载，结束后清理测试注册项。

冻结包包含 Python、托盘组件、Windows 蓝牙后端与 CLI 所需的 VC 运行库；用户无需单独安装 Python。

Apple Silicon Mac 先构建 `.app`：

```bash
./bridge/build_app.sh --release-repository owner/releases
```

脚本要求 arm64 主机，生成 `bridge/dist/QuotaFrame Bridge.app`，创建 `.icns`，并执行 ad-hoc 签名与结构校验。再生成 DMG：

```bash
./bridge/build_dmg.sh \
  --app "bridge/dist/QuotaFrame Bridge.app" \
  --output bridge/dist/quotaframe-bridge-macos-arm64-v<version>.dmg
```

DMG 包含应用 bundle 与 Applications 快捷方式。发布产物由 `v*` tag workflow 构建；Release 资产契约与手工装配见[产品发布说明](../docs/release/FIRMWARE_RELEASE.md)。
