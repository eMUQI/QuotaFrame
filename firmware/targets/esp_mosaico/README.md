# ESP-Mosaico

乐鑫 ESP-Mosaico 开发板的移植工程，结构遵循 [Hardware Target Porting Guide](../../../docs/PORTING.md)，功能与实机范围见 [verification.md](../../../docs/verification.md)。已登记到 `bridge/src/quotaframe_bridge/targets.py`，进入 CI 与 Release matrix。发布提供 `espressif-esp-mosaico-ota-v<version>.bin` 和 `espressif-esp-mosaico-full-v<version>.bin`；完整镜像从地址 `0x0` 写入。该目标已接入 Web 烧录器，按设备型号选择 ESP-Mosaico 后安装完整镜像。

## 硬件

| 项目 | 参数 |
| --- | --- |
| SoC | ESP32-S31（RISC-V 双核 320 MHz，Wi-Fi 6 / BLE 5.4 / Classic BT） |
| 屏幕 | CO5300 QSPI 480×480 |
| 触控 | CST9217 |
| Flash / PSRAM | 16 MB / Octal |
| 输入 | AI 键、BOOT 键 |
| 电量 | BQ27220 |
| 其他 | ES8311 音频、BMI270 IMU、两路 BMM150、SPI NAND、振动马达、两个热插拔扩展槽 |

板级能力由上游 BSP 提供。该 BSP 未发布到 ESP Component Registry。

## ESP-IDF 版本与 BSP 来源

本目标使用 ESP-IDF v6.1 的 `esp32s31` preview target。BSP 本地快照放宽了上游 SDK 版本约束，并维护显示集成修改；固定提交、原因与同步要求见 [UPSTREAM.md](../../components/esp-mosaico-bsp/UPSTREAM.md)。

```bash
eim run "idf.py -C firmware/targets/esp_mosaico --preview set-target esp32s31" v6.1
eim run "idf.py -C firmware/targets/esp_mosaico build" v6.1
```

构建产物为 `build/mosaico_usage_panel.bin`，两个 app 分区均为 4 MiB，Folder Push 总传输限额为 4 MiB + 512 字节，额外空间用于 manifest。

## 当前实现

界面、用量数据路径、BLE 与 OTA 都已接入，UI 从 ESP32-S3-Touch-AMOLED-2.16 移植而来（同为 480×480 CO5300 + CST9217）：

- 与屏幕无关的面板行为由 `usage_panel_state` 共享；`display_ui.*` 和带板级标定的 `orientation.*` 由本目标维护；
- 显示、触控、旋转、亮度和 LVGL 锁全部交给乐鑫 BSP：`bsp_display_start_with_config()` 一次性完成 esp_lvgl_adapter 初始化、CO5300 注册、CST9217 注册与区域对齐，`bsp_display_set_rotation()` 同时旋转面板和触控；
- `usage_core` / `usage_protocol` / `usage_ble` / `usage_ota` 与其他目标共用，BLE 广播前缀 `QF-Mosaico-`，状态身份 `ESP-Mosaico Usage Panel`，型号标识 `esp_mosaico`；
- 触控标签与左右滑动切页之外，板载 AI 键单击可唤醒并切换到下一页。

### 显示刷新

默认保留 CO5300 TE 输出，使用局部刷新和单个 PSRAM 绘图缓冲。BSP 在每轮
LVGL 刷新的第一次像素传输前等待新的 TE 上升沿，同轮其余区域连续提交；
DMA 完成及缓冲释放仍由适配器处理。缓冲保留全屏容量，不代表强制整屏重绘。
TE 信号缺失时最多等待 25 ms 后继续刷新，并在超时、恢复状态变化时记录日志。

启动日志应包含 `partial refresh: PSRAM single buffer`。硬件验收应比较导航条、
用量条动画和整页切换的帧间隔，并检查四个旋转方向、休眠唤醒后的撕裂与触控。
40 MHz 四线 QSPI 传输整屏 RGB565 的理论下限约为 23 ms；整页更新仍可能跨越
屏幕扫描周期，按帧 TE 同步不等于所有更新均无撕裂。此刷新路径尚待实机验收。

### 与 AMOLED 目标的板级差异

| 项目 | AMOLED 2.16 | ESP-Mosaico |
| --- | --- | --- |
| IMU | QMI8658 | BMI270（`bsp_imu_get_accel()` 直接返回 g） |
| 电量 | AXP2101（含 PWR 键） | BQ27220 电量计，无 PMIC，无电源键 |
| 时钟 | PCF85063A RTC | 无 RTC 芯片，见下 |

- 外部供电判定：BQ27220 只看得到电池，读不到 USB 轨。`power_monitor.cpp` 以「电池未在放电」作为外部供电的代理判据，OTA 电量门控也用这一判据。
- 时钟：板上没有 RTC 芯片，`local_clock.*` 把 Bridge 下发的本地日历写入 SoC 系统时间再读回，libc 负责日历换算。掉电后日历丢失，需要 Bridge 重新下发。
- 旋转：BMI270 相对面板轴旋转了 180°，`orientation.cpp` 的四个分支把这个偏移固定折进映射里，已在实机核对四个方向。板级偏移只放在这里，不动 BSP 的 `BSP_LCD_BASE_*`/`BSP_TOUCH_BASE_*`——那是面板与触控共用的安装基准，只改一边会让触控和画面错开。

## 测试

```bash
eim run "idf.py -C firmware/targets/esp_mosaico/test_apps/logic build" v6.1
```

只覆盖本目标独有的 `local_clock` 和 `orientation`。共享面板行为由 `firmware/test_apps/panel_state` 覆盖。没有实机时该命令只证明测试镜像编译通过。

## 后续步骤

1. 实机验证触控方向、AI 键，以及 OTA 断电恢复与回滚；
2. 按实测结果校准 BQ27220 的外部供电判据。
