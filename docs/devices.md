# 设备与操作说明

[返回首页](../README.md)

## 功能对照

| 能力 | Waveshare AMOLED 2.16 | M5StickS3 | Waveshare ePaper 3.97 | ESP-Mosaico |
| --- | --- | --- | --- | --- |
| 屏幕 | 480×480 圆角方形 AMOLED | 135×240 LCD | 800×480 四灰阶墨水屏 | 480×480 AMOLED |
| 翻页交互 | 底部触控标签与左右滑动 | 正面按键 | 三向拨轮 | 触控标签、左右滑动与 AI 键 |
| 用量页面 | 总览 / Codex / Claude | 总览 / Codex / Claude | 主页同屏显示 Codex 与 Claude，另有趋势页 | 总览 / Codex / Claude |
| 断连保留最后用量 | ✅ | ✅ | ✅ | ✅ |
| 电量与充电图标 | ✅ AXP2101 | ✅ M5PM1 | ✅ AXP2101 | ✅ BQ27220 |
| 时钟屏保 | ✅ PCF85063 RTC | — | ✅ PCF85063 RTC | ⚠️ 无 RTC 芯片，掉电后日历需 Bridge 重新下发 |
| 屏保用量细条 | ✅ | — | ✅ | ✅ |
| 自动旋转 | ✅ 四向 | — 固定 0°/180°（menuconfig） | ✅ 四向 | ✅ 四向 |
| 摇动 / 翻转唤醒 | ✅ | — | — | ✅ |
| 屏上设置页 | ✅ 亮度、自动时钟超时 | — 亮度仅 menuconfig | ✅ 刷新间隔、屏幕方向 | ✅ 亮度、自动时钟超时 |
| 温湿度显示 | — | — | ✅ 时钟页 | — |
| 用量趋势（48 点 / 30 分钟） | — | — | ✅ 需 FAT SD 卡 | — |
| 托盘/菜单栏左键切换时钟（`screen.toggle.v1`） | ✅ | — | ✅ | ✅ |
| 滚轮翻页（`screen.page.v1`） | ✅ | ✅ | ✅ | ✅ |
| BLE OTA（`ota.folder.v1`） | ✅ | ✅ | ✅ | ✅ |
| Web 烧录器 | ✅ | ✅ | ✅ | ✅ |

✅ 已实现；⚠️ 存在使用限制；— 未实现。所有设备都显示短周期与周周期的已用百分比、进度条与重置倒计时。表中的固件能力只描述设备侧；左键切换时钟与滚轮翻页在 Windows 托盘和 macOS 菜单栏都已支持，Windows 与 macOS 都提供固件 OTA 入口，安装需桌面确认及设备物理确认。

ePaper 3.97 与 ESP-Mosaico 的详细说明见 [ePaper 3.97 固件说明](../firmware/targets/waveshare_epaper_397/README.md) 与 [ESP-Mosaico 固件说明](../firmware/targets/esp_mosaico/README.md)。

## 屏幕快捷操作

- **480×480 触屏设置（微雪 AMOLED 2.16 与 ESP-Mosaico）**：长按触屏约 0.8 秒进入，两块板共用同一套设置。亮度即时预览；AUTO CLOCK 可选择 OFF、1、5、10、30 分钟。SAVE 保存并在重启后保留，CANCEL 撤销未保存调整。OFF 仅关闭自动进入时钟，托盘/菜单栏左键仍可主动切换；微雪另有 PWR 侧键，ESP-Mosaico 另有 AI 键。
- **Windows 托盘与 macOS 菜单栏**：左键切换支持设备的时钟；鼠标悬停在 Bridge 图标上滚动，上滚查看上一页，下滚查看下一页，页面集合与顺序由各设备决定。操作作用于所有已连接且支持该命令的设备；设备处于时钟时会先回到额度页面。快速连续滚动会限速，OTA 期间不执行翻页。
- 滚轮翻页要求设备通告 `screen.page.v1`；未通告的设备只接收其支持的命令。
