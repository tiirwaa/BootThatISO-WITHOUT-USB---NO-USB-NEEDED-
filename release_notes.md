# Notas de Versión - BootThatISO

## Características Actuales

### Sistema de Particiones
- Creación y gestión automática de particiones `ISOBOOT` (datos) e `ISOEFI` (EFI)
- Soporte para múltiples sistemas de archivos: FAT32, exFAT, NTFS
- Detección inteligente de particiones existentes
- Recuperación de espacio con extensión automática de volumen C:
- Validación de integridad con `chkdsk` opcional

### Procesamiento de ISOs
- Lectura de ISOs mediante 7-Zip SDK (UDF, ISO, Ext, MBR)
- Detección automática de ISOs de Windows vs Linux
- Caché de hash MD5 para evitar copias redundantes
- Dos modos de arranque:
  - **RAM Mode**: Carga boot.wim en memoria para arranque rápido
  - **EXTRACT Mode**: Instalación completa del contenido ISO
- Validación de integridad de archivos copiados (tamaño y DISM)

### Configuración de Boot
- Procesamiento inteligente de boot.wim con DISM
- Integración automática de drivers del sistema:
  - Categorías: Storage, USB, Network
  - Copia desde `C:\Windows\System32\drivers`
- Soporte para Hiren's BootCD PE (PECMD)
- Configuración de WinPE estándar (startnet.cmd)
- Integración de programas adicionales
- Procesamiento de archivos INI con reemplazo de letras de unidad
- Selector de edición de Windows (Home, Pro, Enterprise, etc.)

### Gestión de BCD
- Configuración automática del Boot Configuration Data
- Estrategias diferenciadas para RAM y EXTRACT modes
- Creación de entradas WinPE con RAMDisk
- Integración con EFI para arranque seguro

### Interfaz de Usuario
- Interfaz Win32 nativa con soporte MFC
- Barra de progreso principal y detallada
- Panel de logs en tiempo real
- Selector de idioma (Inglés, Español)
- Diálogo de selección de edición de Windows
- Botón de recuperación de espacio
- Modo desatendido para automatización

### Sistema de Logs
- Logs detallados en directorio `logs/`:
  - `general_log.log`: Cronología general
  - `diskpart_log.log`: Operaciones de particionado
  - `iso_extract_log.log`: Extracción de ISO
  - `bcd_config_log.log`: Configuración BCD
  - `copy_error_log.log`: Errores de copia
  - Y más...

## Requisitos del Sistema

- Windows 10 u 11 de 64 bits con privilegios de administrador
- PowerShell, DiskPart, bcdedit y herramientas de línea de comandos de Windows disponibles en el sistema
- Espacio libre mínimo de 12 GB en la unidad C: para crear y formatear particiones

## Arquitectura Técnica

### Patrones de Diseño Implementados
- **Facade Pattern**: `BootWimProcessor`, `ISOCopyManager`, `PartitionManager`
- **Strategy Pattern**: Integración de drivers, configuración BCD
- **Observer Pattern**: Sistema de eventos con `EventManager`
- **Chain of Responsibility**: Integración de programas, selección de edición
- **Template Method**: Creación de particiones, copia de ISO

### Módulos Principales
```
- boot/: Coordinación de arranque (BootWimProcessor)
- wim/: Operaciones WIM/DISM (WimMounter, WindowsEditionSelector)
- drivers/: Integración de drivers (DriverIntegrator)
- config/: Configuración PE (PecmdConfigurator, StartnetConfigurator, IniFileProcessor)
- filesystem/: Operaciones FS (ProgramsIntegrator)
- models/: Modelos de dominio (ISOReader, FileCopyManager, EventManager, etc.)
- services/: Servicios de aplicación (PartitionManager, ISOCopyManager, BCDManager)
- controllers/: Orquestación del flujo (ProcessController)
- utils/: Utilidades (Logger, LocalizationManager, Utils)
- views/: Interfaz Win32 (MainWindow, EditionSelectorDialog)
```

## Mejoras Recientes

