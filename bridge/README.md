# QuotaFrame Bridge

QuotaFrame 的电脑端配套应用，将 CodexBar / Win-CodexBar 获取的 Codex 和 Claude 用量通过蓝牙同步到独立桌面屏。

Windows 上发布给日常使用者的 Bridge 是托盘常驻程序：双击`quotaframe-bridge.exe` 后会出现托盘图标，右键图标可打开菜单。左键单击图标可切换所有已连接且支持该功能的设备的时钟屏保；是否支持以设备在安全 status 中通告的 `screen.toggle.v1` 为准。未连接、不支持或正在升级的设备会跳过。macOS 提供菜单栏应用。源码调试与无设备验证也可使用控制台入口 `python -m quotaframe_bridge`，见[开发指南](DEVELOPMENT.md)。

Bridge 仅同步用量百分比、重置时间和采样时间，不向设备发送账号身份、费用、计划或凭据。

## 环境

| | Windows | macOS |
| --- | --- | --- |
| 系统 | Windows 10/11 | macOS 14 或更新（上游 CodexBar 要求） |
| Python（仅源码运行） | 3.11 或更新 | 3.11 或更新 |
| 形态 | 托盘常驻 + 控制台 | 菜单栏常驻 + 控制台 |
| 数据源 | [Win-CodexBar](https://github.com/nesszer/Win-CodexBar) | [CodexBar](https://github.com/steipete/CodexBar) 及其 CLI |
| 配对 | Bridge 主动发起 WinRT PIN 配对 | 由 macOS 自己弹窗，Bridge 不参与 |
| 解除配对 | `--repair-pairing` | 只能去「系统设置 > 蓝牙 > 忘记此设备」 |

macOS 提供控制台与菜单栏形态，Release workflow 生成带图标和原生通知的 Apple Silicon DMG。Intel、Developer ID 签名与 notarization 尚未提供；硬件覆盖见[验证范围](../docs/verification.md)。

### macOS 菜单栏形态

从 Applications 启动 `QuotaFrame Bridge.app`，首次使用蓝牙时授予应用权限。菜单栏图标显示连接状态：

| 状态 | 图标 |
| --- | --- |
| 正常 | 屏幕轮廓 + 用量条 |
| 注意（掉线） | 实心反相 |
| 未就绪（还没连上过） | 正常图标 + 系统变暗 |

菜单栏快捷操作：左键单击图标切换兼容设备的时钟／额度界面；右键或 Control-单击打开菜单；在图标上上下滚动翻看额度页。菜单也提供「切换时钟 / 额度」「上一页额度」「下一页额度」。操作会发送到所有已连接且支持对应能力的设备，未连接、不兼容或正在升级的设备会跳过。

固件更新开始后，设备的固件菜单项显示「取消此次更新」，准备和传输期间均可点击。桌面确认框也可直接取消。取消后 Bridge 停止任务并执行设备端尽力清理；最后一个数据块确认后，菜单显示「正在确认更新结果」，此时不再提供取消操作。

滚动仅处理图标范围内的纵向事件，触控板惯性滚动不触发翻页，连续操作最多每 200 ms 翻一页。

菜单按已添加的设备显示固件更新项，包含新版提示、等待设备确认、传输百分比和重连状态。点击后下载并校验固件，在桌面确认设备、来源、版本和摘要，再通过蓝牙更新；设备仍需逐次物理确认。下载与桌面确认不占独占连接，期间继续同步用量。macOS OTA 软件流程已接入，实机升级、重连和数据恢复仍待验收。

**「重新配对…」只弹说明**：macOS 不提供解绑 API，只能引导去蓝牙设置。

Release `.app` 使用 macOS Notification Center 发送原生通知；用户拒绝通知权限或系统通知不可用时，Bridge 保留日志与菜单状态，不影响数据推送和手工打开更新页。

可在菜单中开启「登录时启动」。

## 安装

### Release 安装

安装包从[产品 Release](https://github.com/eMUQI/QuotaFrame/releases/latest) 下载。

Windows 安装版文件名为 `quotaframe-bridge-windows-v<version>-setup.exe`，默认安装到 `%LOCALAPPDATA%\Programs\QuotaFrame`，无需管理员权限。安装器提供开始菜单入口与卸载入口；覆盖安装保留配置、设备记录和自启动选择。安装时按向导关闭正在运行的 Bridge，再完成文件替换；从免安装版迁移时先退出该副本。

免安装版 `quotaframe-bridge-windows-v<version>.exe` 仍可直接运行，两种形态共用 CLI 依赖准备逻辑。缺少 CLI 时需要联网下载；账号登录仍由对应数据源负责。

Apple Silicon macOS 打开`quotaframe-bridge-macos-arm64-v<version>.dmg`，把`QuotaFrame Bridge.app` 拖入 Applications 并启动。当前包只做 ad-hoc 签名，没有 Developer ID 签名或 notarization，启动若被 Gatekeeper 拦截，打开「系统设置 > 隐私与安全性」，点击该应用对应的「仍要打开」，再确认「打开」。当前没有 Intel 包。

源码环境、控制台入口、Mock、CLI 参数、测试与打包见 [Bridge 开发指南](DEVELOPMENT.md)。

## Bridge 更新检查

Bridge 启动后检查一次产品 Release，之后每 24 小时检查，也可从托盘或菜单栏手工检查。它只接受经过校验的公开 Release：稳定安装只发现比自身更新的稳定版本；alpha、beta 或 rc 安装可以发现比自身更新的 prerelease 或稳定版本。

更新检查空闲时保持“检查 Bridge 更新”可点击，手工请求期间显示不可点击的“正在检查…”，完成或失败后恢复。

发现新版时，Bridge 更新菜单状态并发出通知。手工检查会反馈结果；自动检查对同一版本在每次运行期间只通知一次。点击 Windows 通知会打开托盘菜单，点击下载项才会在浏览器中打开下载链接。

- Windows 安装版：选择“下载安装版”，下载后运行安装器覆盖升级。
- Windows 便携版：选择“下载便携版”，退出 Bridge 后替换原 EXE；保留原路径和文件名，使已有自启动项继续有效。
- macOS：打开 Release 页面下载新 DMG，退出 Bridge 后替换 Applications 中的应用。
- 若打开的是 Release 页面，请按当前使用的平台和安装形态选择资产。

Bridge 不在后台下载、自行替换或执行自己的安装包。

固件目录与 Bridge 自身更新独立，启动及每 24 小时检查一次。只有点击设备更新项才下载并校验镜像；确认框显示实际镜像来源。

固件更新菜单只对通告 `ota.folder.v1` 的设备启用；仓库维护的固件目标都通告该能力。各目标的 OTA 实机验收范围见 [verification.md](../docs/verification.md)。

## macOS 采集排查

采集缓慢时，先单独检查 Claude OAuth 数据源：

```bash
codexbar usage --provider claude --source oauth --format json
```

若命令失败，检查 CodexBar 的账号授权和数据源配置；采集耗时取决于数据源和认证方式。

手工刷新仍需等待采集完成。采集超时和刷新周期的 CLI 设置见[开发指南](DEVELOPMENT.md#cli-与调度)。

## 数据源与配置

Bridge 会按以下顺序寻找并验证 CodexBar CLI：

1. `--codexbar-cli` 显式指定的路径；
2. `%APPDATA%\quotaframe\config.toml` 中的 `codexbar_cli`；
3. Win-CodexBar 默认安装路径 `%LOCALAPPDATA%\Programs\CodexBar\codexbar-cli.exe`；
4. QuotaFrame 管理的固定版本 CLI（校验摘要后使用）；
5. 当前进程可见的 PATH，依次检查 `codexbar-cli.exe`、`codexbar-cli`、`codexbar.exe` 和 `codexbar`。

每个候选都会先执行本地 `--version`，要求输出以 `codexbar x.y.z` 开头，再执行 `usage -p both --json` 并通过当前 JSON 解析器校验。桌面端或其他同名程序不会仅因位于 PATH 就被使用。候选失败时会自动尝试下一个；第一次成功后会复用该路径，若它后来失效则重新寻找。

Windows 在没有可用 CLI、且未通过参数或配置手动指定路径时，自动下载官方 `v1.2.12` 的 `codexbar.exe`，保存为 `%LOCALAPPDATA%\quotaframe\dependencies\codexbar\1.2.12\codexbar-cli.exe`。下载大小与 SHA-256 固定在 `sources/codexbar_dependency.py`，通过校验后才启用，后续运行复用本机副本。下载失败会提示并限频重试；已有 CLI 的取数或凭据失败不会触发下载。可继续使用 `codexbar_cli` 指定本机 CLI，不会覆盖 Win-CodexBar 的安装目录。

冻结包包含该 CLI 所需的 VC 运行库；首次安装与账号登录的验收范围见验证说明。自动下载不包含 Win-CodexBar 桌面安装流程，也不会替用户登录账号。

不需要任何配置时，若 CLI 在 PATH 或默认安装位置：

```powershell
quotaframe-bridge
```

也可以用一个可选的 TOML 文件固定路径并设置托盘进程的日志级别：

```toml
# %APPDATA%\quotaframe\config.toml
codexbar_cli = 'D:\tools\codexbar-cli.exe'
log_level = 'INFO'
```

配置文件缺失、格式错误或路径不可用时会继续自动寻找。命令行参数优先级最高，如果显式路径不存在或校验失败，Bridge 会直接报错，不会自动回退到其他路径。`log_level` 可为 `DEBUG`、`INFO`、`WARNING` 或 `ERROR`，无效或缺失时使用`INFO`。托盘程序把日志写到`%LOCALAPPDATA%\quotaframe\bridge.log`，与 macOS 的`~/Library/Logs/quotaframe/bridge.log` 同由 `paths.log_directory` 决定。

也可以显式指定：

```powershell
quotaframe-bridge --codexbar-cli "C:\path\to\codexbar-cli.exe"
```

找不到任何候选、找到的程序都不是兼容 CLI、或找到 CLI 但都无法解析用量时，Bridge 分别给出对应的脱敏错误提示；提示不会包含候选程序的 stdout、stderr、原始 JSON、账户信息或凭据。

### 弹窗主题

Windows 弹窗跟随系统设置中的“默认应用模式”（浅色/深色），每次打开弹窗时生效，无需重启 Bridge；已经打开的弹窗保持原配色。macOS 使用原生系统弹窗。

### 桌面语言 / Desktop language

在 `config.toml` 的顶层设置 `language = 'auto'`（默认）、`'zh'` 或 `'en'`：

- Windows：`%APPDATA%\quotaframe\config.toml`
- macOS：`~/Library/Application Support/quotaframe/config.toml`

`auto` 读取系统首选界面语言，中文变体使用简体中文，其余语言或检测失败使用英文；日期、数字的地区格式不参与判断。缺失或无效配置按 `auto` 处理。设置在下次启动 Bridge 时生效，菜单、通知和对话框使用同一种语言。请在方便时自行退出并重新打开 Bridge。

Set top-level `language = 'auto'`, `'zh'` or `'en'` in the configuration file above. The default `auto` follows the primary system UI language: Chinese variants use Simplified Chinese; other languages or detection failures use English. Regional date/number formats do not affect this choice. Changes take effect the next time Bridge starts.

本设置仅影响桌面 Bridge；设备屏幕、系统蓝牙配对窗口和诊断日志保留各自的语言。

## 设备连接

Bridge 为每个已认领地址维护独立连接，允许同型号多台和实现公开契约的第三方设备。首次添加通过菜单“添加设备”或 `quotaframe-bridge --add-device` 主动发起；空设备列表不扫描配对。候选必须同时具有 `QF-` 名称和 NUS 服务。

某台设备等待配对码或重连时，其他设备继续接收更新。

默认按已添加的设备列表连接。单设备筛选、命令行添加和诊断参数见[开发指南](DEVELOPMENT.md)。

Bridge 会为支持时间同步的设备同步本地时间。

## 托盘、配对和重连

发布版 `quotaframe-bridge.exe` 无控制台并常驻在系统托盘。PIN 码、重新配对确认和“QuotaFrame Bridge 已在运行”提示均通过 GUI 弹窗显示；右键托盘图标可刷新、重新配对、打开日志或退出。菜单中的“开机自启”默认关闭，只有用户勾选后才为当前用户创建自启动项。`--mock` 和 `--dry-run` 需要标准输出，必须使用源码控制台形态`python -m quotaframe_bridge`，不能用于发布版托盘可执行文件。

Windows 配对时，在图形对话框中输入设备屏幕上的六位码。配对码不会写入日志或文件；只有完成安全连接与协议校验后，Bridge 才开始推送数据。

如果 Windows 以前留下了“电脑显示已配对、设备却没有 Bond”的异常记录，执行一次：

```powershell
python -m quotaframe_bridge --mock --device-name "<DEVICE_NAME>" --repair-pairing
```

将 `<DEVICE_NAME>` 替换为实际设备的完整蓝牙广播名。上述命令需先按[开发指南](DEVELOPMENT.md#从源码运行)准备源码环境；`--mock` 使用示例用量，仍会连接真实设备。

默认多设备模式不接受未限定目标的 `--repair-pairing`。必须同时使用`--device-name` 或 `--name-prefix`，避免误删另一台设备的 Windows Bond。`--repair-pairing` 只在本次进程中移除一次 Windows 记录并重新配对。每次提示都必须输入设备当前显示的新码；失败重试后不要复用上一组。修复后可去掉修复参数继续验证 Mock 连接：

```powershell
python -m quotaframe_bridge --mock --device-name "<DEVICE_NAME>"
```

验证完成后退出控制台并启动桌面应用，以恢复真实用量同步。

配对和连接共用 `--connect-timeout`（默认 60 秒）。同一进程中的自动重连复用同一个 Bond，不会重复删除或重复要求 PIN。多台设备都未配对时，Bridge 依次显示 PIN 提示，不会让多个图形对话框输入互相穿插；已配对或已连接的设备不需要等待另一台完成配对。

Bridge 启动后会持有当前 Windows 用户的单实例锁。若另一个`quotaframe-bridge` 已在运行，新进程会在扫描和配对前退出，避免两个进程同时操作 Windows 配对。`--help` 和参数错误仍可正常显示。

若日志持续显示：

```text
Windows pairing result: operation_already_in_progress
```

说明 Windows 仍认为该设备有一项未结束的配对或解除配对操作。Bridge 不会在此期间重复调用配对 API，而是每秒刷新该设备的 Windows 配对状态，最长等待`--connect-timeout`。如果现有操作完成并产生有效 Bond，对应设备会直接继续连接；等待发生在设备间配对锁之外，另一台已连接设备仍会继续更新。

等待超时后，日志会标明受影响的公开设备名并给出恢复提示。此时先停止 Bridge 并关闭系统的“添加设备”窗口；如果关闭再打开蓝牙仍不能恢复，可重启当前用户的 `DeviceAssociationBrokerSvc_*` 服务，或注销/重启 Windows，然后对该设备运行一次带 `--device-name` 或 `--name-prefix` 的 `--repair-pairing`。Bridge 不会自动重启 Windows 服务、切换蓝牙或删除其他设备 Bond，也不会记录输入的六位码。

断开或连接失败时，Bridge 自动重连对应设备，其他设备继续同步。连接与会话隔离规则见[接入架构](../docs/design/open-device-access.md#连接与状态归属)。

若托盘显示“蓝牙清理失败，请重启 Bridge”，该设备已停止自动恢复。移除后重新添加设备不能解除此状态，需要退出并重新启动 Bridge。

## 设备记录读取失败

若托盘提示“设备记录读取失败，请查看日志”，Bridge 会暂停添加设备并保留 `devices.json`，避免覆盖原记录。日志给出文件的完整路径。退出 Bridge 后修复文件；若决定重新认领，可手动将原文件改名留存，再启动 Bridge。此操作只重置 Bridge 的设备列表，不清除系统蓝牙配对。

## 开放设备接入

第三方设备的发现、安全连接和能力要求见[公开协议](../protocol/open-device-access.md)。设备记录结构、原子持久化及会话所有权见[接入架构](../docs/design/open-device-access.md)。忘记设备仅删除本地记录并停止会话，保留系统蓝牙绑定。
