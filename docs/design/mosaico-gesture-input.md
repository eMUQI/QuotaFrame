# Mosaico 本地手势输入规划

状态：软件实现与构建验证完成，待实机验收。2026-09-22 核对；工程基线 `07ac386364ff680bf1835ace646c788975a0cbbb`，规划分支 `codex/mosaico-gesture-plan`。

## 目标与范围

在 ESP-Mosaico 上增加可选的本地摄像头输入，保留触摸、AI 键、BLE 和自动时钟屏保。先验证 CameraBoard → HandDetect → 四向轨迹 → 翻页/唤醒，再增加现成分类器的 `ok` 手势进入时钟。

摄像头图像仅在设备内处理，不上传、不默认保存、不显示常驻预览。第一阶段不训练模型、不引入 ESP-VISION/MicroPython 或 ESP-WHO 整套应用、不扩展 Bridge 协议、不把视觉依赖加入其他板卡。识别区域内出现手不等于用户发出命令。

此处“解锁”指退出时钟屏保，不是身份验证，也不涉及芯片深度睡眠唤醒。摄像头唤醒要求采集与识别链路仍在运行，不能据此承诺低功耗。

## 两份报告的取舍

已阅读分享对话和补充报告。采用分享对话最后确认的“挥手翻页/唤醒”语义；👌 OK 进入时钟作为第二阶段能力。空中挥手按导航项方向切页，触摸滑动保持原有语义。

| 当前状态 | 有效动作 | 行为 |
| --- | --- | --- |
| 正常用量页 | 向左挥手 | 上一页 |
| 正常用量页 | 向右挥手 | 下一页 |
| 正常用量页 | 向上挥手 | 上一页 |
| 正常用量页 | 向下挥手 | 下一页 |
| 时钟屏保 | 任一有效四向挥手 | 只恢复原页面；本轮不再翻页 |
| 正常用量页 | 稳定 `ok` | 进入时钟并保存原页面 |
| 时钟屏保 | `ok` | 无操作，不执行 Toggle |
| 设置、配对确认、OTA | 任意手势 | 丢弃，不延后执行 |

上下左右方向均为用户面对当前屏幕所见的方向。挥动判定只用 HandDetect，因此不能宣称识别了“张掌”；若日常活动误触发无法达标，再评估以 `five` 分类作为挥动的手形门控。

补充报告中“看到手/普通运动即可唤醒”暂不采纳，避免桌前活动持续打断屏保。ROI motion 仅是后续功耗优化候选，不能代替有效挥手确认。

## 已核对的证据与缺口

| 项目 | 当前证据 | 对实施的影响 |
| --- | --- | --- |
| ESP-DL S31 支持 | Model Zoo 列出 S31；HandDetect 与 HandGesture 的 CMake 均把 `esp32s31` 映射到 `models/p4` | 发布组件已在 IDF v6.1 下完整构建通过；真实推理仍待实机验证 |
| 现成动作类别 | 示例列出 `five`、`ok` 等类别，没有独立 `fist`；`no_gesture` 是未定义手势 | 不把 `no_gesture` 当作拳头，不投入自训练 |
| 模型输入与性能 | 检测输入 224×224，分类输入 128×128；子目录耗时表只给 S3/P4 | 不将 P4 耗时外推为 S31 性能，也不将输入大小视为相机输出模式 |
| ESP-VISION | 官方支持页列出 ESP32-S31-MOSAICO；README 指定 S31 使用 IDF v6.1 | 参考 board port、采集和转换；本次未独立确认报告中的 CI 配置细节 |
| ESP-WHO | README 描述采集/推理异步运行；板卡清单未列 Mosaico | 仅作生命周期与任务划分参考 |
| 当前 BSP | `BSP_CAPS_CAMERA` 为 0，已有扩展槽电源/I2C/GPIO 基础，无采集组件依赖 | CameraBoard 接入是首个工程任务，不能只改能力宏 |
| 摄像头参考实现 | 相邻 esp-mosaico-claw 工程有 OV3640 DVP 的 `mosaico_camera`，当前接口支持左槽和帧获取/归还 | 它依赖 `subboard_support`、模块管理、board manager 及 esp-claw 内的 esp_video；不能直接复制一个组件就认为可用 |
| 固件存储 | 目标配置 16 MB Flash；两个 OTA app 分区各 7 MiB | 完整应用约 5.16 MiB，模型随应用更新 |