### Refactorización de Arquitectura
- **Modularización**: Separación de responsabilidades en módulos especializados
- **Reducción de complejidad**: BootWimProcessor reducido de ~900 a ~250 LOC
- **Mejor mantenibilidad**: Cada clase con una responsabilidad única y clara
- **Testing mejorado**: Componentes aislados y testeables independientemente

### Sistema de Selección de Edición Windows
- Detección automática de ediciones disponibles en install.wim/install.esd
- Interfaz gráfica para selección de edición
- Integración con DriverIntegrator para drivers específicos de edición
- Validación de índices con DISM

### Enlazado Estático
- Runtime de C++ y MFC enlazado estáticamente (/MT)
- Ejecutable completamente autónomo
- No requiere Visual C++ Redistributable
- Menor dependencia de DLLs externas

## ISOs Probadas y Verificadas

### RAM Mode (Boot desde Memoria)
- ✅ HBCD_PE_x64.iso - TOTALMENTE FUNCIONAL
  - Carga todos los programas en RAM
  - Arranque rápido
  - Soporte completo PECMD
  
- ✅ Win11_25H2_Spanish_x64.iso - TOTALMENTE FUNCIONAL
  - Arranque e Instalación completa
  - Selección de edición
  - Integración de drivers
  
- ✅ Windows10_22H2_X64.iso - TOTALMENTE FUNCIONAL
  - Arranque e Instalación completa
  - Todas las ediciones soportadas

### EXTRACT Mode (Instalación Completa)
- ✅ HBCD_PE_x64.iso - Hace fallback a ISOBOOT_RAM
- ✅ Win11_25H2_Spanish_x64.iso - Hace fallback a ISOBOOT_RAM
- ✅ Windows10_22H2_X64.iso - Hace fallback a ISOBOOT_RAM

## Herramientas de Diagnóstico

### Utilidades de Prueba
```bash
# Listar formatos soportados por 7-Zip SDK
build\Release\ListFormats.exe

# Probar lectura y extracción de ISO
build\Release\TestISOReader.exe "ruta\al\archivo.iso"

# Detectar tipo de ISO (Windows/Linux)
build\Release\TestISODetection.exe "ruta\al\archivo.iso"

# Probar recuperación de espacio
build\Release\TestRecoverSpace.exe

# Validar traducciones
build\Release\ValidateTranslations.exe

# Ejecutar tests unitarios
cd build
ctest -C Release --output-on-failure
```

## Modo Desatendido

Ejecución automatizada vía línea de comandos:

```cmd
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\ruta\imagen.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr
```

## Limitaciones Conocidas

- Opera exclusivamente en Disco 0
- Reduce volumen C: en aproximadamente 10.5 GB
- Requiere privilegios de administrador
- Dependencia de Windows PowerShell
- Archivos de idioma requeridos en `lang/`

## Seguridad

- Operaciones destructivas en disco del sistema
- Se recomienda copia de seguridad antes de ejecutar
- Uso del botón "Recuperar espacio" para revertir cambios
- Validación de integridad con MD5 y DISM
- Logs detallados para auditoría

## Próximas Características (Roadmap)

- [ ] Soporte para múltiples discos
- [ ] Interfaz para configuración de drivers personalizada
- [ ] Soporte para más tipos de PE (Ventoy, etc.)
- [ ] Más idiomas (Francés, Alemán, Portugués)
- [ ] Modo de actualización sin reformatear
- [ ] Integración con Windows Terminal
- [ ] Soporte para arranque dual

---

## What's Changed (Historial de Cambios)

* UI Fixes by @tiirwaa in https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/pull/1
* screenshot by @tiirwaa in https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/pull/2
* Add cache-busting parameters to image URLs in README by @tiirwaa in https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/pull/3
* signed exe by @tiirwaa in https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/pull/4

## New Contributors
* @tiirwaa made their first contribution in https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/pull/1

**Full Changelog**: https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-/commits/master

---

Desarrollado por **Andrey Rodríguez Araya** en 2025.

Licencia: GPL 3.0
