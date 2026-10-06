# 设备与操作说明

[返回首页](../README.md)

## 功能对照

| 能力 | Waveshare AMOLED 2.16 | M5StickS3 | Waveshare ePaper 3.97 | ESP-Mosaico |
| --- | --- | --- | --- | --- |
| 屏幕 | 480×480 圆角方形 AMOLED | 135×240 LCD | 800×480 四灰阶墨水屏 | 480×480 AMOLED |
| 翻页交互 | 底部触控标签与左右滑动 | 正面按键 | 三向拨轮 | 触控标签、左右滑动与 AI 键 |
| 摄像头手势（实验性） | — | — | — | 需左槽 CameraBoard，默认关闭 |
| 用量页面 | 总览 / Codex / Claude | 总览 / Codex / Claude | 主页同屏显示 Codex 与 Claude，另有趋势页 | 总览 / Codex / Claude |
| 断连保留最后用量 | ✅ | ✅ | ✅ | ✅ |
| 电量与充电图标 | ✅ AXP2101 | ✅ M5PM1 | ✅ AXP2101 | ✅ BQ27220 |
| 时钟屏保 | ✅ PCF85063 RTC | — | ✅ PCF85063 RTC | ⚠️ 无 RTC 芯片，掉电后日历需 Bridge 重新下发 |
| 屏保用量细条 | ✅ | — | ✅ | ✅ |
| 自动旋转 | ✅ 四向 | — 固定 0°/180°（menuconfig） | ✅ 四向 | ✅ 四向 |
| 摇动 / 翻转唤醒 | ✅ | — | — | ✅ |
| 屏上设置页 | ✅ 亮度、自动时钟超时 | — 亮度仅 menuconfig | ✅ 刷新间隔、屏幕方向 | ✅ 亮度、自动时钟超时、CAMERA 开关 |
| 温湿度显示 | — | — | ✅ 时钟页 | — |
| 用量趋势（48 点 / 30 分钟） | — | — | ✅ 需 FAT microSD 卡 | — |
| 托盘/菜单栏左键切换时钟（`screen.toggle.v1`） | ✅ | — | ✅ | ✅ |
| 滚轮翻页（`screen.page.v1`） | ✅ | ✅ | ✅ | ✅ |
| BLE OTA（`ota.folder.v1`） | ✅ | ✅ | ✅ | ✅ |
| Web 烧录器 | ✅ | ✅ | ✅ | ✅ |

✅ 已实现；⚠️ 存在使用限制；— 未实现。所有设备都显示短周期与周周期的已用百分比、进度条与重置倒计时。表中的固件能力只描述设备侧；左键切换时钟与滚轮翻页在 Windows 托盘和 macOS 菜单栏都已支持，Windows 与 macOS 都提供固件 OTA 入口，安装需桌面确认及设备物理确认。

ePaper 3.97 与 ESP-Mosaico 的详细说明见 [ePaper 3.97 固件说明](../firmware/targets/waveshare_epaper_397/README.md) 与 [ESP-Mosaico 固件说明](../firmware/targets/esp_mosaico/README.md)。

RLCD 4.2 使用 400×300 黑白反射式 LCD，提供总览、Codex / Claude 详情、趋势、时钟和设置页；竖屏仅在总览和时钟间轮换。它支持 RTC、温湿度显示、电压估算电量，以及需 FAT microSD 卡的 48 点趋势。屏幕方向由设置选择，可通过 Web 烧录器安装。当前实机验证范围和完整操作见 [RLCD 4.2 说明](../firmware/targets/waveshare_rlcd_42/README.md)。

Read Pico 使用 684×1216 十六灰阶墨水屏，主页同屏显示 Codex 与 Claude 用量、告警状态和 24 小时趋势，另有时钟、设置、配对和 OTA 页面。它支持 PMU RTC、电量与充电状态、四向自动旋转、拿起时从自动时钟返回主页，以及需 FAT microSD 卡的 48 点趋势。可通过 Web 烧录器安装，完整操作见 [Read Pico 说明](../firmware/targets/mindreset_read_pico/README.md)。

## 屏幕快捷操作

- **Read Pico**：KEY1 / KEY3 在主页与时钟间切换，KEY2 整屏刷新，长按 KEY2 约 0.8 秒进入或退出设置；设置中点按选项即保存，点 DONE 退出。OTA 待确认时点 CONFIRM / DENY，或按 KEY2 确认、长按拒绝。
- **RLCD 4.2**：KEY 上一页，BOOT 下一页，长按 KEY 进入或退出设置；设置中 KEY 选择项目、BOOT 修改选项。KEY 可关闭当前用量告警。OTA 待确认时 KEY 确认，长按 KEY 拒绝。
- **480×480 触屏设置（微雪 AMOLED 2.16 与 ESP-Mosaico）**：长按触屏约 0.8 秒进入，两块板均支持亮度与自动时钟设置。亮度即时预览；AUTO CLOCK 可选择 OFF、1、5、10、30 分钟。SAVE 保存并在重启后保留，CANCEL 撤销未保存调整。OFF 仅关闭自动进入时钟，托盘/菜单栏左键仍可主动切换；微雪另有 PWR 侧键，ESP-Mosaico 另有 AI 键。
- **ESP-Mosaico 实验性手势**：安装左槽 CameraBoard 后，在设置中打开 CAMERA 并保存。用量页向任意方向挥手或左右招手切到下一页；OK 或点赞稳定保持约半秒进入时钟，挥手或招手唤醒。点击顶栏摄像头标识切换预览。完整说明见[手势指南](validation/mosaico-gestures.md)。
- **Windows 托盘与 macOS 菜单栏**：左键切换支持设备的时钟；鼠标悬停在 Bridge 图标上滚动，上滚查看上一页，下滚查看下一页，页面集合与顺序由各设备决定。操作作用于所有已连接且支持该命令的设备；设备处于时钟时会先回到额度页面。快速连续滚动会限速，OTA 期间不执行翻页。
- 滚轮翻页要求设备通告 `screen.page.v1`；未通告的设备只接收其支持的命令。
