# Arquitectura del Sistema - BootThatISO

## Visión General

BootThatISO es una aplicación Windows que permite arrancar sistemas operativos desde archivos ISO sin necesidad de USB. La arquitectura sigue principios SOLID y patrones de diseño establecidos para mantener código limpio, modular y extensible.

## Arquitectura de Alto Nivel

```
┌─────────────────────────────────────────────────────────────────┐
│                         PRESENTATION LAYER                       │
│  ┌──────────────────────┐        ┌───────────────────────┐     │
│  │   MainWindow         │        │ EditionSelectorDialog │     │
│  │   (Win32 UI)         │        │                       │     │
│  └──────────────────────┘        └───────────────────────┘     │
└────────────────────────┬─────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                       CONTROLLER LAYER                           │
│  ┌──────────────────────────────────────────────────────┐       │
│  │              ProcessController                        │       │
│  │         (Main Workflow Orchestrator)                  │       │
│  └──────────────────────────────────────────────────────┘       │
└────────────────────────┬─────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
┌─────────────────────────────────────────────────────────────────┐
│                        SERVICE LAYER                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ Partition    │  │ ISOCopy      │  │ BCD          │         │
│  │ Manager      │  │ Manager      │  │ Manager      │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
└────────────────────────┬─────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
┌─────────────────────────────────────────────────────────────────┐
│                        DOMAIN LAYER                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ Boot         │  │ WIM          │  │ Driver       │         │
│  │ Processor    │  │ Mounter      │  │ Integrator   │         │
│  ├──────────────┤  ├──────────────┤  ├──────────────┤         │
│  │ ISO          │  │ Content      │  │ Hash         │         │
│  │ Reader       │  │ Extractor    │  │ Verifier     │         │
│  ├──────────────┤  ├──────────────┤  ├──────────────┤         │
│  │ File         │  │ Programs     │  │ Volume       │         │
│  │ CopyManager  │  │ Integrator   │  │ Detector     │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
└────────────────────────┬─────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                    INFRASTRUCTURE LAYER                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ Logger       │  │ Utils        │  │ Localization │         │
│  │              │  │              │  │ Manager      │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
│  ┌──────────────┐  ┌──────────────┐                            │
│  │ Event        │  │ 7-Zip SDK    │                            │
│  │ Manager      │  │ (ISO Reader) │                            │
│  └──────────────┘  └──────────────┘                            │
└─────────────────────────────────────────────────────────────────┘
```

## Diagrama de Clases (Módulo Boot)

```
┌────────────────────────────────────────────────────────────────┐
│                     BootWimProcessor                            │
│                    (Orquestador/Facade)                         │
│                                                                  │
│  + processBootWim()                                             │
│  - extractBootFiles()                                           │
│  - mountAndProcessWim()                                         │
│  - extractAdditionalBootFiles()                                 │
└────────────────────────────────────────────────────────────────┘
                            │
                            │ coordina
                            ▼
    ┌────────────────────────────────────────────────────────────┐
    │                                                              │
    ▼                  ▼                 ▼                        ▼
┌─────────┐    ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
│WimMounter│    │DriverInteg. │   │PecmdConfig.  │   │IniFileProc.  │
└─────────┘    └─────────────┘   └──────────────┘   └──────────────┘
     │                │                   │                   │
     │                │                   │                   │
     ▼                ▼                   ▼                   ▼
┌─────────┐    ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
│DISM API │    │Windows      │   │Startnet      │   │Programs      │
│         │    │DriverStore  │   │Configurator  │   │Integrator    │
└─────────┘    └─────────────┘   └──────────────┘   └──────────────┘
                                          │
                                          ▼
                                   ┌──────────────┐
                                   │Windows       │
                                   │EditionSel.   │
                                   └──────────────┘
```

## Estructura de Directorios Detallada

