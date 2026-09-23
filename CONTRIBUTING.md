# Contributing to QuotaFrame

欢迎提交可复现的问题、文档改进和范围明确的 Pull Request。开始较大的功能或新设备适配前，请先在 [Issues](https://github.com/eMUQI/QuotaFrame/issues) 说明用途、硬件和维护计划。

## 开发入口

- [Bridge 开发指南](bridge/DEVELOPMENT.md)：Python 3.11+，Windows 与 macOS 的源码运行和打包。
- [固件](firmware/README.md)：EIM、目标工程与 ESP-IDF 构建。
- [Web](web/README.md)：Node.js、浏览器烧录器及站点组装。
- [协议](protocol/open-device-access.md)与[架构](docs/design/open-device-access.md)：第三方设备接入和服务边界。
- [硬件移植](docs/PORTING.md)：维护目标登记和实机要求。

从仓库根目录运行 Bridge 测试：

```sh
python -m pip install -e ./bridge
python -m unittest discover -s bridge/tests -v
```

Web 改动按 Web README 运行检查和测试。固件改动使用目标指定的 SDK 增量构建，涉及显示、输入、配对或 OTA 时补充相应实机观察。纯文档修改检查本地链接与 `git diff --check` 即可。

## 提交与审查

- 保持修改范围明确，说明问题、最终行为、验证方式与尚未验证的部分。
- 不提交账号数据、凭证、设备绑定、真实用量日志、构建输出或本机配置。测试数据使用合成值。
- 保留第三方版权、许可证与来源；依赖升级同步更新锁文件和分发声明。
- 注释解释当前约束及非显然行为，避免记录讨论过程或已删除的实现。
- 新维护目标需要维护者能够持续取得硬件并验证。未满足条件的设备可先独立实现公开接入协议。

安全问题请按 [SECURITY.md](SECURITY.md) 报告，避免在公开 Issue 中附凭证或可直接利用的敏感细节。提交代码应与项目 [MPL-2.0](LICENSE) 许可兼容。