相邻工程核对时 HEAD 为 `903fe11ec3ca4a8881afef104e53a8da040896ac`，摄像头头文件和实现存在未提交修改，README 的默认分辨率/缓冲数量也与当前头文件不同。它只提供参考，实施时必须选定可复现的来源和许可，不能将本地工作区当作已验证的上游版本。

## 现有接入点

路径均相对仓库根目录。

| 路径/符号 | 复用方式 |
| --- | --- |
| `firmware/targets/esp_mosaico/main/app_main.cpp` | 主循环统一拥有页面、屏保、设置、OTA/配对门控；新增手势消费点 |
| `firmware/targets/esp_mosaico/main/orientation.cpp` / `navigation_swipe()` | 现有左滑→Next、右滑→Previous；保留触摸映射，单独校准相机→屏幕坐标 |
| `firmware/components/usage_panel_state/include/usage_panel_state/page_state.hpp` / `page_after_swipe()` | 复用页序与边界行为 |
| `firmware/components/usage_panel_state/screensaver_state.cpp` | 复用原页面保存、活动时间和屏保生命周期；按需要增加明确的输入方法 |
| `firmware/targets/esp_mosaico/main/display_ui.hpp` / `request_page()` | 主循环同步渲染请求，不由推理任务操作 LVGL |
| `firmware/components/usage_ble/include/usage_ble/app_events.hpp` | 当前队列是 BLE 边界，不加入摄像头帧或视觉依赖 |
| `firmware/components/esp-mosaico-bsp/include/bsp/subboard.h` | 复用扩展槽资源；确认 GPIO14 在摄像头模式是 D4，不能同时作 EEPROM 地址选择输出 |
| `firmware/components/esp-mosaico-bsp/UPSTREAM.md` | 保留已有 IDF 约束与 TE 局刷修改；必要的 BSP 修改记录于此 |

不能直接调用现有 `ScreenPage` 远程事件实现手势：当前远程分支在屏保中会恢复页面并继续翻页，还会关闭设置。手势需要“唤醒即消费”和“设置中忽略”的独立策略。

## 最小实现结构

```text
CameraBoard / esp_video
  → 获取最新帧、格式转换
  → HandDetect
  → 屏幕坐标中的单手轨迹
  → SwipeLeft / SwipeRight / SwipeUp / SwipeDown / EnterClock
  → Mosaico 主循环检查状态
  → ScreensaverController / page_after_swipe / ui.request_page
```

- 一个视觉 worker 串行完成采集、推理和轨迹更新即可起步；先测量再决定是否拆任务或绑核。模型不在 UI 主循环或 LVGL 锁内运行。
- 只在 Mosaico 增加相机适配和纯轨迹判定代码，优先放目标目录；只有需要独立管理第三方来源时才抽出组件，不预建跨板卡视觉框架。
- 相机帧的获取与归还由 worker 拥有。所有成功、失败、停用路径归还帧；停止时先结束当前推理并归还缓冲，再关闭流和释放资源。结果中不保留驱动/模型内部容器的借用引用。
- 采用有界的小事件通道，初始可只有一个待消费事件；满时丢弃，不阻塞。事件包含动作、时间戳和输入会话代次，过期或上下文已改变的事件丢弃。
- 主循环在处理最新 BLE/OTA/设置状态后消费手势。屏保、页面或输入许可变化时更新会话代次，取消旧轨迹；关闭功能、旋转、错误恢复也清空待执行动作。
- 首次挥手若发生在屏保中，只唤醒并同步 `page`、`requested_page` 与状态上报；不能随后落入翻页分支。只在命令被接受时重置活动计时。
- 第二阶段为进入时钟提供幂等语义，不重复调用 `note_remote_toggle()`。如需公共方法，只增加最小的明确进入/有效输入唤醒接口，保持其他输入行为。

