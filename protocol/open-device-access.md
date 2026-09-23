# 开放设备接入契约

基础协议版本：1。实机验收范围见 [verification.md](../docs/verification.md)。

本文定义线上字段，与[架构说明](../docs/design/open-device-access.md)及[验证说明](../docs/verification.md)配套。采用 Buddy 命令外壳及已有用量编码，扩展自有 status 数据和固件项目校验。常用消息与官方固件行为见[协议示例](README.md)；Bridge、固件与样例共同遵循本契约。

## 1. 最小设备

设备至少实现 QF-<非空短名称> 广播、NUS、bonding / LE Secure Connections / MITM及可显示配对码、安全JSONL、status与usage.v1。不要求官方target、特定芯片、页面、时钟或OTA；不降级Just Works。

| 项目 | 标识 |
| --- | --- |
| NUS service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX：Bridge写入 | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX：设备通知 | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

名称与UUID只用于候选预筛，不能代替安全配对或身份认证；允许广播重名，Bridge按平台地址区分。

## 2. 帧与关联规则

| 项目 | 规则 |
| --- | --- |
| 帧 | 一个UTF-8 JSON对象后接LF；支持跨GATT分片和合并通知 |
| 上限 | 完整JSON≤4096字节，含全部外壳，不含行结束符 |
| 行结束 | 发送LF；接收容忍CRLF并忽略空行，跨分片边界规则一致 |
| 在途请求 | 每连接同一时刻一个，包括status和OTA查询 |
| 常规分片 | 不超过180字节且受ATT容量约束，使用有响应写入 |
| OTA chunk分片 | 可采用现有最多512字节无响应写入；完整块仍需应用ACK |
| ACK等待 | 普通命令/status默认5秒；超时后关闭连接，不在同一连接继续发送以消耗迟到响应 |
| 缺省字段 | 按字段约定省略，不用null或随意哨兵代替 |

