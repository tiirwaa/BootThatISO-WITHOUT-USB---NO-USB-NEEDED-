# BootThatISO!

**[🇺🇸 English](README.md) | [🇪🇸 Español](README.es.md) | [🇧🇷 Português](README.pt.md) | [🇩🇪 Deutsch](README.de.md) | [🇫🇷 Français](README.fr.md) | [🇮🇹 Italiano](README.it.md) | [🇷🇺 Русский](README.ru.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! 是一款创新的 Windows 工具，允许**从 ISO 文件启动操作系统而无需 USB 驱动器**。非常适合在没有 USB 设备的情况下使用，例如旅行、借用设备或紧急情况。它自动化了在内部磁盘上创建 EFI 和数据分区、直接读取 ISO 和文件提取以及 BCD 配置，提供直观的图形界面并支持无人值守执行。

此实用程序特别适用于：
- **快速安装**：直接从 ISO 启动以安装 Windows、Linux 或恢复工具，无需准备 USB。
- **测试环境**：测试操作系统 ISO 或实用程序而不修改外部硬件。
- **系统恢复**：访问修复工具，如 HBCD_PE 或 live 环境，而不依赖外部媒体。
- **自动化**：集成到脚本中进行批量部署或自动配置。

由 **Andrey Rodríguez Araya** 开发。

网站：[English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## 主要特性
- 在系统磁盘上创建或重新格式化 `ISOBOOT`（数据）和 `ISOEFI`（EFI）分区，提供 FAT32、exFAT 或 NTFS 格式选项。
- 支持两种启动模式：将完整 ISO 加载到磁盘或 RAMDisk 模式（boot.wim 在内存中）。
- 检测 Windows ISO 并自动调整 BCD 配置；非 Windows ISO 直接从 EFI 分区启动。
- 运行可选的完整性检查（`chkdsk`），生成详细日志，并允许取消或恢复空间。
- 提供无人值守模式，通过命令行参数进行脚本集成。
- **ISO 哈希缓存（ISOBOOTHASH）**：将 ISO 的 MD5、选定的启动模式和格式与存储在目标上的 `ISOBOOTHASH` 文件中的值进行比较。如果匹配，则跳过格式化和内容复制以加快重复运行。

## 已测试的 ISO

### RAM 模式（从内存启动）
- ✅ HBCD_PE_x64.iso（完全功能 - 从 RAM 加载所有程序）
- ✅ Win11_25H2_Spanish_x64.iso（完全功能 - 启动和安装）
- ✅ Windows10_22H2_X64.iso（完全功能 - 启动和安装）

### EXTRACT 模式（完整安装）
- ✅ HBCD_PE_x64.iso（回退到 ISOBOOT_RAM）
- ✅ Win11_25H2_Spanish_x64.iso（回退到 ISOBOOT_RAM）
- ✅ Windows10_22H2_X64.iso（回退到 ISOBOOT_RAM）

## 要求
- 具有管理员权限的 Windows 10 或 11 64 位。
- `C:` 驱动器上至少有 12 GB 的可用空间以创建和格式化分区（工具尝试缩减 12 GB）。
- PowerShell、DiskPart、bcdedit 和可用的 Windows 命令行工具。
- 用于编译：带 CMake 的 Visual Studio 2022。不需要外部包管理器；7‑Zip SDK 包含在 `third-party/` 下。

## 编译
```powershell
# 配置和构建（VS 2022，x64）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

最终可执行文件位于 `build/Release/BootThatISO!.exe`。还包含具有等效步骤的 `compilar.bat`。

### 使用 compilar.bat 快速编译（推荐）
```powershell
# 在仓库根目录
./compilar.bat
```

## 使用
### 图形界面
1. **以管理员身份**运行 `BootThatISO!.exe`（清单已请求）。
2. 选择 ISO 文件并为 `ISOBOOT` 选择文件系统格式。
3. 定义启动模式：
   - `完整安装`：将整个 ISO 内容复制到磁盘。
   - `从内存启动`：复制 `boot.wim` 和依赖项以从 RAM 启动。
4. 决定是否运行 `chkdsk`（取消选中则跳过验证以加快过程）。
5. 点击**创建可启动分区**并通过主进度条、详细进度条和日志面板监控进度。
6. 完成后，将出现重启确认。如果需要删除 `ISOBOOT`/`ISOEFI` 分区并扩展 `C:`，请使用**恢复空间**按钮。
7. **服务**按钮打开支持页面 `https://agsoft.co.cr/servicios/`。

### 无人值守模式
使用提升的权限和以下参数运行二进制文件：

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\路径\映像.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|zh_cn
```

- `-mode=RAM` 激活*从内存启动*模式并复制 `boot.wim`/`boot.sdi`。
- `-mode=EXTRACT` 对应*完整安装*。
- `-chkdsk=TRUE` 强制磁盘验证（默认省略）。
- `-lang` 设置与 `lang/` 下文件匹配的语言代码。
- `-autoreboot` 可用于将来的自动化；目前仅记录偏好。

该过程记录事件并在不显示主窗口的情况下退出。

## 日志和诊断
所有操作在 `logs/`（与可执行文件一起创建）中生成文件。最相关的包括：
- `general_log.log`：一般事件时间线和 UI 消息。
- `diskpart_log.log`、`reformat_log.log`、`recover_script_log.txt`：分区和重新格式化步骤。
- `iso_extract_log.log`、`iso_content.log`：提取的 ISO 内容的详细信息。
- `bcd_config_log.log`：BCD 配置命令和结果。
- `copy_error_log.log`、`iso_file_copy_log.log`：文件复制和错误。

在诊断故障或共享报告时查看这些日志。

## 安全和恢复
- 该操作会修改系统磁盘；执行工具前请备份。
- 在过程中，避免从任务管理器关闭应用程序；使用集成的取消选项。
- 如果决定恢复配置，请使用**恢复空间**按钮删除 `ISOBOOT`/`ISOEFI` 分区并还原 `C:` 驱动器。

## 限制
- 在磁盘 0 上操作并将卷 C: 缩小约 10.5 GB；目前不支持其他磁盘布局。
- 需要管理员权限和 Windows PowerShell 可用性。
- 需要 `lang/` 下的语言文件；如果找不到，应用程序会显示错误。

## 致谢
由 **Andrey Rodríguez Araya** 于 2025 年开发。

## 许可证
本项目采用 GPL 3.0 许可证。有关详细信息，请参阅 `LICENSE` 文件。

## 第三方声明
- 7‑Zip SDK：本产品的部分内容包括来自 Igor Pavlov 的 7‑Zip SDK 的代码。
  - 许可摘要（根据 `third-party/DOC/License.txt`）：
    - 大多数文件根据 GNU LGPL（v2.1 或更高版本）许可。
    - 某些文件在标题中明确声明时为公共领域。
    - `CPP/7zip/Compress/LzfseDecoder.cpp` 采用 BSD 3‑Clause 许可证。
    - `CPP/7zip/Compress/Rar*` 采用 GNU LGPL 并带有"unRAR 许可限制"。
  - 我们包含最小子集（ISO 处理程序和通用实用程序）。本项目不使用 RAR 代码。
  - 完整文本：请参阅 `third-party/DOC/License.txt`、`third-party/DOC/lzma.txt` 和 `third-party/DOC/unRarLicense.txt`。