```
src/
│
├── boot/                           # 🎯 Coordinación de arranque
│   ├── BootWimProcessor.cpp       ← Orquestador principal (~250 líneas)
│   └── BootWimProcessor.h         ← Interface principal
│
├── wim/                            # 💿 Operaciones WIM/DISM
│   ├── WimMounter.cpp             ← Mount/Unmount WIM
│   ├── WimMounter.h               ← DISM wrapper
│   ├── WindowsEditionSelector.cpp ← Selección de edición Windows
│   └── WindowsEditionSelector.h   ← Lógica de detección de ediciones
│
├── drivers/                        # 🔧 Integración de drivers
│   ├── DriverIntegrator.cpp       ← System + Custom drivers
│   └── DriverIntegrator.h         ← Categorización inteligente
│
├── config/                         # ⚙️ Configuración PE
│   ├── PecmdConfigurator.cpp      ← Hiren's BootCD PE
│   ├── PecmdConfigurator.h
│   ├── StartnetConfigurator.cpp   ← WinPE estándar
│   ├── StartnetConfigurator.h
│   ├── IniFileProcessor.cpp       ← Procesamiento INI
│   └── IniFileProcessor.h
│
├── filesystem/                     # 📁 Operaciones FS
│   ├── ProgramsIntegrator.cpp     ← Programs folder
│   └── ProgramsIntegrator.h
│
├── models/                         # 📦 Modelos de dominio
│   ├── ISOReader.cpp              ← 7-Zip wrapper para lectura ISO
│   ├── IniConfigurator.cpp        ← Drive letter replacement
│   ├── FileCopyManager.cpp        ← Progress tracking
│   ├── ContentExtractor.cpp       ← Extracción de contenido ISO
│   ├── HashVerifier.cpp           ← Verificación MD5
│   ├── efimanager.cpp             ← Gestión partición EFI
│   ├── isomounter.cpp             ← Montaje de ISO
│   ├── DiskIntegrityChecker.cpp   ← Verificación integridad disco
│   ├── VolumeDetector.cpp         ← Detección de volúmenes
│   ├── SpaceManager.cpp           ← Gestión de espacio
│   ├── DiskpartExecutor.cpp       ← Ejecución de diskpart
│   ├── PartitionReformatter.cpp   ← Reformateo de particiones
│   ├── PartitionCreator.cpp       ← Creación de particiones
│   └── EventManager.h             ← Observer pattern
│
├── services/                       # 🔨 Servicios de aplicación
│   ├── ISOCopyManager.cpp         ← Orquestación copia ISO
│   ├── BCDManager.cpp             ← Configuración BCD
│   ├── PartitionManager.cpp       ← Operaciones de disco
│   ├── PartitionDetector.cpp      ← Detección de particiones
│   ├── VolumeManager.cpp          ← Gestión de volúmenes
│   └── isotypedetector.cpp        ← Detección tipo ISO (Windows/Linux)
│
├── controllers/                    # 🎮 Controladores
│   ├── ProcessController.cpp      ← Workflow principal
│   ├── ProcessController.h
│   └── ProcessService.cpp
│
├── utils/                          # 🛠️ Utilidades
│   ├── Utils.cpp                  ← Utilidades generales
│   ├── Logger.cpp                 ← Sistema de logging
│   └── LocalizationManager.cpp    ← Gestión de idiomas
│
├── views/                          # 🖼️ UI
│   ├── mainwindow.cpp             ← Ventana principal Win32
│   └── EditionSelectorDialog.cpp  ← Diálogo selección edición
│
├── main.cpp                        # 🚀 Punto de entrada
└── SevenZipGuids.cpp              ← Definiciones GUID 7-Zip
```

## Flujo de Ejecución

