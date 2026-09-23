# QuotaFrame 发布流程

固件与 Bridge 使用同一规范 SemVer。源码、CI 和 Release 统一位于公开仓库 `eMUQI/QuotaFrame`。发布工作流从 `GITHUB_REPOSITORY` 读取目标，客户端打包时嵌入同一仓库的 `release-channel.json`；源码运行使用 `QUOTAFRAME_RELEASE_REPO=eMUQI/QuotaFrame`。

## 1. 仓库配置

| 配置 | 类型 | 用途 |
| --- | --- | --- |
| `GITHUB_TOKEN` | GitHub Actions 自动提供 | 仅发布 job 使用 `contents: write` 创建、校验和公开本仓库 Release |
| `CLOUDFLARE_PAGES_DEPLOY_HOOK` | Repository secret | 稳定版发布后触发站点重建 |

无需配置 `RELEASE_REPO` variable 或 `RELEASE_TOKEN` secret。其他构建 job 仅有 Contents 读取权限，网站刷新 job 不申请仓库权限。仓库必须公开，客户端和网站通过匿名下载访问资产，不携带凭证。

发布 tag 必须已经存在并指向实际构建提交；创建 Release 使用 `--verify-tag`，不自动创建 tag。GitHub 自动生成的源码归档与同仓库发布 tag 对应。

首次部署应先发布完整 Release，再连接网站生产构建；否则网站因没有可下载的固件而构建失败。Cloudflare 配置见 [Web README](../../web/README.md)。更改发布渠道时需重新构建安装包和 manifest，并同时更新网站固件来源与下载链接。

OTA 使用 SHA-256 检查完整性，不提供发布者签名认证；信任边界包括发布权限、账号与 HTTPS 渠道。发布前完成[候选验收](../validation/software-acceptance.md)，记录源码版本、资产摘要和未覆盖项目。

## 2. 构建发布候选

在 GitHub Actions 的 Release 工作流中选择 `Run workflow`，或运行：

```sh
gh workflow run release.yml --repo eMUQI/QuotaFrame --ref main
```

手动运行使用源码中的 Bridge 版本，复用正式发布的测试、固件与桌面构建及资产装配步骤，将完整资产上传为 `quotaframe-v<version>-candidate` Actions artifact，保留 7 天。它不创建 tag 或 GitHub Release，也不触发网站部署。候选包中的渠道和 manifest URL 已指向正式仓库；正式 Release 公开前这些下载地址尚不可用。

核对候选资产与 SHA-256 后，记录本次运行的源码提交，并完成平台和设备验收。正式发布须从已验收的源码提交创建 tag；正式工作流会重新构建，发布资产仍需独立核对。

## 3. Publish by pushing a tag

发布前先把 `bridge/src/quotaframe_bridge/__init__.py` 中的 Bridge 版本设置为不带前导 `v` 的规范化 SemVer，例如 `0.6.0` 或 `0.7.0-alpha.1`，并写好 `docs/release/notes/v<version>.md`——该文件就是 Release 正文，preflight 会在构建前检查它存在。提交后在同一 commit 上创建匹配的 tag：

```bash
git tag v0.6.0
git push origin v0.6.0
```

`.github/workflows/release.yml` 会自动完成以下工作：

1. 校验 tag、Bridge 内嵌版本、`docs/release/notes/v<version>.md` 和发布仓库配置；tag 去掉 `v` 后必须与 Bridge 版本完全一致；
2. 从 Target Registry 生成 firmware matrix，用同一版本构建全部 maintained production target，以及 Windows Bridge 和 Apple Silicon Bridge；
3. 用跨平台装配器生成当前 registry 对应的完整资产集合（装配器会拒绝内嵌版本与发布版本不一致的固件），先上传为 draft；
4. 重新下载 draft，将每个文件与本次装配目录逐字节比对，再运行 `sha256sum --check SHA256SUMS.txt`；已公开的同名版本只核验，任何字节差异都会失败，不覆盖公开文件；
5. 全部通过后公开 Release，并以匿名请求验证 `manifest.json` 可下载；
6. 对稳定版，在 Release 公开并通过匿名校验后触发 Cloudflare Pages 的 Deploy Hook，由网站构建重新装配站点。预发布版不会更新站点。