拒绝非法UTF-8、孤立代理项、非对象JSON、重复键、尾随垃圾、非法类型和超长帧。重复键按解码后键名判断，覆盖嵌套对象。未完成的短片段等待LF；超限或非法帧失败并清理，不截断接受或无限累计。本仓在 Buddy 路由之前设置严格校验边界，固定依赖与适配方式见[输入校验](../docs/design/open-device-access.md#输入校验)。

### 2.1 三种请求及其响应

| 请求族 | 请求字段 | 响应与关联 |
| --- | --- | --- |
| status | 只有cmd=status | ack=status、ok、n=0，成功含data；关联当前串行查询 |
| usage/time_sync/screen_toggle/screen_page/ota_abort | cmd、字符串v="1"、字符串seq及参数 | ack匹配cmd，ok为boolean，数字n精确匹配seq的数值 |
| Folder Push | char_begin/file/chunk/file_end/char_end各自参数 | ack匹配当前cmd；成功n是0或文件累计字节数，见第7节；不添加seq/id |

扩展ACK的n由处理器提供；status核心ACK的n固定为0。各请求族按上表关联。

普通seq为uint32规范十进制字符串，可按现有分配器回绕；它只关联命令，不是样本版本或全局设备ID。n必须是JSON整数且在uint32范围，不能是字符串、boolean、小数或指数形式。

合法但不匹配的响应不能完成当前请求或延长超时。通知回调必须属于当前transport对象；旧连接回调不作用于新连接。无序号status和Folder Push不提供跨请求去重或恰好一次保证，依赖串行发送、成功响应后再发下一条、超时断连和不重放不确定命令。

### 2.2 数值编码

普通命令的v/seq、用量数值、日历字段使用ASCII规范十进制字符串：0合法，其余无前导零；拒绝空白、空串、正号、小数、指数、Unicode数字及越界值。除utc_offset_min外均为非负值，uint32为0..4294967295。

utc_offset_min允许负号，范围-840..840，拒绝负零与前导零。解析时用字符串严格检查；不能经Buddy的get_u32读取有符号值。

Folder Push total/size、manifest.size、status.protocol、ACK.n保留各自JSON整数类型。Buddy get_u32会钳位负数和溢出、截断小数，不能证明原始输入合法；实现必须在信息丢失前验证这些字段。本仓校验在 Buddy getter 之前完成，见[输入校验](../docs/design/open-device-access.md#输入校验)。

## 3. status数据

```json
{"cmd":"status"}
{"ack":"status","ok":true,"n":0,"data":{"name":"书桌额度屏","sec":true,"protocol":1,"caps":["usage.v1"]}}
```

| data字段 | 类型 / 必需性 | 规则 |
| --- | --- | --- |
| name | 必需string | 1..64 Unicode码点且UTF-8≤128字节；无首尾Unicode空白、C0/C1控制、U+2028/U+2029、Bidi_Control；支持中文、内部空格、引号和反斜杠 |
| sec | 必需boolean | 必须为true；自报位不替代实际配对及GATT安全权限 |
| protocol | 必需JSON整数 | 精确为1，排除boolean和非整数形式 |
| caps | 必需array[string] | 1..32项，每项1..64 ASCII字节，总token字节≤512；`[A-Za-z0-9._-]+`，禁止重复、空项与空白 |
| firmware_project | 无OTA时可选string | 1..64 ASCII字节，`[a-z0-9][a-z0-9._-]*`；固件项目标识 |
| target | 无OTA时可选string | 1..31 ASCII字节，同上字符规则；可互刷的兼容类 |
| fw | 无OTA时可选string | 1..31可打印ASCII字节，排除引号和反斜杠；OTA时符合版本规则 |
| page | 可选string | 1..31 ASCII字节，`[A-Za-z0-9._-]+`，设备自定义页面状态，不是官方枚举 |
| ota | 声明OTA时必需object | 见第7节 |
| boot_valid | 可选boolean | OTA完成必须观察到true；缺省不能当成功 |

名称不自动trim、截断、大小写折叠或由广播名补齐；正确JSON转义，UI按纯文本显示。长度按解码后码点和UTF-8字节计，编码后的完整帧另检查4096上限。

未知合法能力忽略其行为，保留数据；未知基础protocol或缺usage.v1为明确不兼容。已知字段非法则拒绝整条status，不保存部分元数据。未知status字段可忽略；已知可选字段存在时必须合法，不能以空串/null替代缺省。

| 能力 | 行为 |
| --- | --- |
| usage.v1 | 必需，理解Codex/Claude摘要，显示布局自主 |
| time.sync.v1 | 本地日历同步 |
| screen.toggle.v1 | 设备本地屏保切换 |
| screen.page.v1 | 设备本地上一页/下一页，不要求提供page |
| ota.folder.v1 | 应用OTA；必须有firmware_project、target、fw、ota |

不要求第三方使用Buddy，但必须满足同一线上契约。

## 4. usage

```json
{"cmd":"usage","v":"1","seq":"42","provider":"codex","state":"ok","sampled_at":"1789776000","sent_at":"1789776002","short_used_pct":"36","short_reset_at":"1789786800","week_used_pct":"61","week_reset_at":"1790175600"}
{"ack":"usage","n":42,"ok":true}
{"cmd":"usage","v":"1","seq":"43","provider":"claude","state":"partial","sampled_at":"1789776000","sent_at":"1789776002","week_used_pct":"25"}
{"ack":"usage","n":43,"ok":true}
{"cmd":"usage","v":"1","seq":"44","provider":"claude","state":"unavailable","sampled_at":"1789776060","sent_at":"1789776061"}
{"ack":"usage","n":44,"ok":true}
```

| 参数 | 规则 |
| --- | --- |
| provider | codex或claude，独立保存 |
| state | ok / partial / unavailable |
| sampled_at、sent_at | 必需uint32十进制字符串，分别为数据源采样/尝试时间与Bridge编码时间，UTC Unix秒 |
| short_used_pct、week_used_pct | 条件必需，0..100十进制字符串，表示已使用百分比 |
| short_reset_at、week_reset_at | 可选uint32字符串，只能随对应百分比存在 |

ok恰有两个窗口；partial恰有一个窗口；unavailable不含百分比或reset。0是真实数值，未知reset省略。要求sampled_at≤sent_at+300，计算避免整数溢出。

Bridge取数失败而有历史样本时继续发送原sampled_at；设备收到unavailable保留最后有效数据，无历史时显示无数据。不能以新sent_at掩盖陈旧，不按reset自行清零。额度数值、陈旧和断线分别表达。

ACK表示校验并接受到可处理状态，不等待慢速屏幕刷新。重连可发送最新快照，不承诺恰好一次投递。

## 5. 时间与屏幕

每条请求含cmd、v="1"、seq字符串，ACK.n回送seq数值。

| 命令 | 其他参数 | 语义 |
| --- | --- | --- |
| time_sync | year 2024..2099、month 1..12、合法day、weekday 0..6（周日0）且与日期一致、hour 0..23、minute/second 0..59、utc_offset_min -840..840；均为规范十进制字符串 | 同步本地日历，设备负责本地计时 |
| screen_page | previous="1"表示上一页，previous="0"表示下一页 | 设备决定页面集合、顺序和循环边界；无变化也可接受成功 |
| screen_toggle | 无其他参数 | 切换设备自己的屏保/主视图 |

```json
{"cmd":"screen_page","v":"1","seq":"45","previous":"1"}
{"ack":"screen_page","n":45,"ok":true}
```

Bridge不要求page等于overview/codex/claude；可选page只展示，不驱动能力判断。没有对应能力则不发送。按键/手势在本地处理，屏幕操作不为离线设备排队，ACK丢失后不自动重放。OTA忙时按设备处理器规则返回ota_busy，status及ota_abort仍可用。

## 6. 错误与远程解绑

```json
{"ack":"screen_page","n":45,"ok":false,"error":"unsupported"}
```

error是稳定短码，不返回凭据或原始内部错误。普通命令沿用invalid_request、unsupported、ota_busy、queue_full等；OTA保留现有错误映射，项目不匹配通过稳定的project_mismatch错误报告，不能伪造成功。

有效seq的普通失败ACK仍回送该值；无法解析seq时处理器可按Buddy约定返回n=0的invalid_request，但这不代表成功或任意请求均可被匹配。错误消息或JSON无法解析时允许丢弃/终止，不能生成成功确认。

明确拒绝与超时/断线分开处理；失败不无限重试。status失败响应无data也有效，Bridge结束查询并按现有错误路径清理连接。

固件不注册on_unpair。收到 `{"cmd":"unpair"}` 时Buddy返回ack=unpair、ok=false、n=0、error=unsupported，设备bond保持不变。保留设备本地明确触发的恢复方式；Bridge忘记设备不发送unpair。其他未使用的Buddy回调保持未配置，不扩大远程控制面。

## 7. Folder Push应用OTA

### 7.1 项目、兼容性和状态

firmware_project区分同硬件上的不同固件；target表示硬件修订、分区和引导条件允许互刷的兼容类。两者是自述，不能认证设备或发布者。设备project_mismatch只能防止正常实现误刷，不能阻止伪造项目的设备自行接受镜像。

fw为1..31字节SemVer，可有一个前导v供比较时规范化；目录version无前导v。支持同项目、精确target、更高版本的应用更新，不提供跨项目安装、降级或重装。

| ota字段 | 类型与规则 |
| --- | --- |
| phase | idle / confirming / receiving / verifying / rebooting |
| off | uint32规范十进制字符串，已接收应用镜像字节数 |
| size | uint32规范十进制字符串，本次应用大小；无传输时为"0" |
| err | 空串表示无错误；或denied、timeout、too_large、bad_image、link_lost、seq_gap、low_power、project_mismatch、target_mismatch |

off≤size；失败可保留诊断状态，但不允许据此自动续传。status.off只计应用，Folder Push的ACK.n是当前文件计数，不能混用。

### 7.2 manifest与命令

transfer name固定firmware；仅按顺序接收manifest.json和firmware.bin。manifest为无LF的UTF-8 JSON，最多512字节，分配缓冲时另留解析需要的终止符。

| manifest字段 | 规则 |
| --- | --- |
| schema | 字符串"1" |
| firmware_project | 与当前设备及选定产物项目相同 |
| target | 精确兼容类 |
| version | 规范SemVer |
| size | 正JSON uint32整数，应用字节数 |
| sha256 | 应用文件字节的SHA-256，64位小写十六进制 |

六字段均必需；拒绝未知字段、重复键及错误类型。项目验证在仓库维护的OTA sink完成，不修改Buddy外壳。目录、桌面确认及来源显示不作为设备授权字段发送。

| 命令 | 请求参数 | 成功ACK.n |
| --- | --- | --- |
| ota_abort | v="1"、seq字符串 | seq对应数值，属于普通命令 |
| char_begin | name="firmware"、total为正JSON uint32，等于两文件总字节 | 0 |
| file | path="manifest.json"或"firmware.bin"，size为正JSON uint32 | 0 |
| chunk | d为standard padded base64，无offset/id | 当前文件接受后的累计解码字节数 |
| file_end | 无其他参数 | 当前文件完整字节数 |
| char_end | 无其他参数 | 0 |

Folder Push响应只有ack、ok、n及失败error。失败n可以为0或当前文件计数；匹配当前ack且n为合法uint32时立即报告拒绝，不能按成功进度过滤失败。

manifest的file_end验证后进入confirming，设备逐次物理确认且满足供电条件后才能接收firmware.bin。每文件从0计数，每块等待ACK。不确定块不重发，不跨断线恢复文件状态。char_end成功表示验证并安排切换启动，不表示新固件已经健康运行。

### 7.3 容量和完成条件

完整chunk的base64和外壳仍受4096字节上限；当前每块最多解码2880字节，不能把原始字节数等同JSON长度。整应用容量按真实OTA分区，传输上限还包括manifest；容量以各目标分区表为准。

设备独立检查文件大小、项目/target、镜像结构和摘要；只写非活动应用分区，不远程改bootloader/分区表。设备确认默认60秒、接收无进展10秒或链路断开即终止会话；执行总期限600秒、重启验证60秒，清理最长5秒；下载和桌面确认不计入设备执行阶段。

传输中断或取消后终止本次协程，不能自动续传或自动再安装。预期重启后检查同一当前session对应设备的项目、target、版本和boot_valid=true才报成功。观察超时报告结果未确认，不等同于设备一定没写入。

本地启动检查不能依赖电脑在线；回滚和断电恢复必须实测。[乐鑫OTA说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)

## 8. 实施与隐私边界

Buddy getter 调用前的原始输入校验与固定版本包装见[接入架构](../docs/design/open-device-access.md#输入校验)。

仅传送协议白名单摘要、公开固件和必要manifest，不发送账号、凭据、邮箱、组织、套餐、余额或原始采集错误。地址、名称及桌面确认不进入usage。

验收覆盖三种关联规则、迟到/旧transport响应、严格数值、重复键、UTF-8与4096边界、无target最小设备、无page翻页、项目检查、远程unpair无副作用及OTA中断。可执行样例位于 `protocol/examples/`，解析边界测试位于 Bridge 与固件协议测试工程。