### 轨迹与一次性触发

使用时间戳而非固定帧数定义窗口；检测帧率下降不应改变动作语义。bbox 中心先转为归一化屏幕坐标，包含相机安装方向、镜像与当前已应用的屏幕旋转。旋转过程中放弃当前动作。

状态从 `Idle → Tracking → WaitRelease → Idle`：

1. 单个可信目标稳定出现才开始跟踪；以位置连续性和 bbox 尺寸约束保持目标关联。
2. 在限定时间内检查主轴净位移、累计路径方向一致性、垂直于主轴的漂移、置信度与合理 bbox 大小。突然跳变、长时间漏检、模糊的多手关联取消候选。
3. 完成一次动作只发一个事件，随后等待持续无手且最短冷却已结束再重新接受输入。
4. 单帧漏检、分类器 `no_hand` 或 worker 暂停不能单独作为“手已离开”的证据。恢复采集后要观察新鲜帧，禁止旧事件回放。

以下仅为试验起点，不是已验证的产品参数：主轴净位移约对应画面宽度或高度的 25%，动作窗口 150–900 ms，主轴净位移至少为另一轴最大偏移的 2 倍；连续无手约 300 ms，最短冷却约 500 ms；事件有效期约 300 ms。阈值结合有效帧数、视场、距离和漏检率一起校准。持续可见的手不因冷却超时而重复翻页。

## 分阶段工作与完成条件

### P0：摄像头与模型可行性

- [ ] 核对实物主板版本、OV3640 CameraBoard、左槽安装方向；确认扩展槽电源、共享 I2C 和 GPIO 复用。
- [ ] 固定 esp_video、esp_cam_sensor、HandDetect/ESP-DL 的可获取版本或提交和模型摘要；核查发布包确实含 S31 路径，而不只检查 master。现有 master manifest 的版本为 hand_detect 0.2.0、ESP-DL `~3.3.0`，不能因此认定任意同版本包都含相同支持。
- [ ] 在现有 BSP 上接入最小采集，避免带入整个模块管理/应用调度框架；如必须复用其一部分，记录具体资源所有权和依赖理由。
- [ ] 获取实际协商的像素格式、宽高、stride 和缓冲数量；验证 RGB565 字节序或 UYVY 转换、缩放/裁剪和 bbox 坐标。不要直接把 UYVY 指针作为 RGB 图像输入。
- [ ] 先运行静态图 HandDetect，再接实时帧；确认推理结果和帧归还；在现有 UI、BLE、IMU、电量轮询共存时采集耗时和内存。
- [ ] 核算最终 app 大小、模型大小、内部 RAM/PSRAM 低水位及连续块。224×224 RGB888 输入约 147 KiB；640×480 双缓冲 RGB565 约 1.17 MiB，仅是缓冲示例，不含张量、显示、驱动和栈，也不表示该格式已被硬件接受。

存储决策：模型内嵌 app，使模型随 A/B OTA 和回滚一起切换。裁减未使用的 ESP-DL 像素转换后，完整应用约 5.16 MiB，采用 7 MiB 双槽。NVS、PHY 与 otadata 使用当前分区表指定的地址。设备使用该分区表后，可通过 OTA 更新应用与模型；完整安装和实机验证见[固件安装与验收](../validation/mosaico-gestures.md)。

**完成条件：**可复现的依赖与构建、可用的实时 bbox、资源与延迟记录、正常停止/缺摄像头降级。只构建成功不算实时识别通过。当前已完成软件接入及 UI，以便一次烧录完成端到端验收；实时识别和资源指标未通过实机验证前，不视为产品验收完成。

### P1：四向挥手翻页与屏保唤醒