装配器按 registry 的 `image_chip_id` 检查 OTA 镜像内的芯片身份（ESP32-S3 为 9，ESP32-S31 为 32）、项目名和版本，并要求它能放入两个 app 分区；镜像大小不得超过该型号较小的 OTA app 分区，即 registry 的 `ota_partition_bytes`。Folder Push 总传输限额在分区容量之外预留 512 字节给 manifest，Bridge、固件配置与发布装配器保持一致。full 镜像在 `ota_0` 偏移处必须包含相同的 app 字节。`SHA256SUMS.txt` 覆盖所有其他资产，包括 manifest 和许可证。

首发基线为 registry 中的全部 target、基础协议 1、`ota.folder.v1` 和各 target 的 `partitions.csv`。OTA 只更新 app，不更新 bootloader 或分区表。改变这些安装条件时需重新决定烧录方式。候选验收应记录源码 commit 和资产 SHA-256；重新构建出的同版本文件不能沿用原字节的验收结论。Windows 与 macOS 均已实现 BLE OTA 菜单；各平台、各目标的实机 OTA 验收范围见 [verification.md](../verification.md)。

维护中的 target 元数据只登记在 `bridge/src/quotaframe_bridge/targets.py`。Release workflow、artifact 名称、manifest target 和装配器都从同一份 registry 派生。

带预发布后缀的 tag 会创建 prerelease；无后缀 tag 会创建稳定 Release。稳定版 Bridge 只发现比自身更新的稳定 Release；alpha、beta 或 rc Bridge 可以发现比自身更新的 prerelease 或稳定 Release。

刷新 job 独立运行，不读取发布令牌，也不申请任何 workflow 权限；它只负责触发重建，站点构建时从公开仓库读取 latest stable Release。未配置 `CLOUDFLARE_PAGES_DEPLOY_HOOK` 时只记录提示并跳过，推送 `main` 仍会触发构建。站点尚未部署时，不应把 Web 地址表述为已经可用。

## 4. Release asset contract

以当前 registry 为例，上传资产为：

```text
quotaframe-bridge-windows-v<version>.exe
quotaframe-bridge-windows-v<version>-setup.exe
quotaframe-bridge-macos-arm64-v<version>.dmg
m5sticks3-ota-v<version>.bin
m5sticks3-full-v<version>.bin
waveshare-esp32-s3-touch-amoled-216-ota-v<version>.bin
waveshare-esp32-s3-touch-amoled-216-full-v<version>.bin
waveshare-esp32-s3-epaper-397-ota-v<version>.bin
waveshare-esp32-s3-epaper-397-full-v<version>.bin
espressif-esp-mosaico-ota-v<version>.bin
espressif-esp-mosaico-full-v<version>.bin
manifest.json
SHA256SUMS.txt
LICENSE
THIRD_PARTY_LICENSES.md
```

固件文件名使用品牌前缀和官方完整型号，统一为小写，尺寸中的小数点省略，例如 `ESP32-S3-ePaper-3.97` 对应 `waveshare-esp32-s3-epaper-397`。M5StickS3 使用 `m5sticks3`。每个型号分别生成 `-ota-v<version>.bin` 和 `-full-v<version>.bin`。

Bridge 安装包、`manifest.json`、`SHA256SUMS.txt` 与两份许可证固定存在，其余是每个 registry target 的一份 `-ota-` 和一份 `-full-`。新增 maintained target 后不需要修改 Release workflow 或 assembler 的设备列表，只需要为 registry 填写 `release_stem`、`image_chip_id`、`ota_partition_bytes`、镜像名和工程路径。

`-ota-` 文件是 app 分区镜像；`-full-` 文件从 `0x0` 烧录，不能用于 OTA。Windows 与 macOS Bridge 独立读取配置仓库的 `releases/latest/download/manifest.json`，按设备自报项目和 target 匹配候选，不依赖 Bridge 自身是否有新版。安装前显示设备、来源、版本、大小与摘要，随后设备逐次物理确认。

