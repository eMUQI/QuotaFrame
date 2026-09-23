# usage.v1 / ota.folder.v1 BLE Protocol

完整字段、发现和第三方接入约束见 [开放设备接入契约](open-device-access.md)。本文提供常用消息与官方固件行为说明；软件检查及实机验收边界见 [验证说明](../docs/verification.md)。

## Transport

Windows/macOS Bridge 作为 Central，经已配对、已加密的 NUS 连接发送 UTF-8 JSONL，每连接只允许一个在途请求。UUID、分片、ACK 关联和严格输入规则见[接入契约](open-device-access.md#2-帧与关联规则)。

## Usage command

```json
{"cmd":"usage","provider":"codex","sampled_at":"1785398400","sent_at":"1785398402","seq":"42","short_reset_at":"1785409200","short_used_pct":"36","state":"ok","v":"1","week_reset_at":"1785798000","week_used_pct":"61"}
```

字段、数值编码和 `ok` / `partial` / `unavailable` 组合见[usage 契约](open-device-access.md#4-usage)。

成功 ACK：

```json
{"ack":"usage","n":42,"ok":true}
```

失败 ACK：

```json
{"ack":"usage","error":"invalid_request","n":42,"ok":false}
```

Bridge 取数失败时继续发布该服务最后有效数据，保留原始状态、窗口和 `sampled_at`；尚无有效数据时发布 `unavailable`。固件收到 `unavailable` 时同样保留最后有效样本。仅蓝牙断开或从未取得有效数据时显示灰态，样本年龄和刷新失败不改变已有数据的显示。成功取数后立即采用新结果，Codex 与 Claude 独立更新。

## Status

请求：

```json
{"cmd":"status"}
```

仅实现基础用量能力的设备响应：

```json
{"ack":"status","n":0,"ok":true,"data":{"caps":["usage.v1"],"name":"书桌额度屏","protocol":1,"sec":true}}
```

Bridge 必须确认 `sec=true`、`protocol=1` 和 `caps` 包含 `usage.v1`。

### 协议与能力校验

`protocol` 必须为整数 `1`，设备必须声明 `usage.v1`。能力 token 描述可选功能：OTA、时钟切换和屏保分别按设备声明启用；未知合法能力可被忽略。

安全 status 中的整数协议版本不受支持，或缺少必需的 `usage.v1` 时，Bridge 将其识别为明确不兼容：关闭连接并暂停该设备的自动重试，托盘显示“需更新 Bridge/固件”。更新到相互兼容的版本后重启 Bridge；已认领设备的显式连接重置也可恢复尝试。首次发现的不兼容设备不会写入 `devices.json`，本次运行跳过该地址并显示“新设备需更新 Bridge/固件”。普通掉线继续按原退避策略重连，畸形 status 不据此认定为已确认的版本不兼容。

支持 OTA 的固件还返回精确 target、固件版本和 OTA 状态：

```json
{"ack":"status","data":{"caps":["usage.v1","ota.folder.v1"],"firmware_project":"quotaframe","fw":"0.9.0","boot_valid":true,"name":"M5 Usage Panel","ota":{"err":"","off":"0","phase":"idle","size":"0"},"page":"overview","protocol":1,"sec":true,"target":"m5sticks3"},"ok":true,"n":0}
```

可选身份字段、OTA 阶段和错误码见[接入契约](open-device-access.md#3-status数据)。

`boot_valid` 表示本次启动已经完成本地检查：显示初始化与首次渲染完成、主循环运行，且 BLE 已同步并正在广播或已有连接。待确认 OTA 应用还必须成功写入有效状态。Bridge 只有同时观察到同一认领设备的预期项目、target、版本和 `boot_valid=true` 才报告升级成功；不要求电脑在线或发送真实用量才能完成本地启动检查。

## Screen toggle

设备通过 `screen.toggle.v1` 声明远程屏保切换能力。Bridge 仅向已连接、已加密且声明该能力的设备发送命令：

```json
{"cmd":"screen_toggle","v":"1","seq":"42"}
```

`v` 固定为字符串 `"1"`，`seq` 为规范十进制 `uint32` 字符串。成功响应为 `{"ack":"screen_toggle","n":42,"ok":true}`，表示一次切换事件已进入设备主循环队列。设备根据自己的当前状态进入时钟屏保或恢复原页面；解锁重新开始空闲计时，超时自动屏保、按键、触摸及动作唤醒规则继续生效。

错误为 `invalid_request`、`unsupported`、`ota_busy` 或 `queue_full`。OTA 期间不执行切换。该命令不具备幂等性，ACK 丢失或断线后的执行结果可能未知，Bridge 不自动重发，也不为离线设备保留点击。

## Firmware OTA over Folder Push

OTA 复用 Espressif Folder Push 命令，不新增 GATT Service。OTA sink 只接受名为`firmware` 的 transfer，并要求两个扁平文件严格按顺序出现：

```text
firmware/
├── manifest.json
└── firmware.bin
```

Bridge 根据发布 manifest 和已校验的本地镜像生成本次传输的 `manifest.json`；其内容是 UTF-8 JSON，不带行尾：

```json
{"schema":"1","firmware_project":"quotaframe","sha256":"abababababababababababababababababababababababababababababababab","size":804896,"target":"m5sticks3","version":"0.9.0"}
```

线上的完整顺序如下。`chunk.d` 是 standard padded base64；示例中的 size 和 total 是解码后的字节数，`total = manifest bytes + firmware bytes`。

```json
{"cmd":"char_begin","name":"firmware","total":805071}
{"cmd":"file","path":"manifest.json","size":175}
{"cmd":"chunk","d":"<base64 manifest>"}
{"cmd":"file_end"}
{"cmd":"file","path":"firmware.bin","size":804896}
{"cmd":"chunk","d":"<base64 firmware block>"}
{"cmd":"file_end"}
{"cmd":"char_end"}
```

容量、文件顺序、ACK 计数、分片和超时见[Folder Push 契约](open-device-access.md#7-folder-push应用ota)。官方实现的执行行为：

- manifest 校验后进入物理确认阶段，Bridge 每 500 ms 查询 status，直到 `receiving` 才发送固件。
- 接收期间 usage 返回 `ota_busy`。`char_end` 仅在镜像结构与 SHA-256 通过后切换启动分区。
- 新镜像完成本地启动检查后确认有效；BLE 启动超过 10 秒仍未就绪或有效状态写入失败时重启，待确认应用由 bootloader 在下一次启动时回滚。

为了清理断线后可能残留的 Folder Push 状态，设备保留一个应用专用恢复命令：

```json
{"cmd":"ota_abort","seq":"80","v":"1"}
```

它使用 `seq` 匹配 ACK，不承载固件数据。Bridge 在开始传输前发送一次；失败或取消时，仅在现有连接仍有效的情况下，在最多 5 秒的清理期限内尝试查询错误并中止；不为清理重新连接。单次升级默认总期限为 600 秒，包含预检、传输、物理确认与重启验证；物理确认和重启验证各最多 60 秒。常驻设备会话可继续退避重连，单条失败命令立即向上报告，结果未知的固件块不会自动重发。

## Privacy allowlist

usage 协议只允许 provider、百分比、重置时间、采样/发送时间、状态和序号。OTA 只允许公开固件字节以及 `schema`、`firmware_project`、`target`、`size`、`sha256`、`version`和恢复命令的 `seq`。禁止传输 Token、Cookie、API Key、邮箱、账户/组织标识、套餐、费用、余额、原始错误、Win-CodexBar source 信息。Bridge 编码测试会检查最终 JSON key 集合。

首发采用**无签名 OTA**。manifest 的 SHA-256 只能发现下载/传输损坏，不能证明发布者身份；发布权限、GitHub 账号和 TLS 是剩余信任边界。物理确认防止静默刷写，但不能判断用户确认的字节是否来自真正发布者。

## 屏幕翻页（screen.page.v1）

设备在 `caps` 中声明 `screen.page.v1` 后，Bridge 可发送：

```json
{"cmd":"screen_page","v":"1","seq":"42","previous":"0"}
```

`previous` 为十进制字符串 `"0"`（下一页）或 `"1"`（上一页），其他值拒绝。页面集合、顺序与循环边界由设备决定；官方固件处于时钟时先回到额度页面，设置页若有未保存的预览则撤销。该能力可选，是否支持以 status 的 `caps` 为准。

ACK 表示已入应用事件队列；不表示屏幕已完成刷新。未启用、OTA 占用、无效参数和队列满分别返回 `unsupported`、`ota_busy`、`invalid_request`、`queue_full`。配对或 OTA 期间应用层不执行翻页。相对翻页不是幂等操作，ACK 丢失或断线后不自动重发，也不为离线设备排队。