- [ ] 完成纯轨迹判定和坐标映射；原型先验证固定朝向，再验证四个朝向。
- [ ] 接入主循环的 Swipe/Wake 策略、一次性消费、会话失效与屏保计时。
- [ ] 默认关闭；开发阶段先用 Mosaico 专属编译开关。交付前提供用户可操作的开启/关闭入口：设置页独立 `CAMERA` 开关，与显示设置共用保存/取消；已保存为打开时顶栏显示摄像头标识。
- [ ] 无摄像头、采集超时、推理失败、内存不足时禁用视觉输入，保留触摸/按键/BLE，不以 fatal error 重启整个面板。初版不承诺热插拔自动恢复，可在重新启用时重试。
- [ ] OTA 和配对期间暂停视觉工作，设置期间禁止动作；恢复后清空轨迹并重新确认手离开。处理进行中的 DMA/推理所有权后再停止。

**完成条件：**下表的软件与实机验收通过；确认 HandDetect-only 能满足误触发目标，否则先评估 `five` 门控，再决定可用范围。无需等待 👌 OK 功能即可独立评审本阶段。

### P2：👌 OK 主动进入时钟

- [ ] 引入 HandGestureRecognizer；只对目标手分类，记录新增推理耗时与资源，重新检查 OTA 大小。
- [ ] 稳定 `ok` 约 500 ms 作为初始停留确认值，增加简洁确认反馈；分数阈值由实测决定，不能把 top-1 输出直接视为可信命令。
- [ ] 进入时钟后等待手离开，防止自然松手立即唤醒；保持 `ok` 只进入一次。`no_gesture` / `no_hand` 不映射到命令。
- [ ] 若 P1 误触发需要 `five`，比较增加分类前后的误触、动作遗漏与延迟；不要求先静止半秒才能挥动。

**完成条件：**正常页一次 👌 OK 进入、重复保持无副作用、自然松手不退出，随后独立挥手只恢复原页；开关关闭后不再采集。

## 验证与决策门槛

以下数量和延迟是建议的首轮验收目标，P0 测量后再确认可实现范围，不是当前性能承诺。

| 范围 | 检查与建议门槛 |
| --- | --- |
| 纯逻辑 | 在现有 Mosaico logic 测试入口增加少量有意义的轨迹用例：上下左右、抖动/斜向、漏检/目标跳变、持续停留、冷却/离开、旋转/上下文失效；不模拟整套相机驱动 |
| 屏保 | 若共享控制器有改动，扩展现有 panel_state 测试：首挥只唤醒、恢复原页、重复进入幂等、无效检测不延长超时；保留现有触摸/按键/远程用例 |
| 编译 | 手势开/关均构建；共享状态若变化，构建受影响目标。检查锁文件、镜像大小、分区和生成配置；测试镜像构建与测试执行分别记录 |
| 方向/识别 | 记录实际距离、光线与板方向；每朝向每方向 20 次，有效操作成功率目标 ≥90%，零反向，零单轮多次触发 |
| 唤醒/门控 | 每个用量页进入屏保再挥手，第一次只恢复；设置/配对/OTA 中持续挥手及恢复后均无遗留动作 |
| 误触 | 打字、拿杯、路人、静止手、照明变化等各类场景，合计至少 30 分钟无误触；不能将短期结果宣传为长期误触率 |
| 延迟/共存 | 从动作满足条件到 UI 响应 p95 目标 ≤300 ms；同时记录采集、转换、推理、主循环等待及动作总时长。与关闭功能时对比 UI 帧耗时和 BLE 更新延迟，恶化超过约 20% 时先优化/降频，不默认发布 |
| 资源/恢复 | 连续运行至少 30 分钟，堆低水位趋于稳定、无缓冲耗尽/看门狗；反复启停，缺摄像头与超时故障均可降级 |
| 功耗 | 同亮度、同 BLE 负载测关闭/用量页启用/屏保启用的电流；低频采集是否节能必须测量，S31/P4 的计算能力不能替代功耗证据 |

固件构建前先运行 `eim --version` 和 `eim list`，按目标注册表使用 v6.1：

