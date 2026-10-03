# Mosaico 实验性手势输入

本功能使用左槽 CameraBoard 在设备本地识别手势，默认关闭。用户操作见[使用与安装](../validation/mosaico-gestures.md)，固定依赖与参考仓库见[摄像头依赖说明](../../firmware/targets/esp_mosaico/CAMERA_UPSTREAM.md)。

## 交互

| 当前页面 | 动作 | 行为 |
| --- | --- | --- |
| 用量页 | 任一方向挥手或左右招手 | 下一页，按总览 → Codex → Claude 循环 |
| 时钟 | 挥手或左右招手 | 只恢复原页面，本轮不翻页 |
| 用量页 | 稳定 OK 或点赞约 500 ms | 进入时钟并保留原页面 |
| 时钟 | OK 或点赞 | 不切换页面 |
| 设置、配对确认、OTA | 任意手势 | 不接受动作，不延后执行 |

触摸滑动保留双向导航。摄像头唤醒依赖持续采集和识别，时钟屏保不是芯片深度睡眠。

设置页的 `CAMERA` 开关与亮度、自动时钟超时一并保存。调整开关只改变设置预览，`SAVE` 成功后才改变采集状态，`CANCEL` 恢复保存值。NVS 使用 `mosaico/settings` 单个整数保存三个字段；记录缺失或无效时关闭相机，亮度和自动时钟超时沿用旧版固件在 `display` 命名空间中保存的值，没有则使用默认值。

图像仅在本机处理，不上传或保存。点击摄像头标识切换预览，预览选择不持久化。界面尺寸与状态见[AMOLED 设计规范](amoled-ui.md)。

## 处理链路与所有权

```text
CameraBoard / esp_video
  → 最新 UYVY 帧
  → 等比例 RGB888 转换、镜像与旋转
  → HandDetect → 单手轨迹 / HandGestureCls
  → SwipeLeft / SwipeRight / SwipeUp / SwipeDown / Wave / EnterClock
  → Mosaico 主循环检查上下文
  → 翻页 / 唤醒 / 进入时钟
```

| 文件 | 职责 |
| --- | --- |
| `firmware/targets/esp_mosaico/main/gesture_camera.cpp` | 左槽引脚、V4L2 流、帧获取与归还、停止及资源释放 |
| `firmware/targets/esp_mosaico/main/gesture_image.cpp` | UYVY 转 RGB888、等比例缩放、镜像和旋转 |
| `firmware/targets/esp_mosaico/main/gesture_tracker.cpp` | 单手轨迹、招手、姿态停留与重复触发抑制 |
| `firmware/targets/esp_mosaico/main/gesture_input.cpp` | 视觉任务、模型生命周期、上下文和有界事件队列 |
| `firmware/targets/esp_mosaico/main/app_main.cpp` | 设置、页面、屏保和输入许可；消费手势并更新 UI |
| `firmware/targets/esp_mosaico/main/display_ui.cpp` | 摄像头标识、停留进度和预览；LVGL 访问受显示锁保护 |
| `firmware/targets/esp_mosaico/main/panel_settings.cpp` | 显示与相机设置的原子持久化 |

视觉任务串行执行采集、检测、分类和轨迹更新。V4L2 帧转换后立即归还；模型不在主循环或 LVGL 锁内运行。预览回调从视觉任务调用，取得显示锁后复制像素；隐藏时跳过显示锁和绘制。

主循环拥有页面和屏保状态。事件队列容量为 1，满时丢弃，不阻塞推理。每个事件带时间戳和上下文代次；超过 300 ms 或代次不匹配时丢弃。触摸、按键、连接变化、页面、旋转和输入许可变化使旧轨迹失效。被接受的手势会保留自身的重复触发抑制状态。

关闭相机或进入受保护状态时，视觉任务完成当前帧后停止流并释放模型和缓冲。启动最多尝试三次；连续三次采集失败停用识别。普通故障可通过关闭并重新开启重试；无法安全释放驱动资源时，需重启设备。主循环的触摸、按键和 BLE 路径独立于视觉任务。

## 识别参数

以下为当前实验性实现的参数，集中定义于 `gesture_tracker.cpp`，不代表识别率或响应时间承诺。

- 单手检测置信度至少 0.65；位置归一化到当前屏幕坐标。多手、越界、尺寸异常和不连续观测不构成有效挥手。
- 挥手至少跨越三帧、持续 150–900 ms；主轴净位移至少 25%，至少为另一轴最大偏移的两倍，且不小于主轴累计路径的 75%。
- 招手由三段交替水平运动构成，每段至少为画面宽度的 12%。一次连续运动只发出一个动作；再次接受挥动前需要约 400 ms 静止，帧间位移不超过 3% 视为静止。挥手后 1.5 秒内的反向挥动作为收手忽略。
- OK 和点赞使用分类器的 `ok`、`like` 类别，置信度至少 0.85，稳定保持约 500 ms。相邻帧位移超过 5% 时跳过分类，优先保留轨迹采样频率。
- 可接受的观测间隔上限为 400 ms。短暂漏检保留候选，长间隔或位置跳变重新开始跟踪。
- 初始化、外部输入和 OK 触发后，挥手与姿态识别要求连续约 300 ms 无手，并距上次动作至少 500 ms。招手按交替轨迹单独判定。姿态停留不反复触发时钟切换。

相机安装校正先水平镜像再顺时针旋转 90°，随后应用屏幕旋转的逆变换。OV3640 的 640×480 图像缩为 320×240，SC101IOT 的 1280×720 图像缩为 320×180，旋转后将实际尺寸传给模型和预览。

## 资源与存储

检测使用 ESPDet-Pico 224×224，分类使用 MobileNetV2 128×128。模型随代码内嵌在应用中，使用 7 MiB 双 OTA 槽，更新及回滚使用同一份代码和模型。组件版本与模型 SHA-256 见[依赖说明](../../firmware/targets/esp_mosaico/CAMERA_UPSTREAM.md)。

720p 双 UYVY 缓冲约占 3.52 MiB。启动预检查要求至少 9 MiB 连续空闲 PSRAM 和 96 KiB 内部内存，为模型及并行 UI 分配留出空间。ESP-DL 内部分配包含断言，预检查不构成运行时内存分配成功的保证。

LVGL 使用 CLIB 分配器，避免独立固定大小的字形内存池。日志记录采集转换、检测、分类、反馈耗时以及内存和栈余量；`mean_cycle_ms` 不包含反馈和每轮固定 20 ms 等待，不能直接换算为帧率。

## 开发检查

在仓库根目录使用目标指定的 SDK：

```sh
eim --version
eim list
eim run "idf.py -C firmware/targets/esp_mosaico build" v6.1
eim run "idf.py -C firmware/targets/esp_mosaico/test_apps/logic build" v6.1
eim run "idf.py -C firmware/test_apps/panel_state build" v6.1
```

Mosaico logic 包含轨迹、动作路由、图像坐标转换和设置持久化用例，共享 panel_state 覆盖屏保。构建测试镜像与运行用例是不同步骤，执行结果应对应具体源码和产物记录。仓库通用检查见[软件与发布验收](../validation/software-acceptance.md)。
