# Firmware

固件使用原生 ESP-IDF，不使用 PlatformIO。当前锁定并验证的组合是：

- ESP-IDF v6.1（由 EIM 管理）；
- M5Unified 0.2.20；
- Waveshare `esp32_s3_touch_amoled_2_16` BSP 2.0.1；
- LVGL 9；
- esp-desktop-buddy 固定到`b6bac05db208717676e70180e5269d79f32b2d68`。

M5Unified 依赖由 `targets/m5sticks3/main/idf_component.yml` 管理，协议与 BLE 依赖由`components/*/idf_component.yml` 管理，各生产目标的解析结果记录在自己的 `targets/<target>/dependencies.lock`。不要手工修改 `managed_components/`。

Git 来源组件的锁文件哈希会受 checkout 行尾配置影响。本项目的 LF 基准、证据等级、排查与更新流程见 [Firmware 依赖锁文件与 Git 行尾](../docs/firmware-dependency-locks.md)。`core.autocrlf` 引起的文件字节差异不应被误判为操作系统限制。

## 共享组件与设备代码

`components/` 中的代码可由不同设备目标复用：

- `usage_core`：用量数据、最后有效值、连接和数据可用状态和倒计时格式；
- `usage_protocol`：`usage.v1` 命令解析与严格字段校验；
- `usage_json`：原始 JSON、UTF-8、重复键及名称/版本边界校验；
- `usage_ota`：物理确认、ESP-IDF OTA 写入/校验、rollback 状态和 Folder Push OTA sink；
- `usage_ble`：事件队列、BLE Secure Connections、Bonding、协议处理和状态响应；
- `usage_rtc`：PCF85063A 本地日历读写与 BCD 编解码，挂在目标自有的 I2C 总线上；
- `usage_fonts`：截取的 Montserrat LVGL 字体子集，生成步骤见组件内 README；
- `usage_panel_state`：与屏幕无关的面板行为——翻页与 OTA 触控路由、屏保状态机、渲染调度、电量分档、屏保用量视图、用量条动画与设置持久化；
- `esp_desktop_buddy_folder_push`：固定到上游 `b6bac05d` 的 Folder Push 源码快照；
- `esp-mosaico-bsp`：固定到上游 `392860b1` 的 ESP-Mosaico BSP 源码快照，仅供 `targets/esp_mosaico/` 使用。

Folder Push 本地快照原因见 [UPSTREAM.md](components/esp_desktop_buddy_folder_push/UPSTREAM.md)；核心 Buddy 和 BLE transport 由 Component Manager 锁定获取。

所有生产固件都位于 `targets/<target>/`。M5StickS3 工程位于 `targets/m5sticks3/`；Waveshare 工程位于`targets/esp32_s3_touch_amoled_216/`，使用官方 BSP 初始化 CO5300 AMOLED 和 CST9217/9220 触控，并用 LVGL 维护自己的 480×480 界面；ESP32-S3-ePaper-3.97 工程位于 `targets/waveshare_epaper_397/`，不使用 LVGL，直接绘制 800×480 四灰阶帧并按页面选择全刷或局刷。各目标只通过共享组件接收同一种用量数据。

| 目标设备 | 状态 | UI / 输入栈 |
| --- | --- | --- |
| M5StickS3 | 已维护 | M5Unified、LovyanGFX、BtnA |
| ESP32-S3-Touch-AMOLED-2.16 | 已维护 | Waveshare BSP、LVGL、触控标签和手势 |
| ESP32-S3-ePaper-3.97 | 已维护 | 自绘帧缓冲、SSD1677 驱动、三向拨轮 |
| ESP-Mosaico | 已维护 | 乐鑫 BSP、LVGL、触控标签和手势、AI 键 |

各目标的实机验收范围见 [verification.md](../docs/verification.md)。

维护中的硬件目标统一登记在 `bridge/src/quotaframe_bridge/targets.py`，用于 CI、Release、镜像结构和分区容量校验。Bridge 基础发现使用公共 QF 名称空间和 NUS，运行时名称来自 status，常驻会话来自 `devices.json`；第三方设备基础接入无需加入 registry。`scripts/target_registry.py` 输出官方构建与发布 matrix。

新增或移植硬件目标时，先阅读 [Hardware Target Porting Guide](../docs/PORTING.md)。其中定义了 target 目录与 Registry 契约、共享/板级边界、分区与 OTA 要求、target test app 接入方式，以及 build-only 与 hardware-verified 的证据边界；可复制的最小工程骨架位于 `docs/porting/minimal-target/`。

## 从源码构建

首次安装无需 ESP-IDF，可使用项目首页的 Web 烧录器，或按下文「烧录和串口日志」写入 Release 完整镜像。

开发者从仓库根目录执行 `eim --version` 和 `eim list`，确认已安装目标注册表指定的 SDK。当前维护目标使用 v6.1：

