# BootThatISO!

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! is an innovative Windows tool that allows **booting operating systems from ISO files without needing a USB drive**. Ideal for situations where you don't have a USB device handy, such as during travel, borrowed equipment, or emergencies. It automates the creation of EFI and data partitions on the internal disk, ISO mounting, file copying, and BCD configuration, offering an intuitive graphical interface and support for unattended execution.

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
- **ISOHASHBOOT Improvement**: Efficiency optimization that compares the MD5 hash of the ISO file, selected boot mode, and format against stored values in the existing partition. If they match, skips formatting and file copying, speeding up repetitive processes with the same ISO and configuration.

## Tested ISOs

### RAM Mode (Boot from Memory)
- ✅ HBCD_PE_x64.iso (CORRECT BOOT)
- ✅ Win11_25H2_Spanish_x64.iso (CORRECT BOOT)
- ✅ Windows10_22H2_X64.iso (CORRECT BOOT)

### EXTRACT Mode (Full Installation)
- ✅ HBCD_PE_x64.iso (falls back to ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (falls back to ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (falls back to ISOBOOT_RAM)

## Requirements
- Windows 10 or 11 64-bit with administrator privileges.
- Visual C++ Redistributable for Visual Studio 2022 (x64 or x86 depending on system architecture; download from https://aka.ms/vs/17/release/vc_redist.x64.exe for x64 or https://aka.ms/vs/17/release/vc_redist.x86.exe for x86).
- PowerShell, DiskPart, bcdedit, and available Windows command-line tools.
- Minimum 12 GB free space on C: drive for creating and formatting partitions.

## Compilation
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
```
The final executable is located at `build/Release/BootThatISO!.exe`. Also included is `compilar.bat` with the same steps.

**Note**: The executable is digitally signed with a development certificate to enhance trust and reduce security warnings on Windows.
```

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
  -autoreboot=y|n
```

- `-mode=RAM` activates *Boot from Memory* mode and copies `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corresponds to *Full Installation*.
- `-chkdsk=TRUE` forces disk verification (omitted by default).
- `-autoreboot` is available for future automations; currently just logs the preference.

The process logs events and exits without showing the main window.

## Internal Flow Summary
1. **Validation and Partitions** (`PartitionManager`): checks available space, runs optional `chkdsk`, reduces `C:` by ~10.5 GB, creates `ISOEFI` (500 MB FAT32) and `ISOBOOT` (10 GB), or reforms existing ones, and exposes recovery methods.
2. **Content Preparation** (`ISOCopyManager`): mounts ISO via PowerShell, classifies if Windows, lists content, copies files to target drives, and delegates EFI handling to `EFIManager`.
3. **Copying and Progress** (`FileCopyManager`/`EventManager`): notifies granular progress, allows cancellation, and updates logs.
4. **BCD Configuration** (`BCDManager` + strategies): creates WinPE entries (RAMDisk) or full installation, adjusts `{ramdiskoptions}`, and logs executed commands.
5. **Win32 UI** (`MainWindow`): manually builds controls, applies style, handles commands, and exposes recovery options.

## Logs and Diagnostics
All operations generate files in `logs/` (created alongside the executable). Among the most relevant:
- `general_log.log`: general event timeline and UI messages.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: partitioning and reformatting steps.
- `iso_extract_log.log`, `iso_content.log`: details of extracted ISO content.
- `bcd_config_log.log`: BCD configuration commands and results.
- `copy_error_log.log`, `iso_file_copy_log.log`: file copying and errors.

Review these logs when diagnosing failures or sharing reports.

## Security and Recovery
- The operation modifies the system disk; back up before executing the tool.
- During the process, avoid closing the application from Task Manager; use the integrated cancel option.
- Use the **Recover Space** button to remove `ISOBOOT`/`ISOEFI` partitions and restore the `C:` drive if you decide to revert the configuration.

## Repository Structure
```
BootThatISO!/
|- build/               # CMake/Visual Studio generated files
|- include/             # Shared headers (reserved)
|- src/
|  |- controllers/      # Flow orchestration (ProcessController)
|  |- models/           # Boot strategies, EFI handling, observers
|  |- services/         # Partitioning, ISO copying, BCD, detections
|  |- utils/            # Utilities (exec, conversions, constants)
|  |- views/            # Main window and Win32 UI logic
|- CMakeLists.txt
|- compilar.bat
```
## Credits
Developed by **Andrey Rodríguez Araya** in 2025.

## License
This project is under the GPL 3.0 License. See the `LICENSE` file for more details.

---

# BootThatISO!

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! es una herramienta innovadora para Windows que permite **arrancar sistemas operativos desde archivos ISO sin necesidad de una memoria USB**. Ideal para situaciones donde no se cuenta con un dispositivo USB a mano, como en viajes, equipos prestados o emergencias. Automatiza la creación de particiones EFI y de datos en el disco interno, montaje de ISOs, copia de archivos y configuración de BCD, ofreciendo una interfaz gráfica intuitiva y soporte para ejecución no asistida.

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
- **Mejora ISOHASHBOOT**: Optimización de eficiencia que compara el hash MD5 del archivo ISO, el modo de arranque y el formato seleccionado contra los valores almacenados en la partición existente. Si coinciden, omite el formateo y la copia de archivos, acelerando procesos repetitivos con el mismo ISO y configuración.

## ISOs Testeadas

### Modo RAM (Boot desde Memoria)
- ✅ HBCD_PE_x64.iso (ARRANQUE CORRECTO)
- ✅ Win11_25H2_Spanish_x64.iso (ARRANQUE CORRECTO)
- ✅ Windows10_22H2_X64.iso (ARRANQUE CORRECTO)

### Modo EXTRACT (Instalación Completa)
- ✅ HBCD_PE_x64.iso (hace fallback ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (hace fallback ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (hace fallback ISOBOOT_RAM)

## Requisitos
- Windows 10 u 11 de 64 bits con privilegios de administrador.
- Visual Studio 2022 (o toolset MSVC compatible) con soporte para CMake y MFC.
- Visual C++ Redistributable for Visual Studio 2022 (x64 o x86 según la arquitectura del sistema; descárgalos desde https://aka.ms/vs/17/release/vc_redist.x64.exe para x64 o https://aka.ms/vs/17/release/vc_redist.x86.exe para x86).
- PowerShell, DiskPart, bcdedit y herramientas de linea de comandos de Windows disponibles en el sistema.
- Espacio libre minimo de 12 GB en la unidad `C:` para crear y formatear particiones.

## Compilacion
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
```
El ejecutable final se ubica en `build/Release/BootThatISO!.exe`. Tambien se incluye `compilar.bat` con los mismos pasos.

**Nota**: El ejecutable está firmado digitalmente con un certificado de desarrollo para mejorar la confianza y reducir advertencias de seguridad en Windows.
```

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
  -autoreboot=y|n
```

- `-mode=RAM` activa el modo *Boot desde Memoria* y copia `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corresponde a *Instalacion Completa*.
- `-chkdsk=TRUE` obliga la verificacion de disco (por defecto se omite).
- `-autoreboot` esta disponible para automatizaciones futuras; actualmente solo registra la preferencia.

El proceso registra eventos y finaliza sin mostrar la ventana principal.

## Flujo interno resumido
1. **Validacion y particiones** (`PartitionManager`): verifica espacio disponible, ejecuta `chkdsk` opcional, reduce `C:` ~10.5 GB, crea `ISOEFI` (500 MB FAT32) y `ISOBOOT` (10 GB) o reformatea las existentes, y expone metodos de recuperacion.
2. **Preparacion de contenidos** (`ISOCopyManager`): monta el ISO mediante PowerShell, clasifica si es Windows, lista el contenido, copia archivos a las unidades de destino y delega el manejo EFI a `EFIManager`.
3. **Copia y progresos** (`FileCopyManager`/`EventManager`): notifica avance granular, permite cancelacion y actualiza bitacoras.
4. **Configuracion de BCD** (`BCDManager` + estrategias): crea entradas WinPE (RAMDisk) o de instalacion completa, ajusta `{ramdiskoptions}` y registra comandos ejecutados.
5. **UI Win32** (`MainWindow`): construye controles manualmente, aplica estilo, maneja comandos y expone opciones de recuperacion.

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

## Estructura del repositorio
```
BootThatISO!/
|- build/               # Archivos generados por CMake/Visual Studio
|- include/             # Cabeceras compartidas (reservado)
|- src/
|  |- controllers/      # Orquestacion del flujo (ProcessController)
|  |- models/           # Estrategias de boot, manejo EFI, observadores
|  |- services/         # Particionado, copiado de ISO, BCD, detecciones
|  |- utils/            # Utilidades (exec, conversiones, constantes)
|  |- views/            # Ventana principal y logica UI Win32
|- CMakeLists.txt
|- compilar.bat
```
## Creditos
Desarrollado por **Andrey Rodríguez Araya** en 2025.

## Licencia
Este proyecto está bajo la Licencia GPL 3.0. Ver el archivo `LICENSE` para más detalles.