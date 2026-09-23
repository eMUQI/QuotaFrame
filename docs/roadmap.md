# 维护方向

当前设备能力和安装方法见[项目 README](../README.md)。以下为维护方向，不代表交付承诺；具体方案以公开 Issue 和 Pull Request 为准。

- 完善 Windows/macOS 安装、授权、自启、更新与新用户上手验证。
- 完成各维护目标的 OTA 中断恢复、损坏镜像、断电和启动失败回滚验收。
- 评估第三方更新的可信来源机制。
- 验证墨水屏长期残影、SD 趋势存储与断电恢复。
- 依据实测评估显示响应、低功耗与电池续航。
- 根据维护资源评估更多开发板、Intel macOS、签名和包管理器分发。

## 技术文档

| 文档 | 用途 |
| --- | --- |
| [贡献指南](../CONTRIBUTING.md) | 开发入口、提交与验证要求 |
| [验证范围](verification.md) | 设备实测范围和已知限制 |
| [软件与发布验收](validation/software-acceptance.md) | 测试入口与发布候选验收清单 |
| [AMOLED 性能参考](validation/amoled-performance.md) | 内存、刷新耗时与测量条件 |
| [ePaper 刷新参考](validation/epaper-refresh.md) | 局刷数据及光学验证边界 |
| [接入架构](design/open-device-access.md) | 状态归属、连接与更新生命周期 |
| [接入协议](../protocol/open-device-access.md) | 设备线上字段与校验约束 |
| [硬件移植](PORTING.md) | 新目标目录与硬件验收 |
| [设计语言](design/amoled-ui.md) | AMOLED 界面规范 |
| [依赖管理](firmware-dependency-locks.md) | 锁文件、Git 行尾与升级流程 |
| [发布流程](release/FIRMWARE_RELEASE.md) | 版本、资产和站点部署 |
