"""Desktop copy catalog. Each entry contains Chinese and English text."""

MESSAGES: dict[str, tuple[str, str]] = {
    "toggle_screen": ("切换时钟 / 额度", "Toggle clock / usage"),
    "previous_page": ("上一页额度", "Previous usage page"),
    "next_page": ("下一页额度", "Next usage page"),
    "usage_short": ("短周期", "short"),
    "usage_week": ("周", "week"),
    "usage_used": ("已用", "used"),
    "device_metadata_failed": ("设备信息保存失败", "Device information could not be saved"),
    "firmware_official_source": ("官方目录", "Official catalog"),
    "firmware_development_source": ("开发测试来源", "Development source"),
    "firmware_confirmation": (
        "{name}\n{address}\n\n来源：{source}\n{url}\n项目：{project}\n型号：{target}\n版本：{current} → {version}\n大小：{size} 字节\nSHA-256：{sha256}",
        "{name}\n{address}\n\nSource: {source}\n{url}\nProject: {project}\nTarget: {target}\nVersion: {current} → {version}\nSize: {size} bytes\nSHA-256: {sha256}",
    ),
    "ble_cleanup_failed": (
        "蓝牙清理失败，请重启 Bridge",
        "BLE cleanup failed; restart Bridge",
    ),
    "firmware_download_timeout": (
        "固件下载超时，请检查网络后重试。",
        "Firmware download timed out. Check your network and try again.",
    ),
    "devices_unreadable": (
        "设备记录读取失败，请查看日志",
        "Device records unreadable; see log",
    ),
    "starting_devices": (
        "正在连接面板…",
        "Connecting to panels…",
    ),
    "starting_usage": (
        "正在读取用量…",
        "Reading usage…",
    ),
    "unavailable": (
        "不可用",
        "Unavailable",
    ),
    "check_bridge_update": (
        "检查 Bridge 更新",
        "Check for Bridge updates",
    ),
    "firmware_detail": (
        "固件：{detail}",
        "Firmware: {detail}",
    ),
    "repair_menu": (
        "重新配对…",
        "Pair again…",
    ),
    "forget_menu": (
        "移除设备…",
        "Remove device…",
    ),
    "refresh": (
        "立即刷新",
        "Refresh now",
    ),
    "add_device": (
        "添加设备…",
        "Add device…",
    ),
    "autostart_windows": (
        "开机自启",
        "Start with Windows",
    ),
    "open_log": (
        "打开日志",
        "Open log",
    ),
    "quit": (
        "退出",
        "Quit",
    ),
    "repair_device": (
        "重新配对 {device_name}？",
        "Pair {device_name} again?",
    ),
    "repair_body": (
        "将删除现有配对记录，需要在面板上重新读取六位配对码。",
        "The existing pairing record will be removed. Read the new six-digit code on "
        "the panel to pair again.",
    ),
    "repair": (
        "重新配对",
        "Pair again",
    ),
    "forget_device": (
        "移除 {device_name}？",
        "Remove {device_name}?",
    ),
    "forget_body": (
        "仅从 Bridge 中移除；系统蓝牙配对记录会保留。",
        "Remove from Bridge only. The system Bluetooth pairing record will be kept.",
    ),
    "forget": (
        "移除设备",
        "Remove device",
    ),
    "already_running": (
        "QuotaFrame Bridge 已在运行",
        "QuotaFrame Bridge is already running",
    ),
    "already_running_windows": (
        "查看任务栏右下角的托盘图标。",
        "Look for the tray icon in the bottom-right corner of the taskbar.",
    ),
    "ok": (
        "知道了",
        "OK",
    ),
    "pair_panel": (
        "配对面板",
        "Pair panel",
    ),
    "enter_pin": (
        "输入配对码",
        "Enter pairing code",
    ),
    "pin_instructions": (
        "在 {device_name} 屏幕上查看六位数字",
        "Read the six-digit code on {device_name}",
    ),
    "invalid_pin": (
        "配对码不正确，请重试",
        "Invalid pairing code. Try again.",
    ),
    "pair": (
        "配对",
        "Pair",
    ),
    "cancel": (
        "取消",
        "Cancel",
    ),
    "open_download_failed": (
        "打开下载页失败",
        "Could not open download page",
    ),
    "retry_later": (
        "请稍后重试。",
        "Please try again later.",
    ),
    "firmware_failed": (
        "固件更新失败",
        "Firmware update failed",
    ),
    "device_unavailable": (
        "{label} 当前不可用。",
        "{label} is currently unavailable.",
    ),
    "firmware_unavailable": (
        "固件更新不可用",
        "Firmware update unavailable",
    ),
    "firmware_not_configured": (
        "尚未配置固件 Release 地址。",
        "No firmware release URL is configured.",
    ),
    "firmware_not_supported": (
        "{label} 尚未连接或不支持 OTA。",
        "{label} is disconnected or does not support OTA.",
    ),
    "preparing": (
        "正在准备…",
        "Preparing…",
    ),
    "firmware_preparing": (
        "正在准备 {label} 固件更新",
        "Preparing firmware update for {label}",
    ),
    "firmware_preparing_body": (
        "正在获取并校验固件，完成后请在设备屏幕上确认。",
        "Downloading and verifying firmware. Confirm on the device screen when ready.",
    ),
    "firmware_cancel_preparing": ("取消此次更新（准备中）", "Cancel update (preparing)"),
    "firmware_cancel_progress": ("取消此次更新（{percent}%）", "Cancel update ({percent}%)"),
    "firmware_cancelling": ("正在取消更新…", "Cancelling update…"),
    "firmware_verifying": ("正在确认更新结果…", "Verifying update result…"),
    "firmware_cancelled": ("固件更新已取消", "Firmware update cancelled"),
    "firmware_cancelled_body": (
        "{label}：更新任务已停止。请查看设备状态，可重新发起更新。",
        "{label}: The update task has stopped. Check the device status before retrying.",
    ),
    "update_progress": (
        "更新中 {percent}%",
        "Updating {percent}%",
    ),
    "awaiting_confirmation": (
        "等待设备确认",
        "Waiting for device confirmation",
    ),
    "firmware_interrupted": (
        "{label}：更新过程意外中止。",
        "{label}: The update stopped unexpectedly.",
    ),
    "firmware_complete": (
        "固件更新完成",
        "Firmware update complete",
    ),
    "firmware_complete_body": (
        "{label} 已升级到 {version}。",
        "{label} was updated to {version}.",
    ),
    "reconnecting": (
        "正在重连",
        "Reconnecting",
    ),
    "updating": (
        "更新中",
        "Updating",
    ),
    "not_configured": (
        "未配置",
        "Not configured",
    ),
    "panel": (
        "面板",
        "Panel",
    ),
    "device_added": (
        "已添加设备",
        "Device added",
    ),
    "device_added_body": (
        "{name} 已添加。连接与用量同步状态请查看设备屏幕。",
        "{name} has been added. Check the device screen for connection and usage sync status.",
    ),
    "device_removed": (
        "已移除设备",
        "Device removed",
    ),
    "device_removed_body": (
        "{label} 不再接收用量。",
        "{label} will no longer receive usage.",
    ),
    "panel_disconnected": (
        "面板已断开",
        "Panel disconnected",
    ),
    "panel_disconnected_body": (
        "{device} 失去连接，正在尝试重连。",
        "{device} lost its connection. Trying to reconnect.",
    ),
    "panel_restored": (
        "面板已恢复",
        "Panel reconnected",
    ),
    "panel_restored_body": (
        "{device} 重新连接。",
        "{device} is connected again.",
    ),
    "pairing_required": (
        "需要配对",
        "Pairing required",
    ),
    "pairing_required_body": (
        "在面板屏幕上查看六位配对码。",
        "Read the six-digit pairing code on the panel screen.",
    ),
    "dependency_title": (
        "用量组件",
        "Usage component",
    ),
    "dependency_downloading": (
        "正在下载用量组件，完成后将自动开始读取用量。",
        "Downloading the usage component. Collection will start when it is ready.",
    ),
    "dependency_ready": (
        "用量组件已准备就绪。请确保所需账号已登录。",
        "The usage component is ready. Make sure your provider accounts are signed in.",
    ),
    "dependency_failed": (
        "用量组件下载或校验失败，请检查网络。将在五分钟后重试，也可配置本机 CLI 路径。",
        "The usage component could not be downloaded or verified. Check your network. "
        "Retrying in five minutes; you can also configure a local CLI path.",
    ),
    "startup_failed": (
        "QuotaFrame Bridge 未能启动",
        "QuotaFrame Bridge could not start",
    ),
    "startup_failed_body": (
        "点击查看日志。",
        "Click to view the log.",
    ),
    "no_devices": (
        "尚未添加设备 · 点击“添加设备…”",
        "No devices · Click “Add device…”",
    ),
    "no_devices_scanning": (
        "尚未添加设备 · 正在查找",
        "No devices · Searching",
    ),
    "connected": (
        "已连接",
        "Connected",
    ),
    "disconnected": (
        "已断开",
        "Disconnected",
    ),
    "not_found": (
        "未发现",
        "Not found",
    ),
    "single_device": (
        "1 台设备{separator}{state}",
        "1 device{separator}{state}",
    ),
    "all_connected": (
        "{count} 台设备{separator}全部已连接",
        "{count} devices{separator}All connected",
    ),
    "named_exception": (
        "{device}{separator}{connected} 已连接",
        "{device}{separator}{connected} online",
    ),
    "device_counts": (
        "{count} 台设备{separator}{connected} 已连接{separator}{disconnected} 未连接",
        "{count} devices{separator}{connected} online{separator}{disconnected} "
        "offline",
    ),
    "autostart_macos": (
        "登录时启动",
        "Open at login",
    ),
    "repair_panel": (
        "重新配对面板",
        "Pair panel again",
    ),
    "repair_macos_body": (
        "macOS 不允许应用删除配对记录。请打开「系统设置 > 蓝牙」，点击面板右侧的 "
        "ⓘ，选择「忘记此设备」，然后回到这里点「立即刷新」。面板会在约两分钟内重新连接并弹出配对码，期间屏幕显示离线属于正常。",
        "macOS does not let apps remove pairing records. Open System Settings > "
        "Bluetooth, click ⓘ next to the panel, and choose Forget This Device. Then "
        "return here and click Refresh now. The panel should reconnect and show a "
        "pairing code within about two minutes. An offline screen during this time is"
        " normal.",
    ),
    "open_bluetooth": (
        "打开蓝牙设置",
        "Open Bluetooth settings",
    ),
    "forget_macos_body": (
        "移除后不再向它推送用量，也不再自动连接它。已配对的系统蓝牙记录不受影响。\n\n要移除哪一台？",
        "Bridge will stop sending usage to this device and will no longer connect automatically. The system Bluetooth pairing record will be kept.\n\nWhich device should be removed?",
    ),
    "already_running_macos": (
        "查看屏幕右上角菜单栏的图标。",
        "Look for the icon in the menu bar at the top-right of the screen.",
    ),
    "checking_ellipsis": (
        "正在检查…",
        "Checking…",
    ),
    "update_check_failed": (
        "检查更新失败",
        "Could not check for updates",
    ),
    "bridge_current": (
        "Bridge 已是最新版本",
        "Bridge is up to date",
    ),
    "bridge_current_body": (
        "当前版本是 v{version}。",
        "Current version: v{version}.",
    ),
    "download_version": (
        "下载 {version}…",
        "Download {version}…",
    ),
    "download_installer_version": (
        "下载安装版 {version}…",
        "Download installer {version}…",
    ),
    "download_portable_version": (
        "下载便携版 {version}…",
        "Download portable {version}…",
    ),
    "bridge_installer_available_body": (
        "{version} 已发布。请从菜单下载安装版，下载后运行安装器完成升级。",
        "{version} is available. Download the installer from the menu, then run it to upgrade.",
    ),
    "bridge_portable_available_body": (
        "{version} 已发布。请从菜单下载便携版，退出 Bridge 后替换原 EXE。",
        "{version} is available. Download the portable version from the menu, then quit Bridge and replace the original EXE.",
    ),
    "bridge_available": (
        "Bridge 有新版本",
        "Bridge update available",
    ),
    "bridge_available_body": (
        "{version} 已发布，可从菜单打开下载页。",
        "{version} is available. Open the download page from the menu.",
    ),
    "checking": (
        "正在检查",
        "Checking",
    ),
    "check_and_update": (
        "检查并更新",
        "Check and update",
    ),
    "up_to_date": (
        "已是最新",
        "Up to date",
    ),
    "firmware_available": (
        "{label} 固件有新版本",
        "Firmware update for {label}",
    ),
    "firmware_available_body": (
        "当前 v{current_version}，可更新到 v{version}。请从托盘菜单开始更新。",
        "Current: v{current_version}. Available: v{version}. Start the update from "
        "the tray menu.",
    ),
    "firmware_version_available": (
        "v{version} 可更新",
        "v{version} available",
    ),
}
