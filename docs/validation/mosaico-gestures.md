# Mosaico 实验性手势：使用与安装

本地摄像头手势是**实验性功能**，默认关闭。需要左槽 CameraBoard，支持 OV3640 和 SC101IOT，固件自动识别传感器。图像只在设备内处理，不上传、不保存。识别效果受光线、距离和手部是否完整入镜影响。

## 开启与操作

安装相机板前先断电。开机后长按触屏进入设置，打开 `CAMERA` 并点击 `SAVE`。关闭该开关并保存即可停止采集；`CANCEL` 撤销未保存的调整。

| 页面 | 动作 | 效果 |
| --- | --- | --- |
| 用量页 | 向任意方向挥手，或左右招手 | 切到下一页，按总览 → Codex → Claude 循环 |
| 用量页 | 👌 OK 或 👍 点赞稳定保持约半秒 | 进入时钟 |
| 时钟 | 挥手或左右招手 | 恢复进入时钟前的页面 |

一段连续挥动只执行一次，手停稳后再做下一次动作。触摸、AI 键和 Bridge 操作继续可用；设置、配对确认和 OTA 期间暂停手势输入。

## 摄像头标识与预览

打开 `CAMERA` 后，顶栏显示摄像头标识：采集中为亮色，暂停时变暗，失败时带斜杠。关闭并保存后标识消失。

在普通用量页点击摄像头标识可显示或隐藏预览。预览保留画面比例，显示图像和手部定位框，可用于调整摆放位置。预览默认隐藏，重启后恢复隐藏；关闭预览不影响识别。时钟、设置、配对和 OTA 界面隐藏预览，返回普通页面后恢复本次选择。

标识带斜杠时，断电检查左槽相机连接，再开机重试。也可关闭 `CAMERA` 并保存，再打开并保存；仍无法启动时重启设备。屏保期间摄像头继续工作，开启手势会增加耗电。

## 构建

在仓库根目录运行：

```sh
eim --version
eim list
eim run "idf.py -C firmware/targets/esp_mosaico build" v6.1
```

构建产物为 `firmware/targets/esp_mosaico/build/mosaico_usage_panel.bin`。生成配置应启用 CLIB 分配器、OV3640 和 SC101IOT；摄像头选项位于 `menuconfig → Camera gestures`。

16 MiB Flash 使用两个 7 MiB 应用槽，模型与代码共同更新。NVS 位于 `0x9000`，ota_0 位于 `0x10000`，otadata 位于 `0x810000`，ota_1 位于 `0x820000`。Folder Push 限额为 7 MiB + 512 字节。

## 有线烧录

按住 BOOT 再开机进入 ROM 下载模式。整段擦写大应用可能断连，应用按 1 MiB 分段烧录，不直接使用 `idf.py flash` 或未拆分的 `@flash_args`。

地址从目标应用槽起点逐段递增。使用 ESP-IDF v6.1 环境中的 esptool，参数为 `--chip esp32s31 --no-stub`、460800 波特率、DIO/80 MHz/16 MB，并指定 `--after no-reset`。烧录前核对分段拼接内容与构建应用一致，烧录后确认每段设备哈希校验成功，再通过 POWER 关机重启。

仅更新当前应用时，不写入 NVS、分区表或初始 OTA 元数据。完整安装使用构建产物匹配的 bootloader、分区表及初始化数据，应用仍分段写入。整片擦除或写入包含填充区域的合并镜像会影响已保存的配对和设置。

## 维护入口

应用启动 USB CDC 后，日志通过 Type-C USB OTG 串口输出；连接监视器时使用 `monitor --no-reset` 可避免重启。启动早期的 UART0 输出可从右侧排针 H1 的 15 脚 TX0 / GPIO58，或底板背面 TX 调试焊盘读取，波特率为 115200。摄像头占用的 USB Serial/JTAG 引脚位于左侧排针，与 Type-C USB OTG 独立。日志包含手部坐标、置信度、动作和运行耗时，不包含图像。

代码结构、识别参数见[手势设计](../design/mosaico-gesture-input.md)；固定依赖、模型摘要和引脚所有权见[摄像头依赖说明](../../firmware/targets/esp_mosaico/CAMERA_UPSTREAM.md)。
