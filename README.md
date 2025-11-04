# BootThatISO!

**Languages / Idiomas / Línguas / Sprachen / Langues / Lingue / Языки / 语言 / 言語:**
[🇪🇸 Español](README.es.md) | [🇧🇷 Português](README.pt.md) | [🇩🇪 Deutsch](README.de.md) | [🇫🇷 Français](README.fr.md) | [🇮🇹 Italiano](README.it.md) | [🇷🇺 Русский](README.ru.md) | [🇨🇳 中文](README.zh.md) | [🇯🇵 日本語](README.ja.md)

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! is an innovative Windows tool that allows **booting operating systems from ISO files without needing a USB drive**. Ideal for situations where you don't have a USB device handy, such as during travel, borrowed equipment, or emergencies. It automates the creation of EFI and data partitions on the internal disk, direct ISO reading and file extraction, and BCD configuration, offering an intuitive graphical interface and support for unattended execution.

This utility is especially useful for:
- **Quick Installations**: Direct boot from ISO for Windows, Linux installation, or recovery tools without preparing USB.
- **Testing Environments**: Test OS ISOs or utilities without modifying external hardware.
- **System Recovery**: Access repair tools like HBCD_PE or live environments without depending on external media.
- **Automation**: Integration in scripts for mass deployments or automated configurations.

Developed by **Andrey Rodríguez Araya**.