```
Usuario inicia proceso
         │
         ▼
┌─────────────────────┐
│ ProcessController   │
│ (Main Workflow)     │
└─────────────────────┘
         │
         ├─→ PartitionManager
         │   ├─→ VolumeDetector
         │   ├─→ PartitionDetector
         │   ├─→ DiskIntegrityChecker
         │   ├─→ SpaceManager
         │   ├─→ DiskpartExecutor
         │   └─→ PartitionCreator
         │
         ▼
┌─────────────────────┐
│ ISOCopyManager      │
│ (Orchestrator)      │
└─────────────────────┘
         │
         ├─→ ISOTypeDetector
         ├─→ ISOReader (7-Zip)
         ├─→ ContentExtractor
         ├─→ HashVerifier
         └─→ FileCopyManager
         │
         ▼
┌─────────────────────────────────────┐
│      BootWimProcessor               │
│   (Facade Pattern)                  │
└─────────────────────────────────────┘
         │
         ├─→ extractBootFiles()
         │   └─→ ISOReader::extractFile()
         │
         ├─→ mountAndProcessWim()
         │   ├─→ WimMounter::mountWim()
         │   ├─→ WindowsEditionSelector::selectEdition()
         │   ├─→ PecmdConfigurator::isPecmdPE()
         │   │   ├─→ YES: configurePecmdForRamBoot()
         │   │   └─→ NO:  StartnetConfigurator::configure()
         │   ├─→ ProgramsIntegrator::integratePrograms()
         │   ├─→ DriverIntegrator::integrateCustomDrivers()
         │   ├─→ DriverIntegrator::integrateSystemDrivers()
         │   ├─→ IniFileProcessor::processIniFiles()
         │   └─→ WimMounter::unmountWim(commit=true)
         │
         └─→ extractAdditionalBootFiles()
         │
         ▼
┌─────────────────────┐
│ BCDManager          │
│ (BCD Config)        │
└─────────────────────┘
         │
         └─→ Configuración arranque BCD
```

## Dependencias entre Módulos

```
┌─────────────────┐
│BootWimProcessor│
└────────┬────────┘
         │
         ├──uses──→ WimMounter
         ├──uses──→ WindowsEditionSelector
         ├──uses──→ DriverIntegrator
         ├──uses──→ PecmdConfigurator
         ├──uses──→ StartnetConfigurator
         ├──uses──→ ProgramsIntegrator
         ├──uses──→ IniFileProcessor
         │
         └──depends on──┐
                        │
         ┌──────────────┴──────────────┐
         │                             │
    ┌────▼────┐                  ┌─────▼─────┐
    │ISOReader│                  │FileCopy   │
    │         │                  │Manager    │
    └─────────┘                  └───────────┘

┌──────────────┐
│ISOCopyManager│
└──────┬───────┘
       │
       ├──uses──→ ISOReader
       ├──uses──→ ISOTypeDetector
       ├──uses──→ ContentExtractor
       ├──uses──→ HashVerifier
       ├──uses──→ FileCopyManager
       ├──uses──→ EFIManager
       ├──uses──→ IniConfigurator
       └──uses──→ BootWimProcessor

┌──────────────────┐
│PartitionManager  │
└────────┬─────────┘
         │
         ├──uses──→ VolumeDetector
         ├──uses──→ PartitionDetector
         ├──uses──→ VolumeManager
         ├──uses──→ DiskIntegrityChecker
         ├──uses──→ SpaceManager
         ├──uses──→ DiskpartExecutor
         ├──uses──→ PartitionReformatter
         └──uses──→ PartitionCreator

┌──────────────────┐
│ProcessController │
└────────┬─────────┘
         │
         ├──uses──→ PartitionManager
         ├──uses──→ ISOCopyManager
         ├──uses──→ BCDManager
         └──uses──→ EventManager
```

## Patrones de Diseño en Acción

