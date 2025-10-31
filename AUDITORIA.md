# Auditoría Técnica EasyISOBoot

## Build y Tooling
- [ ] Activar warnings altos y análisis estático desde CMake; actualmente solo se fija el estándar y `/utf-8` (`CMakeLists.txt:3`, `CMakeLists.txt:14`) sin `target_compile_options`, `clang-tidy` ni sanitizadores.
- [ ] Enumerar manualmente las fuentes y evitar duplicar el recurso; `file(GLOB_RECURSE...)` y la doble inclusión de `BootThatISO.rc` (`CMakeLists.txt:41`, `CMakeLists.txt:42`, `CMakeLists.txt:45`) generan builds no deterministas.
- [ ] Añadir una política de formateo automática (por ejemplo `.clang-format`) y engancharla en la build/CI; la falta de formato consistente se nota en bloques largos como `MainWindow::SetupUI` (`src/views/mainwindow.cpp:69`).

## Arquitectura y Diseño
- [ ] Sustituir la gestión manual de `MainWindow` en `WndProc` por RAII (`std::unique_ptr`) y destrucción controlada; hoy se hace `new`/`delete` explícito (`src/main.cpp:230`, `src/main.cpp:247`, `src/main.cpp:273`).
- [ ] Separar la construcción de UI, el layout y la carga de textos; `MainWindow::SetupUI` (`src/views/mainwindow.cpp:69`) mezcla responsabilidades y dificulta reutilizar o actualizar el diseño.
- [ ] Delimitar responsabilidades de `ProcessController`; la rutina `startProcess` (`src/controllers/ProcessController.cpp:70`) orquesta UI, logs y lógica de negocio en un mismo método, lo que complica testing y extensión (aplicar patrón Command/Service).
- [ ] Refactorizar `PartitionManager::createPartition` en pasos pequeños reutilizables y objetos de estrategia; la implementación actual (`src/services/partitionmanager.cpp:82`) supera el millar de líneas y mezcla scripting de DiskPart, chkdsk y BCD.
- [ ] Introducir un ejecutor de comandos del sistema reutilizable con RAII para handles; hoy se repite `CreateProcess` en `Utils::exec` (`src/utils/Utils.cpp:8`), `ISOCopyManager::exec` (`src/services/isocopymanager.cpp:68`) y `EFIManager::exec` (`src/models/efimanager.cpp:360`).

## Concurrencia y Ciclo de Vida
- [ ] Proteger el registro de observadores con sincronización; `EventManager` muta `observers` y los recorre sin locks (`src/models/EventManager.h:16`, `src/models/EventManager.h:29`), lo que puede fallar con callbacks desde hilos secundarios.
- [ ] Encapsular las notificaciones hacia la UI en mensajes asíncronos; `MainWindow::onLogUpdate` (`src/views/mainwindow.cpp:443`) escribe controles y archivos desde hilos de trabajo, provocando condiciones de carrera y bloqueos potenciales.
- [ ] Evitar bloquear la UI al cancelar procesos; `ProcessController::requestCancel` hace `join()` inmediato (`src/controllers/ProcessController.cpp:56`), lo que congela la ventana si el hilo tarda en limpiar.
- [ ] Reemplazar los `std::thread*` por miembros RAII (`std::thread`/`std::jthread`) y limpiar su ciclo de vida (`src/controllers/ProcessController.cpp:40`, `src/controllers/ProcessController.cpp:123`, `src/controllers/ProcessController.cpp:246`).

## Calidad de Código y Reutilización
- [ ] Consolidar `HashInfo` en una sola definición compartida; ahora se duplica en `ProcessController` y `ISOCopyManager` (`src/controllers/ProcessController.cpp:12`, `src/services/isocopymanager.cpp:23`).
- [ ] Centralizar la escritura en logs con un servicio thread-safe para evitar duplicados y corridas; tanto `EventManager::notifyLogUpdate` (`src/models/EventManager.h:36`) como `MainWindow::onLogUpdate` (`src/views/mainwindow.cpp:462`) escriben al mismo archivo sin coordinación.
- [ ] Revisar `MainWindow::workerThread`: se une en el destructor pero nunca se inicializa (`src/views/mainwindow.cpp:48`), lo que indica código muerto o fuga de responsabilidad.
- [ ] Parametrizar la unidad analizada en `getAvailableSpaceGB`; el valor fijo `C:` (`src/services/partitionmanager.cpp:76`) rompe escenarios donde el usuario quiere usar otro disco.
- [ ] Eliminar la duplicación de borrado de logs; `ClearLogs` (`src/main.cpp:42`) y la inicialización en `ProcessController::startProcess` (`src/controllers/ProcessController.cpp:75`) deberían compartir utilidades.

## Localización e Internacionalización
- [ ] Sustituir el parser manual de XML en `LocalizationManager::parseLanguageFile` por una librería robusta y validar errores (`src/utils/LocalizationManager.cpp:208`).
- [ ] Corregir la codificación de `lang/es_cr.xml`; el atributo `name` ya aparece como `Espa��ol` (`lang/es_cr.xml:2`), señal de archivos guardados en CP-1252.

## Automatización y CI/CD
- [ ] Añadir pruebas automatizadas para componentes lógicos (detector de ISO, parser de idioma, validaciones) y ejecutarlas en CI; el workflow de release solo compila (`.github/workflows/release.yml:28`).
- [ ] Corregir la codificación de las notas de release generadas; los acentos se malogran (`.github/workflows/release.yml:37`), por lo que los usuarios verán texto corrupto.

## Limpieza
- [ ] Retirar comentarios temporales como `// Force rebuild` (`src/services/partitionmanager.cpp:2`) para mantener el código limpio.
