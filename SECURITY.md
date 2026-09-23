# Security

QuotaFrame 通过已配对的加密 BLE 连接传输用量数据。账号登录与凭证存储由 CodexBar / Win-CodexBar 负责；报告问题时请勿上传 Token、Cookie、完整账号输出或真实设备绑定文件。

## 报告漏洞

请使用仓库 Security 页面的 **Report a vulnerability** 私下报告问题：

[Private vulnerability reporting](https://github.com/eMUQI/QuotaFrame/security/advisories/new)

请说明受影响版本、平台、设备型号、最小复现步骤与预期影响，并尽量使用合成数据。若该入口不可用，可先提交不含漏洞细节的 Issue，请维护者提供私下沟通方式。

## 安全边界

- OTA 要求桌面确认与设备物理确认，并校验项目、型号、大小和 SHA-256；摘要提供完整性检查，不是发布者签名。
- 更新依赖 GitHub 发布权限与 HTTPS 下载渠道。不要安装来源不明的固件或运行不可信的 Bridge 构建。
- 设备自报名称、项目和型号不是制造商身份认证。
- macOS 分发使用 ad-hoc 签名，尚无 Developer ID 签名或 notarization。

修复优先针对当前维护版本。验收范围与仍需测试的故障场景见[验证说明](docs/verification.md)。
