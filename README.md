# BootThatISO!

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
- ✅ HBCD_PE_x64.iso
- ✅ Win11_25H2_Spanish_x64.iso (BOOT, TESTING INSTALL)
- ✅ Windows10_22H2_X64.iso (BOOT, TESTING INSTALL)

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
|- build/               # CMake/Visual Studio generated files
|- include/             # Shared headers
|  |- models/           # Model interfaces (HashInfo, PartitionDetector, etc.)
|  |- version.h.in      # Version template
|- src/
|  |- boot/             # Boot coordination (BootWimProcessor)
|  |- wim/              # WIM/DISM operations (WimMounter)
|  |- drivers/          # Driver integration (DriverIntegrator)
|  |- config/           # PE configuration (PecmdConfigurator, StartnetConfigurator, IniFileProcessor)
|  |- filesystem/       # Filesystem operations (ProgramsIntegrator)
|  |- controllers/      # Flow orchestration (ProcessController, ProcessService)
|  |- models/           # Domain models (ISOReader, FileCopyManager, EventManager, etc.)
|  |- services/         # Application services (PartitionManager, ISOCopyManager, BCDManager)
|  |- utils/            # Utilities (Logger, LocalizationManager, Utils, constants)
|  |- views/            # Win32 UI (MainWindow)
|- tests/               # Unit tests
|- third-party/         # 7-Zip SDK
|- lang/                # Localization files (en_us.xml, es_cr.xml)
|- CMakeLists.txt
|- compilar.bat
|- .clang-format        # Code formatting configuration
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

---

# BootThatISO!

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! es una herramienta innovadora para Windows que permite **arrancar sistemas operativos desde archivos ISO sin necesidad de una memoria USB**. Ideal para situaciones donde no se cuenta con un dispositivo USB a mano, como en viajes, equipos prestados o emergencias. Automatiza la creación de particiones EFI y de datos en el disco interno, lectura directa de ISO y extracción de archivos, y configuración de BCD, ofreciendo una interfaz gráfica intuitiva y soporte para ejecución no asistida.

Esta utilidad es especialmente útil para:
- **Instalaciones rápidas**: Arranque directo desde ISO para instalación de Windows, Linux o herramientas de recuperación sin preparar USB.
- **Entornos de prueba**: Prueba ISOs de sistemas operativos o utilidades sin modificar hardware externo.
- **Recuperación de sistemas**: Acceso a herramientas de reparación como HBCD_PE o entornos live sin depender de medios externos.
- **Automatización**: Integración en scripts para despliegues masivos o configuraciones automatizadas.

Proyecto desarrollado por **Andrey Rodríguez Araya**.