### 1. Facade Pattern
```cpp
BootWimProcessor // Fachada simple
    ├─→ WimMounter          (oculta complejidad DISM)
    ├─→ DriverIntegrator    (oculta lógica de drivers)
    ├─→ PecmdConfigurator   (oculta configuración PECMD)
    └─→ WindowsEditionSelector (oculta selección de edición)

ISOCopyManager // Fachada de orquestación
    ├─→ ISOReader           (oculta SDK 7-Zip)
    ├─→ ContentExtractor    (oculta extracción)
    └─→ HashVerifier        (oculta verificación MD5)

PartitionManager // Fachada de gestión de disco
    ├─→ VolumeDetector      (oculta detección volúmenes)
    ├─→ SpaceManager        (oculta gestión espacio)
    └─→ DiskpartExecutor    (oculta comandos diskpart)
```

### 2. Strategy Pattern
```cpp
DriverIntegrator::integrateSystemDrivers(
    DriverCategory::Storage  // Estrategia: solo storage
);

WimMounter::mountWim(
    wimPath, mountDir, index,
    [](int percent, const std::string& msg) { /* callback */ }
);

BCDManager // Diferentes estrategias de configuración BCD
    ├─→ configureBCD_RAM()      (estrategia RAMDisk)
    └─→ configureBCD_EXTRACT()  (estrategia instalación completa)
```

### 3. Chain of Responsibility
```cpp
ProgramsIntegrator::integratePrograms()
    ├─→ Intento 1: programsSrc
    ├─→ Intento 2: fallbackProgramsSrc
    └─→ Intento 3: extractFromISO()

WindowsEditionSelector::selectEdition()
    ├─→ Intento 1: Detectar ediciones vía DISM
    ├─→ Intento 2: Selección manual por usuario
    └─→ Intento 3: Usar edición por defecto
```

### 4. Observer Pattern
```cpp
EventManager::notifyProgress()
    └─→ Notifica a todos los EventObservers
        ├─→ UI actualiza barra de progreso
        └─→ Logger escribe en archivo

FileCopyManager // Propaga eventos de copia
    └─→ EventManager::notifyProgress()
        ├─→ MainWindow::updateProgress()
        └─→ Logger::logProgress()
```

### 5. Template Method Pattern
```cpp
PartitionCreator::createPartition()
    ├─→ validateSpace()        (paso común)
    ├─→ executeDiskpart()      (paso común)
    └─→ verifyCreation()       (paso común)

ISOCopyManager::copyISO()
    ├─→ detectISOType()        (paso común)
    ├─→ extractContent()       (variable según tipo)
    └─→ configureEFI()         (paso común)
```

## Métricas de Calidad

### Cohesión
- ✅ **Alta**: Cada clase tiene responsabilidad única
- ✅ **Métodos relacionados**: Todos los métodos de una clase trabajan con los mismos datos

### Acoplamiento
- ✅ **Bajo**: Clases dependen de abstracciones (callbacks, interfaces)
- ✅ **Inyección**: EventManager y FileCopyManager inyectados

### Complejidad Ciclomática
```
Antes: BootWimProcessor::mountAndProcessWim() = 45
Después:
  - BootWimProcessor::mountAndProcessWim() = 15
  - WimMounter::mountWim() = 5
  - DriverIntegrator::integrateSystemDrivers() = 8
  - PecmdConfigurator::configurePecmdForRamBoot() = 4
  Total distribuido: 32 (pero aislado en componentes)
```

### Líneas de Código por Clase
```
WimMounter:                   ~200 LOC
DriverIntegrator:             ~350 LOC
PecmdConfigurator:            ~180 LOC
StartnetConfigurator:          ~80 LOC
IniFileProcessor:             ~180 LOC
ProgramsIntegrator:           ~120 LOC
WindowsEditionSelector:       ~200 LOC
BootWimProcessor:             ~250 LOC (reducido de ~900)
PartitionManager:             ~400 LOC
ISOCopyManager:               ~600 LOC
BCDManager:                   ~350 LOC
FileCopyManager:              ~300 LOC
ISOReader:                    ~450 LOC
────────────────────────────────────
Total módulos core:           ~3660 LOC (bien distribuido y mantenible)
```