```powershell
eim run "idf.py -C firmware/targets/<target> build" v6.1
eim run "idf.py -C firmware/targets/<target> merge-bin -o <full-image>" v6.1
```

也可在 EIM 创建的 v6.1 PowerShell 中直接运行相同的 `idf.py` 命令。无需手工拼接 PATH。

| `<target>` | OTA app 镜像 | `<full-image>` |
| --- | --- | --- |
| `m5sticks3` | `m5_usage_panel.bin` | `m5_usage_panel_full.bin` |
| `esp32_s3_touch_amoled_216` | `ws_usage_panel.bin` | `ws_usage_panel_full.bin` |
| `waveshare_epaper_397` | `ws_epaper_397.bin` | `ws_epaper_397_full.bin` |
| `esp_mosaico` | `mosaico_usage_panel.bin` | `mosaico_usage_panel_full.bin` |

产物位于各目标的 `build/`。ESP-Mosaico 首次配置需启用 preview target，见[目标说明](targets/esp_mosaico/README.md)；墨水屏刷新、输入和诊断见[ePaper 说明](targets/waveshare_epaper_397/README.md)。

M5GFX 的 SPI 兼容包装位于 `targets/m5sticks3/main/m5gfx_spi_compat.c`，将未初始化的 DMA burst 值归一化为驱动默认值；升级依赖时需复核包装与实机显示。

AMOLED 2.16 使用 16 MB Flash、Octal PSRAM、4 MB App 分区和 BSP 2.0.1，利用 QMI8658、PCF85063 与 AXP2101 实现动作唤醒、时钟和电源遥测；音频与 SD 卡未纳入产品功能。

## 分区表与 OTA 准备

所有生产目标都使用双 OTA 槽分区表（`ota_0` / `ota_1` / `otadata`），没有 `factory` 分区。首次烧录时 `otadata` 为空，bootloader 按 ESP-IDF 标准行为默认从 `ota_0` 启动。各目标的分区布局见 `targets/<target>/partitions.csv`，OTA 槽容量同时登记在 Target Registry 的 `ota_partition_bytes`，发布装配器会核对两者。

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 已开启。`usage_ota` 使用`esp_ota_begin` / `esp_ota_write` / `esp_ota_end` 写入非当前分区，核对镜像和 SHA-256 后才切换启动分区；新镜像完成显示和 BLE 初始化后才标记有效。

## Folder Push OTA

固件通过现有 Buddy/NUS 通道接收 `ota.folder.v1`，不注册独立 OTA GATT Service。应用 sink 只接受 transfer `firmware`，其中必须先发送 `manifest.json`，物理确认后再发送 `firmware.bin`。详细 wire contract 见 [协议文档](../protocol/README.md)。

固件使用 2,880 B 解码块，完整 JSON 帧上限为 4,096 B。应用容量按目标真实 OTA 槽校验，传输总量另计最多 512 B manifest。设备核对项目、target、镜像结构与摘要，并逐次要求物理确认。

## 烧录和串口日志

首次安装或串口恢复无需 ESP-IDF。直接写入 Release 中与硬件型号匹配的 `-full-` 镜像，从 `0x0` 开始：

```powershell
esptool --chip esp32s3 -p <PORT> write-flash 0x0 waveshare-esp32-s3-touch-amoled-216-full-v<version>.bin
esptool --chip esp32s31 -p <PORT> write-flash 0x0 espressif-esp-mosaico-full-v<version>.bin
```

文件名前缀即 Target Registry 的 `release_stem`；`--chip` 按设备芯片选择，ESP-Mosaico 为 `esp32s31`，其余为 `esp32s3`。

必须选择正确的设备型号和串口。`-full-` 文件不能用于 BLE OTA；日常升级使用 Release 中对应的 `-ota-` 文件。下面的 `idf.py flash` 命令仅适用于已经从源码构建的开发环境。

先在设备管理器确认串口，使用实际端口 `<PORT>`：

```powershell
idf.py -C firmware/targets/m5sticks3 -p <PORT> flash monitor
```

退出 monitor 使用 `Ctrl+]`。只烧录不监视：

```powershell
idf.py -C firmware/targets/m5sticks3 -p <PORT> flash
```

固件启动后会以 `QF-M5-XXXX` 广播，其中 `XXXX` 来自 BLE MAC 末两字节。

Waveshare 的烧录命令相同，只需替换工程路径和实际端口：

```powershell
idf.py -C firmware/targets/esp32_s3_touch_amoled_216 -p <PORT> flash monitor
```

Waveshare AMOLED 固件以 `QF-WS-S3-A216-XXXX` 广播，状态身份为`Waveshare Usage Panel`；ePaper 固件以 `QF-WS-S3-E397-XXXX` 广播，状态身份为 `Waveshare ePaper 3.97`，型号标识为 `waveshare_epaper_397`；ESP-Mosaico 固件以 `QF-Mosaico-XXXX` 广播，状态身份为 `ESP-Mosaico Usage Panel`，型号标识为 `esp_mosaico`。Bridge 按用户已认领的设备地址分别建立会话，支持同型号多台设备；各设备独立配对和重连。