目录为 `{"schema_version":1,"releases":[...]}`；每条记录含 `firmware_project`（官方为 `quotaframe`）、`target`、规范 SemVer `version`、`kind:"app"`、正整数 `size`、小写 `sha256` 和版本固定的 HTTPS `url`。同一项目/target 不重复。装配器验证的 ESP app descriptor 项目名仍为各目标工程名，与公开的 `firmware_project` 分开校验。

目录随完整资产先上传并校验，再公开 Release；latest 入口随公开发布更新。

Web 烧录器为每个启用 Web 的 target 另外生成一个 ESP Web Tools manifest。它只引用该稳定 Release 中对应的 `-full-` 镜像，写入偏移固定为 `0x0`；它与 Release 根目录供 Bridge 使用的 BLE OTA `manifest.json` 是两套不同契约，不能互换。Web 目标的名称、说明、芯片家族、设备图和完整镜像名都从 Target Registry 派生；网页不提供 menuconfig，也不会在同属 ESP32-S3 的设备之间自动猜测型号。

Bridge 自身更新的菜单行为和安装方式见 [Bridge 说明](../../bridge/README.md#bridge-更新检查)。

## 5. Manual cross-platform fallback

自动流程不可用时，先为 registry 中的每个 target 构建 OTA 镜像、完整镜像和 `project_description.json`，并确保 `project_version` 与参数版本一致。完整固件用对应工程的 `idf.py merge-bin -o <name>` 生成。

把这些文件整理成与 Actions artifact 相同的目录结构：

```text
release-inputs/
├─ firmware-m5sticks3/
│  ├─ m5_usage_panel.bin
│  ├─ m5_usage_panel_full.bin
│  └─ project_description.json
└─ firmware-waveshare_amoled_216/
   ├─ ws_usage_panel.bin
   ├─ ws_usage_panel_full.bin
   └─ project_description.json
```

新增 target 时目录名固定为 `firmware-<target-id>`，其中镜像文件名来自 Target Registry。然后在 Windows、macOS 或 Linux 上用 Python 3.11+ 运行同一个装配器。以下是 POSIX shell 示例；PowerShell 使用相同参数，只需改用其续行语法：

```bash
python scripts/assemble_release.py \
  --version 0.5.0 \
  --repository owner/releases \
  --firmware-root release-inputs \
  --windows-exe bridge/dist/quotaframe-bridge.exe \
  --windows-setup bridge/dist/quotaframe-bridge-windows-v0.5.0-setup.exe \
  --macos-dmg bridge/dist/quotaframe-bridge-macos-arm64-v0.5.0.dmg \
  --license LICENSE \
  --third-party-licenses THIRD_PARTY_LICENSES.md \
  --output release-assets-v0.5.0
```

装配器会拒绝非规范 SemVer、非法仓库名、缺失 target 文件和固件版本不匹配，并清理指定输出目录后只写入 registry 对应的资产集合。上传前核对文件数量并验证校验和：

```bash
expected=$(PYTHONPATH=bridge/src python -c 'from quotaframe_bridge.targets import TARGETS; print(7 + 2 * len(TARGETS))')
test "$(find release-assets-v0.5.0 -maxdepth 1 -type f | wc -l | tr -d ' ')" = "$expected"
cd release-assets-v0.5.0 && sha256sum --check SHA256SUMS.txt
```

人工创建的 tag、Release 标题与 Bridge 内嵌版本仍必须一致。先保存为 draft，重新下载并核对完整资产集合后再公开；不要把烧录工具或完整构建目录追加到该 tag Release。

## 6. Installation and recovery boundary

安装与 Gatekeeper 处理见 [Bridge 说明](../../bridge/README.md#安装)，USB 恢复命令见[固件说明](../../firmware/README.md#烧录和串口日志)。Release 说明需明确 macOS 仅提供 Apple Silicon、ad-hoc 签名，尚无 Developer ID 签名、notarization 或 Intel 包。

完整镜像从 `0x0` 写入，仅用于匹配型号的首次安装或串口恢复；BLE OTA 使用 app 镜像。Release 不包含 esptool、驱动或 GUI 烧录器。