## Beneficios Tangibles

1. **Testing**: Cada clase puede tener su propio test suite
2. **Debugging**: Problemas aislados en componentes específicos
3. **Reusabilidad**: WimMounter puede usarse en otros proyectos
4. **Documentación**: Cada clase documenta su propósito claramente
5. **Onboarding**: Nuevos desarrolladores entienden más rápido
6. **Mantenimiento**: Cambios localizados, bajo riesgo de regresión

## Extensibilidad Futura

### Agregar nuevo tipo de driver
```cpp
// Solo modificar DriverIntegrator
enum class DriverCategory {
    Storage,
    Usb,
    Network,
    Audio,      // ← Nuevo
    Video       // ← Nuevo
};
```

### Agregar soporte para otro PE
```cpp
// Crear nuevo configurador
class VentoyConfigurator {
public:
    bool isVentoyPE(const std::string& mountDir);
    bool configureVentoy(...);
};

// Usar en BootWimProcessor
if (pecmdConfigurator_->isPecmdPE(mountDir)) {
    // ...
} else if (ventoyConfigurator_->isVentoyPE(mountDir)) {
    ventoyConfigurator_->configureVentoy(...);
}
```

### Agregar nuevo idioma
```xml
<!-- Crear nuevo archivo lang/fr_fr.xml -->
<?xml version="1.0" encoding="UTF-8"?>
<translations language="fr_fr">
    <item key="APP_TITLE">BootThatISO! - Créer une partition amorçable</item>
    <!-- ... más traducciones ... -->
</translations>
```

El sistema de localización (`LocalizationManager`) detectará automáticamente el nuevo archivo y lo cargará según la configuración del usuario.

## Sistema de Localización

### Arquitectura
```
┌─────────────────────────────────────┐
│    LocalizationManager              │
│  (Singleton Pattern)                │
├─────────────────────────────────────┤
│  - currentLanguage_                 │
│  - translations_ (map)              │
│  - availableLanguages_ (vector)     │
├─────────────────────────────────────┤
│  + loadLanguage(code)               │
│  + getString(key)                   │
│  + getAvailableLanguages()          │
│  + setLanguage(code)                │
└─────────────────────────────────────┘
         │
         │ lee archivos XML
         ▼
┌─────────────────────────────────────┐
│      lang/ directory                │
│  - en_us.xml (Inglés)               │
│  - es_cr.xml (Español)              │
│  - [otros idiomas]                  │
└─────────────────────────────────────┘
```

### Flujo de Traducción
```
App Inicio
    │
    ├─→ LocalizationManager::getInstance()
    ├─→ scanAvailableLanguages()
    │   └─→ busca archivos *.xml en lang/
    │
    ├─→ loadLanguage(defaultLang)
    │   ├─→ abre archivo XML
    │   ├─→ parsea con tinyxml2
    │   └─→ almacena en translations_ map
    │
    └─→ UI usa getString(key) para obtener textos
        └─→ devuelve traducción o key si no existe
```

### Validación de Traducciones
El proyecto incluye una herramienta de validación (`ValidateTranslations`) que:
- Verifica que todos los archivos de idioma tengan las mismas claves
- Detecta claves faltantes o duplicadas
- Valida formato XML correcto
- Se ejecuta como parte del proceso de construcción

```bash
# Validar traducciones
build\Release\ValidateTranslations.exe
```

## Conclusión

La arquitectura refactorizada es:
- ✅ **Modular**: Componentes independientes y reutilizables
- ✅ **Escalable**: Fácil agregar nuevas funcionalidades
- ✅ **Mantenible**: Cambios aislados, bajo acoplamiento
- ✅ **Testeable**: Cada componente puede probarse aisladamente
- ✅ **Profesional**: Sigue estándares de la industria (SOLID, Design Patterns)