Website: [English](https://agsoft.co.cr/en/software-and-services/) | [Spanish](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Key Features
- Creates or reforms `ISOBOOT` (data) and `ISOEFI` (EFI) partitions on the system disk, with FAT32, exFAT, or NTFS format options.
- Supports two boot modes: full ISO loading to disk or RAMDisk mode (boot.wim in memory).
- Detects Windows ISOs and automatically adjusts BCD configuration; non-Windows ISOs boot directly from the EFI partition.
- Runs optional integrity checks (`chkdsk`), generates detailed logs, and allows cancellation or space recovery.
- Provides unattended mode for script integrations via command-line arguments.
- **ISO hash cache (ISOBOOTHASH)**: Compares the ISO MD5, selected boot mode, and format against values stored in the `ISOBOOTHASH` file on the target. If they match, it skips formatting and content copy to speed up repeated runs.

## Tested ISOs

### RAM Mode (Boot from Memory)
- ✅ HBCD_PE_x64.iso (FULLY FUNCTIONAL - Load all programs from RAM)
- ✅ Win11_25H2_Spanish_x64.iso (FULLY FUNCTIONAL - Boot and Install)
- ✅ Windows10_22H2_X64.iso (FULLY FUNCTIONAL - Boot and Install)

### EXTRACT Mode (Full Installation)
- ✅ HBCD_PE_x64.iso (falls back to ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (falls back to ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (falls back to ISOBOOT_RAM)

## Requirements
- Windows 10 or 11 64-bit with administrator privileges.
- At least 12 GB of free space on drive `C:` to create and format partitions (the tool attempts to shrink 12 GB).
- PowerShell, DiskPart, bcdedit, and available Windows command-line tools.
- For compilation: Visual Studio 2022 with CMake. No external package manager required; the 7‑Zip SDK is vendored under `third-party/`.

## Compilation
```powershell
# Configure and build (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The final executable is located at `build/Release/BootThatISO!.exe`. Also included is `compilar.bat` with equivalent steps.

### Quick compilation with compilar.bat (recommended)
```powershell
# In the repository root
./compilar.bat
```

#### Code signing
- To skip signing (useful on dev machines without a certificate):
```powershell
$env:SIGN_CERT_SHA1 = "skip"
./compilar.bat
```
- To sign, set `SIGN_CERT_SHA1` to your certificate's SHA1 thumbprint or make sure `signtool.exe` can find a suitable certificate in your store.

#### Code formatting
The `compilar.bat` script automatically formats all source code using `clang-format` before building:
- Formats all `.cpp` and `.h` files in `src/`, `include/`, and `tests/` directories
- Uses the project's `.clang-format` configuration file
- If `clang-format` is not found, a warning is displayed but the build continues

Note: The project now uses the 7‑Zip SDK (vendored) for ISO reading/extraction; no vcpkg or libarchive is required.

### Build notes
- Release binaries are linked with the static MSVC runtime (/MT) for a self-contained EXE (no VC++ Redistributable required).
- 7‑Zip SDK is compiled as a static library and linked whole-archive to retain handler registration.
- Included handlers: UDF, ISO, Ext container, and MBR. The reader prefers UDF and can unwrap Ext to reach the inner UDF/ISO stream.

### Diagnostics and tests
The following console utilities are built alongside the app to validate behavior:

```powershell
# List supported handlers and try opening via UDF/ISO
build/Release/ListFormats.exe

# List ISO contents and auto-extract all *.wim/*.esd to %TEMP%\EasyISOBoot_iso_extract_test
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Win11_25H2_Spanish_x64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Windows10_22H2_X64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\HBCD_PE_x64.iso"

# Detect Windows ISO heuristics
build/Release/TestISODetection.exe "C:\path\to\some.iso"

# Space recovery demonstration (uses same routines as the app)
build/Release/TestRecoverSpace.exe

# INI replacer demo
build/Release/test_ini_replacer.exe
```

Notes:
- The test preserves internal ISO paths when extracting (e.g., writes to %TEMP%\EasyISOBoot_iso_extract_test\sources\boot.wim).
- Windows ISOs may use `install.wim` or `install.esd`; the test discovers and extracts all `.wim` and `.esd` files it finds.
- Hybrid Windows ISOs expose few items via the ISO handler; opening through UDF yields the full file list (handled automatically).

Run unit tests with CTest:
```powershell
cd build
ctest -C Release --output-on-failure
```

### Install image copy validation
- When copying the Windows image from the ISO (`sources/install.wim` or `sources/install.esd`), the app now verifies:
  - Size match: compares the size inside the ISO with the extracted file size on disk.
  - Image integrity: runs `DISM /Get-WimInfo /WimFile:"<dest>"` and checks for valid indices.
- Results are written to `logs/iso_extract_log.log` and shown in the UI. Any mismatch or DISM failure is flagged so you can retry extraction.

## Usage
### Graphical Interface
1. Run `BootThatISO!.exe` **as administrator** (the manifest already requests it).
2. Select the ISO file and choose the file system format for `ISOBOOT`.
3. Define the boot mode:
   - `Full Installation`: copies the entire ISO content to disk.
   - `Boot from Memory`: copies `boot.wim` and dependencies to boot from RAM.
4. Decide whether to run `chkdsk` (unchecked skips verification to speed up the process).
5. Click **Create Bootable Partition** and monitor progress via the main bar, detailed bar, and log panel.
6. Upon completion, a restart confirmation will appear. Use the **Recover Space** button if you need to remove `ISOBOOT`/`ISOEFI` partitions and extend `C:`.
7. The **Services** button opens the support page `https://agsoft.co.cr/servicios/`.

### Unattended Mode
Run the binary with elevated privileges and the following arguments:

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\path\image.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr
```

- `-mode=RAM` activates *Boot from Memory* mode and copies `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corresponds to *Full Installation*.
- `-chkdsk=TRUE` forces disk verification (omitted by default).
- `-lang` sets the language code matching files under `lang/`.
- `-autoreboot` is available for future automations; currently just logs the preference.

The process logs events and exits without showing the main window.

## Internal Flow Summary
1. **Validation and Partitions** (`PartitionManager`): checks available space, runs optional `chkdsk`, reduces `C:` by ~10.5 GB, creates `ISOEFI` (500 MB FAT32) and `ISOBOOT` (10 GB), or reforms existing ones, and exposes recovery methods.
2. **Content Preparation** (`ISOCopyManager`): reads ISO content using the 7‑Zip SDK (ISO handler), classifies if Windows, lists content, copies files to target drives, and delegates EFI handling to `EFIManager`.
3. **Boot Processing** (`BootWimProcessor`): orchestrates the extraction and processing of boot.wim, coordinates with specialized modules:
   - `WimMounter`: handles DISM operations for mounting/unmounting WIM files
   - `DriverIntegrator`: integrates system and custom drivers into the WIM image
   - `PecmdConfigurator`: configures Hiren's BootCD PE environments
   - `StartnetConfigurator`: configures standard WinPE environments
   - `IniFileProcessor`: processes INI files with drive letter replacement
   - `ProgramsIntegrator`: integrates additional programs into the boot environment
4. **Copying and Progress** (`FileCopyManager`/`EventManager`): notifies granular progress, allows cancellation, and updates logs.
5. **BCD Configuration** (`BCDManager` + strategies): creates WinPE entries (RAMDisk) or full installation, adjusts `{ramdiskoptions}`, and logs executed commands.
6. **Win32 UI** (`MainWindow`): manually builds controls, applies style, handles commands, and exposes recovery options.

### Modular Architecture
The project follows a clean, modular architecture with clear separation of concerns:
- **Facade Pattern**: `BootWimProcessor` provides a simple interface to complex boot processing operations
- **Strategy Pattern**: Driver integration uses categories (Storage, USB, Network) for flexible configuration
- **Observer Pattern**: `EventManager` notifies multiple observers (UI, Logger) of progress updates
- **Chain of Responsibility**: Programs integration attempts multiple fallback strategies
- **Single Responsibility**: Each class has one clear purpose (mounting WIMs, integrating drivers, etc.)

For detailed architecture documentation, see `ARCHITECTURE.md`.

## Logs and Diagnostics
All operations generate files in `logs/` (created alongside the executable). Among the most relevant:
- `general_log.log`: general event timeline and UI messages.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: partitioning and reformatting steps.
- `iso_extract_log.log`, `iso_content.log`: details of extracted ISO content.
- `bcd_config_log.log`: BCD configuration commands and results.
- `copy_error_log.log`, `iso_file_copy_log.log`: file copying and errors.

Review these logs when diagnosing failures or sharing reports.

## Security and Recovery
## Limitations
- Operates on Disk 0 and shrinks volume C: by ~10.5 GB; other disk layouts are not currently supported.
- Requires administrator privileges and Windows PowerShell availability.
- Language files under `lang/` are required; the app shows an error if none are found.

## Security and Recovery
- The operation modifies the system disk; back up before executing the tool.
- During the process, avoid closing the application from Task Manager; use the integrated cancel option.
- Use the **Recover Space** button to remove `ISOBOOT`/`ISOEFI` partitions and restore the `C:` drive if you decide to revert the configuration.

## Repository Structure
```
BootThatISO!/
|- build/                      # CMake/Visual Studio generated files
|- include/                    # Shared headers
|  |- models/                  # Model interfaces (HashInfo, etc.)
|  |- version.h.in             # Version template
|- src/
|  |- boot/                    # Boot coordination (BootWimProcessor)
|  |  |- BootWimProcessor.cpp  # Main orchestrator for boot.wim processing
|  |  |- BootWimProcessor.h
|  |- wim/                     # WIM/DISM operations
|  |  |- WimMounter.cpp        # WIM mount/unmount with DISM
|  |  |- WimMounter.h
|  |  |- WindowsEditionSelector.cpp  # Windows edition selection logic
|  |  |- WindowsEditionSelector.h
|  |- drivers/                 # Driver integration
|  |  |- DriverIntegrator.cpp  # System + custom driver integration
|  |  |- DriverIntegrator.h
|  |- config/                  # PE configuration
|  |  |- PecmdConfigurator.cpp # Hiren's BootCD PE configuration
|  |  |- PecmdConfigurator.h
|  |  |- StartnetConfigurator.cpp  # Standard WinPE configuration
|  |  |- StartnetConfigurator.h
|  |  |- IniFileProcessor.cpp  # INI file processing
|  |  |- IniFileProcessor.h
|  |- filesystem/              # Filesystem operations
|  |  |- ProgramsIntegrator.cpp  # Programs folder integration
|  |  |- ProgramsIntegrator.h
|  |- controllers/             # Flow orchestration
|  |  |- ProcessController.cpp
|  |  |- ProcessController.h
|  |  |- ProcessService.cpp
|  |- models/                  # Domain models
|  |  |- ISOReader.cpp         # 7-Zip wrapper for ISO reading
|  |  |- IniConfigurator.cpp   # Drive letter replacement in INI files
|  |  |- FileCopyManager.cpp   # Progress tracking for file operations
|  |  |- EventManager.h        # Observer pattern for events
|  |  |- ContentExtractor.cpp  # ISO content extraction
|  |  |- HashVerifier.cpp      # Hash verification (MD5)
|  |  |- efimanager.cpp        # EFI partition management
|  |  |- isomounter.cpp        # ISO mounting operations
|  |  |- DiskIntegrityChecker.cpp
|  |  |- VolumeDetector.cpp
|  |  |- SpaceManager.cpp
|  |  |- DiskpartExecutor.cpp
|  |  |- PartitionReformatter.cpp
|  |  |- PartitionCreator.cpp
|  |- services/                # Application services
|  |  |- ISOCopyManager.cpp    # ISO copying orchestration
|  |  |- BCDManager.cpp        # BCD configuration
|  |  |- PartitionManager.cpp  # Partition operations
|  |  |- PartitionDetector.cpp # Partition detection
|  |  |- VolumeManager.cpp     # Volume management
|  |  |- isotypedetector.cpp   # ISO type detection (Windows/Linux)
|  |- utils/                   # Utilities
|  |  |- Utils.cpp             # General utilities
|  |  |- Logger.cpp            # Logging system
|  |  |- LocalizationManager.cpp  # Multi-language support
|  |- views/                   # Win32 UI
|  |  |- mainwindow.cpp        # Main application window
|  |  |- EditionSelectorDialog.cpp  # Edition selection dialog
|  |- main.cpp                 # Application entry point
|  |- SevenZipGuids.cpp        # 7-Zip GUID definitions
|- tests/                      # Unit tests
|  |- utils_tests.cpp
|  |- test_recover_space.cpp
|- tools/                      # Build and validation tools
|  |- validate_translations.cpp
|- third-party/                # 7-Zip SDK
|  |- C/                       # C implementation files
|  |- CPP/                     # C++ implementation files
|  |- DOC/                     # License and documentation
|- lang/                       # Localization files
|  |- en_us.xml                # English (US)
|  |- es_cr.xml                # Spanish (Costa Rica)
|- isos/                       # Test ISOs directory
|- release-assets/             # Release assets
|- res/                        # Resources (icons, images)
|- test_ini_replacer.cpp       # INI replacer test
|- test_iso_detection.cpp      # ISO detection test
|- test_iso_reader.cpp         # ISO reader test
|- test_list_formats.cpp       # Format listing test
|- CMakeLists.txt              # CMake build configuration
|- compilar.bat                # Windows build script
|- .clang-format               # Code formatting configuration
|- LICENSE                     # GPL 3.0 License
|- README.md                   # This file
|- ARCHITECTURE.md             # Architecture documentation
|- release_notes.md            # Release notes
```
## Credits
Developed by **Andrey Rodríguez Araya** in 2025.

## License
This project is under the GPL 3.0 License. See the `LICENSE` file for more details.

## Third-party notices
- 7‑Zip SDK: Portions of this product include code from the 7‑Zip SDK by Igor Pavlov.
  - Licensing summary (per `third-party/DOC/License.txt`):
    - Most files are licensed under the GNU LGPL (v2.1 or later).
    - Some files are public domain where explicitly stated in headers.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` is under the BSD 3‑Clause license.
    - `CPP/7zip/Compress/Rar*` are under GNU LGPL with the “unRAR license restriction”.
  - We vendor a minimal subset (ISO handler and common utilities). No RAR code is used by this project.
  - Full texts: see `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt`, and `third-party/DOC/unRarLicense.txt`.