## 页面和状态

正面 BtnA 的顺序固定为：

```text
Overview -> Codex -> Claude -> Overview
```

Waveshare 使用屏幕底部的 Overview、Codex、Claude 三个触控标签直接切页；内容区域左滑进入下一页、右滑返回上一页，到两端后保持当前页。

状态优先级：

1. 配对码浮层；
2. `NO DATA`；
3. `OFFLINE`；
4. `PARTIAL`；
5. `ONLINE`。

蓝牙连接正常时持续显示最后一次有效用量，刷新失败和样本年龄不改变显示。只有蓝牙断开或从未取得有效数据时显示灰态。设备重启后用量从空状态开始，收到有效数据后恢复正常显示。

## M5StickS3 显示刷新

M5StickS3 固件使用一张与屏幕等大的 16 位 `M5Canvas` 先在内存中完成整页绘制，再把完整画面推送到屏幕。每秒只计算一次当前页面的可见时间状态；只有倒计时文字等可见内容实际发生变化时才刷新。切页、收到用量、BLE 状态变化和配对码浮层仍会立即刷新。

若全屏画布分配失败，串口会记录 `full-screen canvas allocation failed`，随后降级为直接绘制。降级不会影响页面功能，但合法更新时可能再次看见短暂的整页重绘。

烧录 M5StickS3 主固件后按以下步骤验证显示效果：

1. 在任一页面静置至少两分钟，确认没有周期性整屏闪黑；
2. 使用 BtnA 依次切换 Overview、Codex 和 Claude，确认页面完整且无黑色中间帧；
3. 发送新用量并等待倒计时文字变化，确认内容及时更新；
4. 断开并恢复 Bridge，确认连接状态及时更新；
5. 在需要重新配对时确认六位配对码浮层出现和消失均正常。

固件构建成功只能确认画布和刷新调度代码可编译，不能替代上述实机观察。

Waveshare 目标由官方 BSP 启动 LVGL，界面对象只创建一次，后续仅更新可见页面、文本、进度条和状态。底部标签处理点击，内容区域处理左右滑动；这部分同样需要在实机上确认触控方向、边界行为和连续刷新。

## BLE 配对

添加设备、输入配对码及主机绑定修复见 [Bridge 配对说明](../bridge/README.md#托盘配对和重连)。加密后配对浮层消失，绑定分别保存在设备 NVS 与主机。

若设备旧绑定仍阻止配对，最后手段是擦除后重新烧录。先确认设备、实际串口和目标工程：

```powershell
eim run "idf.py -C firmware/targets/<target> -p <PORT> erase-flash" v6.1
eim run "idf.py -C firmware/targets/<target> -p <PORT> flash" v6.1
```

`erase-flash` 清除固件及设备全部配置和绑定，仅在确认目标、端口及配置可清除后使用。

## 固件测试工程

共享组件的测试工程在 `test_apps/` 下，每个目标自己的板级逻辑测试在`targets/<target>/test_apps/logic`。本节命令与上面的构建命令一样，在 EIM v6.1 PowerShell 中执行；若 EIM CLI 已在 PATH 中，也可写成 `eim run "<命令>" v6.1`。构建某个目标的板级 Unity 测试镜像：

```powershell
idf.py -C firmware/targets/m5sticks3/test_apps/logic build
```

连接该目标后可烧录并运行：

```powershell
idf.py -C firmware/targets/m5sticks3/test_apps/logic -p <PORT> flash monitor
```

测试覆盖页面循环、倒计时、电量视图、供电判定与按键路由。没有设备时只能验证测试镜像成功编译，不能声明 Unity 用例已在目标芯片上运行。

构建 `usage_panel_state` 的行为测试镜像：

```powershell
idf.py -C firmware/test_apps/panel_state build
```

构建 Buddy JSON 路由、usage.v1 ACK 和 Folder Push OTA sink 契约测试镜像：

```powershell
idf.py -C firmware/test_apps/protocol build
```

连接设备后的运行方式与逻辑测试相同，只需把工程路径替换为`firmware/test_apps/protocol`。该工程还固定检查 M5 的 BLE 广播名和 status JSON、Waveshare 的独立身份、manifest/firmware 顺序、物理确认门控、镜像校验与 abort 清理，并覆盖配置字符串输出缓冲区不足的情况。

Waveshare 页面状态和刷新逻辑测试镜像：

```powershell
idf.py -C firmware/targets/esp32_s3_touch_amoled_216/test_apps/logic build
```

无 Waveshare 实机时，该命令只能证明测试代码和目标逻辑完成编译、链接，不能声明 Unity 测试已经在芯片上运行。