```sh
eim run "idf.py -C firmware/targets/esp_mosaico build" v6.1
eim run "idf.py -C firmware/targets/esp_mosaico/test_apps/logic build" v6.1
eim run "idf.py -C firmware/test_apps/panel_state build" v6.1
```

烧录、清除存储、发布另行按仓库授权规则执行。

## 当前实现与验证记录

上述阶段的复选框保留为端到端验收条件，未勾选不等于没有软件实现。已完成：

- 固定 registry 依赖和模型包；左槽 OV3640 采集适配、双缓冲、UYVY 转 RGB888 和四向映射；来源见 [CAMERA_UPSTREAM.md](../../firmware/targets/esp_mosaico/CAMERA_UPSTREAM.md)。
- 单手轨迹、OK 500 ms 停留、离开后再触发、有界事件与上下文失效；主循环实现翻页、只唤醒、幂等进入时钟及受保护状态屏蔽。
- 设置页独立 `CAMERA` 开关默认关闭，与显示设置共用保存/取消；顶栏摄像头标识区分采集中、暂停与失败，不显示文字提示；停用释放采集与模型，手动重试。ESP-DL 内部部分分配使用断言；资源预检查降低风险，但不保证所有运行时内存不足均能无重启降级。
- 7 MiB 双 OTA 分区、Folder Push 上限和 Bridge 容量配置；无共享模型分区。
- 手势开/关固件、Mosaico logic、共享 panel_state 测试镜像以及 AMOLED 2.16 固件构建通过。已完成手势/屏保主机 Unity 测试的 ASan/UBSan 检查及 Bridge 自动化测试。软件与硬件验证范围见[验证范围](../verification.md)，新增用例的执行情况以[手势验收记录](../validation/mosaico-gestures.md)为准；测试镜像尚未在设备运行。

下一步是按[首次烧录与实机验收](../validation/mosaico-gestures.md)安装并开启手势，检查摄像头方向、真实模型输出、UI 提示布局、推理耗时、误触、启停与功耗。未烧录、未发布，也未验证物理 A/B 回滚。不能把本次软件验证解释为 P0/P1/P2 的硬件验收通过。

## 参考来源

后续开发优先阅读[摄像头与手势仓库参考索引](../../firmware/targets/esp_mosaico/CAMERA_UPSTREAM.md)：集中记录官方仓库、项目 fork、固定提交/组件版本、各仓库用途及本地代码入口。下列默认分支链接用于查阅，不代表本实现的锁定版本。

- [分享报告及交互修订](https://chatgpt.com/share/6ab29265-faa8-83e8-8e32-fa2bb8b8989b)；另一份为本次任务内粘贴的补充报告。
- [ESP-DL Model Zoo](https://github.com/espressif/esp-dl/blob/master/models/README.md)
- [HandDetect CMake](https://github.com/espressif/esp-dl/blob/master/models/hand_detect/CMakeLists.txt) 与 [模型说明](https://github.com/espressif/esp-dl/blob/master/models/hand_detect/README.md)
- [HandGesture CMake](https://github.com/espressif/esp-dl/blob/master/models/hand_gesture_recognition/CMakeLists.txt)、[接口定义](https://github.com/espressif/esp-dl/blob/master/models/hand_gesture_recognition/hand_gesture_recognition.hpp) 与 [类别清单](https://github.com/espressif/esp-dl/blob/master/examples/hand_gesture_recognition/README.md)
- [ESP-VISION 支持板卡](https://docs.espressif.com/projects/esp-vision/en/latest/esp32p4/target-support/index.html) 与 [工程说明](https://github.com/espressif/esp-vision)
- [ESP-WHO](https://github.com/espressif/esp-who)
- [Mosaico 当前移植说明](../../firmware/targets/esp_mosaico/README.md)、[BSP 来源与本地修改](../../firmware/components/esp-mosaico-bsp/UPSTREAM.md)、[软件与硬件验收边界](../validation/software-acceptance.md)
