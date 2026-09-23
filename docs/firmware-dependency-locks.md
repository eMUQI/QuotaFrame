# 固件依赖与可复现构建

各生产目标和测试工程提交 `dependencies.lock`，由 ESP-IDF Component Manager 记录精确版本、来源与组件哈希。请使用 Target Registry 指定的 SDK；不要手工修改锁文件中的哈希。

## 首次配置

ESP-Mosaico 的生产目标在 `main/idf_component.yml` 中显式声明 LVGL 和显示适配器。BSP 的显示依赖使用 Kconfig 条件，而干净目录首次配置时尚无 `sdkconfig`；显式声明保持首次与后续配置的直接依赖集合一致，避免重新求解并改写锁文件。修改这些声明后需核对干净配置和增量配置，不能只验证已有构建目录。

## Git 行尾

Git 来源组件的完整性检查依赖检出字节。LF 与 CRLF 转换可能导致相同上游提交出现不同哈希。CI 与本地依赖解析应使用 LF 检出；组件管理器的独立缓存不会继承主仓库的 local Git 配置。

可以为当前 PowerShell 进程设置 Git 配置，不修改用户全局偏好：

```powershell
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'core.autocrlf'
$env:GIT_CONFIG_VALUE_0 = 'false'
eim run "idf.py -C firmware/targets/m5sticks3 build" v6.1
```

如果当前进程已有 `GIT_CONFIG_COUNT` 条目，应追加配置而不是覆盖。保留原有安全目录配置；镜像构建后核对实际版本，避免 Git 访问失败导致版本回退。

## 完整性错误

出现 `The downloaded component ... is corrupted` 时：

1. 核对 SDK 版本和 `git config --show-origin --get core.autocrlf`。
2. 对照目标 manifest 与锁文件，确认来源、版本和提交。
3. 在正确行尾配置下重新取得受影响依赖，避免复用错误缓存；不要删除源码或其他目标的构建目录。
4. 重建受影响工程，确认锁文件没有非预期变化。

## 更新依赖

先修改 `idf_component.yml` 的明确约束，再由指定 SDK 的 Component Manager 重新求解锁文件。检查依赖版本、许可证、构建及相关设备行为。普通构建和行尾修复不应顺带升级依赖。

Buddy 的输入校验包装依赖固定的私有布局与源码哈希。升级该依赖还需按[接入架构](design/open-device-access.md#输入校验)检查包装边界，不能只更新提交号。

参考：[锁文件说明](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/dependencies_lock.html)、[版本求解](https://docs.espressif.com/projects/idf-component-manager/en/latest/use/explanation_version_solver.html)。