Sitio web: [Inglés](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Caracteristicas clave
- Crea o reforma particiones `ISOBOOT` (datos) e `ISOEFI` (EFI) en el disco del sistema, con opciones de formato FAT32, exFAT o NTFS.
- Soporta dos modos de arranque: carga completa del ISO en disco o modo RAMDisk (boot.wim en memoria).
- Detecta ISOs de Windows y ajusta automaticamente la configuracion de BCD; las ISOs no Windows arrancan directamente desde la particion EFI.
- Ejecuta comprobaciones opcionales de integridad (`chkdsk`), genera bitacoras detalladas y permite cancelar o recuperar espacio.
- Proporciona un modo no asistido para integraciones con scripts mediante argumentos de linea de comandos.
- **Caché de hash del ISO (ISOBOOTHASH)**: Compara el hash MD5 del archivo ISO, el modo de arranque y el formato seleccionado contra los valores almacenados en la partición existente. Si coinciden, omite el formateo y la copia de archivos, acelerando procesos repetitivos con el mismo ISO y configuración.

## ISOs Testeadas

### Modo RAM (Boot desde Memoria)
- ✅ HBCD_PE_x64.iso 
- ✅ Win11_25H2_Spanish_x64.iso (ARRANQUE, PROBANDO INSTALACION)
- ✅ Windows10_22H2_X64.iso (ARRANQUE, PROBANDO INSTALACION)

### Modo EXTRACT (Instalación Completa)
- ✅ HBCD_PE_x64.iso (hace fallback ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (hace fallback ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (hace fallback ISOBOOT_RAM)

## Requisitos
- Windows 10 u 11 de 64 bits con privilegios de administrador.
- Espacio libre minimo de 12 GB en la unidad `C:` para crear y formatear particiones (la herramienta intenta reducir ~10.5 GB).
- PowerShell, DiskPart, bcdedit y herramientas de linea de comandos de Windows disponibles en el sistema.
- Para compilacion: Visual Studio 2022 con CMake. No se requiere gestor de paquetes externo; el SDK de 7‑Zip está incluido en `third-party/`.

## Compilacion
```powershell
# Configura y compila (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

El ejecutable final se ubica en `build/Release/BootThatISO!.exe`. Tambien se incluye `compilar.bat` con pasos equivalentes.

### Compilación rápida con compilar.bat (recomendado)
```powershell
# En la raíz del repositorio
./compilar.bat
```

#### Firma de código
- Para omitir la firma (útil en equipos de desarrollo sin certificado):
```powershell
$env:SIGN_CERT_SHA1 = "skip"
./compilar.bat
```
- Para firmar, define `SIGN_CERT_SHA1` con la huella SHA1 de tu certificado o asegúrate de que `signtool.exe` encuentre un certificado válido en tu almacén.

#### Formato de código
El script `compilar.bat` formatea automáticamente todo el código fuente usando `clang-format` antes de compilar:
- Formatea todos los archivos `.cpp` y `.h` en los directorios `src/`, `include/` y `tests/`
- Utiliza el archivo de configuración `.clang-format` del proyecto
- Si no se encuentra `clang-format`, se muestra una advertencia pero la compilación continúa

### Notas de compilación
- En Release se vincula el runtime de MSVC de forma estática (/MT) para obtener un ejecutable autocontenido (no requiere VC++ Redistributable).
- El SDK de 7‑Zip se compila como librería estática y se enlaza como "whole-archive" para conservar el registro de manejadores.
- Manejadores incluidos: UDF, ISO, contenedor Ext y MBR. El lector prioriza UDF y puede "desenvolver" Ext para acceder al flujo interno UDF/ISO.

### Diagnóstico y pruebas
Se generan dos utilidades de consola junto con la app para validar el manejo de ISOs:

```powershell
# Lista manejadores soportados e intenta abrir vía UDF/ISO
build/Release/ListFormats.exe

# Lista el contenido del ISO y extrae automáticamente todos los *.wim/*.esd a %TEMP%\EasyISOBoot_iso_extract_test
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Win11_25H2_Spanish_x64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Windows10_22H2_X64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\HBCD_PE_x64.iso"
```

Notas:
- La prueba preserva las rutas internas del ISO al extraer (por ejemplo, escribe en %TEMP%\EasyISOBoot_iso_extract_test\sources\boot.wim).
- Las ISOs de Windows pueden usar `install.wim` o `install.esd`; la prueba detecta y extrae todos los `.wim` y `.esd` que encuentre.
- Las ISOs híbridas de Windows muestran pocos elementos con el manejador ISO; al abrir vía UDF se obtiene el listado completo (se maneja automáticamente).

Ejecutar pruebas con CTest:
```powershell
cd build
ctest -C Release --output-on-failure
```

### Validación de copia de la imagen de instalación
- Al copiar la imagen de Windows desde el ISO (`sources/install.wim` o `sources/install.esd`), la app ahora verifica:
  - Coincidencia de tamaño: compara el tamaño dentro del ISO con el tamaño del archivo extraído en disco.
  - Integridad de la imagen: ejecuta `DISM /Get-WimInfo /WimFile:"<dest>"` y verifica que existan índices válidos.
- Los resultados se escriben en `logs/iso_extract_log.log` y se muestran en la UI. Cualquier discrepancia o fallo de DISM se marca para que puedas reintentar la extracción.

## Uso
### Interfaz grafica
1. Ejecuta `BootThatISO!.exe` **como administrador** (el manifest ya lo solicita).
2. Selecciona el archivo ISO y escoge el formato del sistema de archivos para `ISOBOOT`.
3. Define el modo de arranque:
   - `Instalacion Completa`: copia todo el contenido del ISO a disco.
   - `Boot desde Memoria`: copia `boot.wim` y dependencias para arrancar desde RAM.
4. Decide si deseas ejecutar `chkdsk` (desmarcado se omite la verificacion para acelerar el proceso).
5. Haz clic en **Crear particion bootable** y sigue el progreso mediante la barra principal, la barra detallada y el panel de logs.
6. Al finalizar se mostrara una confirmacion para reiniciar. Usa el boton **Recuperar espacio** si necesitas eliminar las particiones `ISOBOOT`/`ISOEFI` y extender `C:`.
7. El boton **Servicios** abre la pagina de soporte `https://agsoft.co.cr/servicios/`.

### Modo no asistido
Ejecuta el binario con privilegios elevados y los siguientes argumentos:

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\ruta\imagen.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr
```

- `-mode=RAM` activa el modo *Boot desde Memoria* y copia `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corresponde a *Instalacion Completa*.
- `-chkdsk=TRUE` obliga la verificacion de disco (por defecto se omite).
- `-autoreboot` esta disponible para automatizaciones futuras; actualmente solo registra la preferencia.
- `-lang` establece el código de idioma según los archivos dentro de `lang/`.

El proceso registra eventos y finaliza sin mostrar la ventana principal.

## Flujo interno resumido
1. **Validacion y particiones** (`PartitionManager`): verifica espacio disponible, ejecuta `chkdsk` opcional, reduce `C:` ~10.5 GB, crea `ISOEFI` (500 MB FAT32) y `ISOBOOT` (10 GB) o reformatea las existentes, y expone metodos de recuperacion.
2. **Preparacion de contenidos** (`ISOCopyManager`): lee el contenido del ISO usando el SDK de 7‑Zip (manejador ISO), clasifica si es Windows, lista el contenido, copia archivos a las unidades de destino y delega el manejo EFI a `EFIManager`.
3. **Procesamiento de Arranque** (`BootWimProcessor`): orquesta la extracción y procesamiento de boot.wim, coordina con módulos especializados:
   - `WimMounter`: maneja operaciones DISM para montar/desmontar archivos WIM
   - `DriverIntegrator`: integra drivers del sistema y personalizados en la imagen WIM
   - `PecmdConfigurator`: configura entornos Hiren's BootCD PE
   - `StartnetConfigurator`: configura entornos WinPE estándar
   - `IniFileProcessor`: procesa archivos INI con reemplazo de letras de unidad
   - `ProgramsIntegrator`: integra programas adicionales en el entorno de arranque
4. **Copia y progresos** (`FileCopyManager`/`EventManager`): notifica avance granular, permite cancelacion y actualiza bitacoras.
5. **Configuracion de BCD** (`BCDManager` + estrategias): crea entradas WinPE (RAMDisk) o de instalacion completa, ajusta `{ramdiskoptions}` y registra comandos ejecutados.
6. **UI Win32** (`MainWindow`): construye controles manualmente, aplica estilo, maneja comandos y expone opciones de recuperacion.

### Arquitectura Modular
El proyecto sigue una arquitectura modular limpia con clara separación de responsabilidades:
- **Patrón Facade**: `BootWimProcessor` proporciona una interfaz simple para operaciones complejas de procesamiento de arranque
- **Patrón Strategy**: La integración de drivers usa categorías (Storage, USB, Network) para configuración flexible
- **Patrón Observer**: `EventManager` notifica a múltiples observadores (UI, Logger) de actualizaciones de progreso
- **Chain of Responsibility**: La integración de programas intenta múltiples estrategias de respaldo
- **Responsabilidad Única**: Cada clase tiene un propósito claro (montar WIMs, integrar drivers, etc.)

Para documentación detallada de la arquitectura, consulte `ARCHITECTURE.md`.

## Bitacoras y diagnostico
Todas las operaciones generan archivos en `logs/` (creados junto al ejecutable). Entre los mas relevantes:
- `general_log.log`: cronologia general de eventos y mensajes de UI.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: pasos de particionado y reformateo.
- `iso_extract_log.log`, `iso_content.log`: detalles del contenido extraido de la ISO.
- `bcd_config_log.log`: comandos y resultados de configuracion BCD.
- `copy_error_log.log`, `iso_file_copy_log.log`: copiado de archivos y errores.

Revisa estas bitacoras al diagnosticar fallos o al compartir reportes.

## Seguridad y recuperacion
- La operacion modifica el disco del sistema; haz una copia de seguridad antes de ejecutar la herramienta.
- Durante el proceso, evita cerrar la aplicacion desde el administrador de tareas; utiliza la opcion de cancelar integrada.
- Usa el boton **Recuperar espacio** para eliminar `ISOBOOT`/`ISOEFI` y restaurar la unidad `C:` si decides revertir la configuracion.

## Limitaciones
- Opera sobre el Disco 0 y reduce la unidad C: en ~10.5 GB; otros layouts no están soportados por ahora.
- Requiere privilegios de administrador y disponibilidad de Windows PowerShell.
- Se requieren archivos de idioma en `lang/`; la app muestra un error si no los encuentra.

## Estructura del repositorio
```
BootThatISO!/
|- build/               # Archivos generados por CMake/Visual Studio
|- include/             # Cabeceras compartidas
|  |- models/           # Interfaces de modelos (HashInfo, PartitionDetector, etc.)
|  |- version.h.in      # Plantilla de versión
|- src/
|  |- boot/             # Coordinación de arranque (BootWimProcessor)
|  |- wim/              # Operaciones WIM/DISM (WimMounter)
|  |- drivers/          # Integración de drivers (DriverIntegrator)
|  |- config/           # Configuración PE (PecmdConfigurator, StartnetConfigurator, IniFileProcessor)
|  |- filesystem/       # Operaciones de sistema de archivos (ProgramsIntegrator)
|  |- controllers/      # Orquestación del flujo (ProcessController, ProcessService)
|  |- models/           # Modelos de dominio (ISOReader, FileCopyManager, EventManager, etc.)
|  |- services/         # Servicios de aplicación (PartitionManager, ISOCopyManager, BCDManager)
|  |- utils/            # Utilidades (Logger, LocalizationManager, Utils, constantes)
|  |- views/            # UI Win32 (MainWindow)
|- tests/               # Pruebas unitarias
|- third-party/         # SDK de 7-Zip
|- lang/                # Archivos de localización (en_us.xml, es_cr.xml)
|- CMakeLists.txt
|- compilar.bat
|- .clang-format        # Configuración de formato de código
```
## Creditos
Desarrollado por **Andrey Rodríguez Araya** en 2025.

## Licencia
Este proyecto está bajo la Licencia GPL 3.0. Ver el archivo `LICENSE` para más detalles.

## Avisos de terceros
- SDK de 7‑Zip: Este producto incluye partes del SDK de 7‑Zip de Igor Pavlov.
  - Resumen de licenciamiento (según `third-party/DOC/License.txt`):
    - La mayoría de los archivos están bajo GNU LGPL (v2.1 o posterior).
    - Algunos archivos son de dominio público cuando así se indica en sus cabeceras.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` usa la licencia BSD de 3 cláusulas.
    - `CPP/7zip/Compress/Rar*` están bajo GNU LGPL con la “restricción de licencia unRAR”.
  - Incluimos un subconjunto mínimo (manejador ISO y utilidades comunes). Este proyecto no utiliza código RAR.
  - Textos completos: ver `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt` y `third-party/DOC/unRarLicense.txt`.
