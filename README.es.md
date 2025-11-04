
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
- ✅ HBCD_PE_x64.iso (TOTALMENTE FUNCIONAL - Carga todos los programas en RAM)
- ✅ Win11_25H2_Spanish_x64.iso (TOTALMENTE FUNCIONAL - Arranque e Instalación)
- ✅ Windows10_22H2_X64.iso (TOTALMENTE FUNCIONAL - Arranque e Instalación)

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
|- build/                      # Archivos generados por CMake/Visual Studio
|- include/                    # Cabeceras compartidas
|  |- models/                  # Interfaces de modelos (HashInfo, etc.)
|  |- version.h.in             # Plantilla de versión
|- src/
|  |- boot/                    # Coordinación de arranque
|  |  |- BootWimProcessor.cpp  # Orquestador principal para procesamiento boot.wim
|  |  |- BootWimProcessor.h
|  |- wim/                     # Operaciones WIM/DISM
|  |  |- WimMounter.cpp        # Montaje/desmontaje WIM con DISM
|  |  |- WimMounter.h
|  |  |- WindowsEditionSelector.cpp  # Lógica de selección de edición Windows
|  |  |- WindowsEditionSelector.h
|  |- drivers/                 # Integración de drivers
|  |  |- DriverIntegrator.cpp  # Integración de drivers del sistema + personalizados
|  |  |- DriverIntegrator.h
|  |- config/                  # Configuración PE
|  |  |- PecmdConfigurator.cpp # Configuración de Hiren's BootCD PE
|  |  |- PecmdConfigurator.h
|  |  |- StartnetConfigurator.cpp  # Configuración WinPE estándar
|  |  |- StartnetConfigurator.h
|  |  |- IniFileProcessor.cpp  # Procesamiento de archivos INI
|  |  |- IniFileProcessor.h
|  |- filesystem/              # Operaciones de sistema de archivos
|  |  |- ProgramsIntegrator.cpp  # Integración de carpeta Programs
|  |  |- ProgramsIntegrator.h
|  |- controllers/             # Orquestación del flujo
|  |  |- ProcessController.cpp
|  |  |- ProcessController.h
|  |  |- ProcessService.cpp
|  |- models/                  # Modelos de dominio
|  |  |- ISOReader.cpp         # Wrapper de 7-Zip para lectura de ISO
|  |  |- IniConfigurator.cpp   # Reemplazo de letras de unidad en archivos INI
|  |  |- FileCopyManager.cpp   # Seguimiento de progreso para operaciones de archivos
|  |  |- EventManager.h        # Patrón Observer para eventos
|  |  |- ContentExtractor.cpp  # Extracción de contenido ISO
|  |  |- HashVerifier.cpp      # Verificación de hash (MD5)
|  |  |- efimanager.cpp        # Gestión de partición EFI
|  |  |- isomounter.cpp        # Operaciones de montaje ISO
|  |  |- DiskIntegrityChecker.cpp
|  |  |- VolumeDetector.cpp
|  |  |- SpaceManager.cpp
|  |  |- DiskpartExecutor.cpp
|  |  |- PartitionReformatter.cpp
|  |  |- PartitionCreator.cpp
|  |- services/                # Servicios de aplicación
|  |  |- ISOCopyManager.cpp    # Orquestación de copia de ISO
|  |  |- BCDManager.cpp        # Configuración BCD
|  |  |- PartitionManager.cpp  # Operaciones de particiones
|  |  |- PartitionDetector.cpp # Detección de particiones
|  |  |- VolumeManager.cpp     # Gestión de volúmenes
|  |  |- isotypedetector.cpp   # Detección de tipo de ISO (Windows/Linux)
|  |- utils/                   # Utilidades
|  |  |- Utils.cpp             # Utilidades generales
|  |  |- Logger.cpp            # Sistema de registro
|  |  |- LocalizationManager.cpp  # Soporte multi-idioma
|  |- views/                   # UI Win32
|  |  |- mainwindow.cpp        # Ventana principal de la aplicación
|  |  |- EditionSelectorDialog.cpp  # Diálogo de selección de edición
|  |- main.cpp                 # Punto de entrada de la aplicación
|  |- SevenZipGuids.cpp        # Definiciones GUID de 7-Zip
|- tests/                      # Pruebas unitarias
|  |- utils_tests.cpp
|  |- test_recover_space.cpp
|- tools/                      # Herramientas de construcción y validación
|  |- validate_translations.cpp
|- third-party/                # SDK de 7-Zip
|  |- C/                       # Archivos de implementación C
|  |- CPP/                     # Archivos de implementación C++
|  |- DOC/                     # Licencia y documentación
|- lang/                       # Archivos de localización
|  |- en_us.xml                # Inglés (US)
|  |- es_cr.xml                # Español (Costa Rica)
|- isos/                       # Directorio de ISOs de prueba
|- release-assets/             # Recursos de release
|- res/                        # Recursos (iconos, imágenes)
|- test_ini_replacer.cpp       # Prueba de reemplazo INI
|- test_iso_detection.cpp      # Prueba de detección ISO
|- test_iso_reader.cpp         # Prueba de lector ISO
|- test_list_formats.cpp       # Prueba de listado de formatos
|- CMakeLists.txt              # Configuración de construcción CMake
|- compilar.bat                # Script de construcción Windows
|- .clang-format               # Configuración de formato de código
|- LICENSE                     # Licencia GPL 3.0
|- README.md                   # Este archivo
|- ARCHITECTURE.md             # Documentación de arquitectura
|- release_notes.md            # Notas de versión
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
