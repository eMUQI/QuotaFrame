# Mosaico 启动流程

## 显示与连接

屏幕初始化先建立共享 I2C 总线和供电。CO5300 亮度寄存器以零启动，`bsp_display_new()` 设置方向后发出 Display On；首次刷新完成后，界面通过 120 ms 亮度过渡显示画面，避免显示未初始化的帧存。

屏幕就绪后创建电量计初始化任务，随后启动 BLE 广播，再初始化 BMI270 和按键。外设初始化期间收到的 BLE 事件进入队列，由主循环处理；Bridge 的实际连接时刻还取决于主机扫描。

## 电量计

BQ27220 在独立任务中核对配置，必要时写入配置，再封存并确认状态。`bsp_battery_init()` 复用驱动已完成的封存，不重复执行。任务通过原子就绪标志将电量计交给主循环读取；就绪前不显示有效电量，OTA 电源判定保持未就绪。

驱动的 `CONFIG_BQ27220_SEAL_SETTLE_MS` 默认值为 2000 ms，Mosaico 配置为 200 ms。等待结束后驱动仍读取状态确认封存；确认失败时初始化失败，不发布有效电量。该值是启动流程的实验性配置，不是芯片时序保证。

电池图形创建时按无数据状态着色，主循环获得读数后更新，避免第一帧被误认为满电。电量计配置重写会恢复配置中的默认容量，不能将此过程视为保留电量计学习结果。

## 维护入口

- [app_main.cpp](../../firmware/targets/esp_mosaico/main/app_main.cpp)：初始化顺序、后台电量计任务和主循环轮询。
- [display_ui.cpp](../../firmware/targets/esp_mosaico/main/display_ui.cpp)：首次刷新与亮度过渡。
- [sdkconfig.defaults](../../firmware/targets/esp_mosaico/sdkconfig.defaults)：封存等待及目标配置。
- [BSP 本地修改](../../firmware/components/esp-mosaico-bsp/UPSTREAM.md)：显示启动序列和电量计集成约束。

启动耗时取决于电量计是否重写配置、外设状态和 Bridge 连接时机。比较启动日志时应使用相同硬件、配置和供电条件，并分别记录显示就绪、广播、主循环和数据到达时刻。
