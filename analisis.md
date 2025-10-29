# Análisis del Proyecto EasyISOBoot

## Requisitos del Proyecto

Basado en el archivo `README.md`, el proyecto EasyISOBoot tiene los siguientes objetivos principales:

1. **Creación de Partición Bootable**:
   - Crear una partición de al menos 10GB en el disco del sistema.
   - Reducir el espacio del disco del sistema en 10GB para esta partición.
   - Validar que haya espacio disponible; si no, generar error.

2. **Copia del ISO**:
   - Copiar un archivo ISO a la nueva partición de 10GB.
   - Asegurar que el ISO copiado sea bootable (compatible con Windows, Hirens, Linux, etc.).
   - Usar herramientas de consola (Ventoy, Rufus, dd) o implementación directa en C++.

3. **Configuración de BCD (Boot Configuration Data)**:
   - Crear una entrada de arranque temporal en BCD.
   - La entrada debe bootear el ISO solo en el siguiente reinicio.
   - Restaurar la configuración original de BCD después del boot temporal.

## Lenguaje de Programación

El proyecto se implementará en **C++** con estándar C++17, como se especifica en `CMakeLists.txt`.

## Herramientas y Bibliotecas Necesarias

### Manipulación de Discos y Particiones
- **Windows APIs**: Para operaciones de bajo nivel en discos y particiones:
  - `CreatePartition` y funciones relacionadas de la API de Windows (windows.h).
  - `DeviceIoControl` para control de dispositivos de disco.
- **Comandos del Sistema**:
  - `diskpart`: Para crear y gestionar particiones desde línea de comandos.
  - `bcdedit`: Para modificar la configuración de arranque (BCD).

### Copia de Archivos ISO
- **Operaciones de Archivo Estándar**: Usar `<fstream>` de C++ para copiar archivos binarios.
- **Herramientas Externas**:
  - `dd` (si disponible en Windows) o equivalentes para copia a nivel de bloques.
  - Considerar bibliotecas como Boost.Filesystem para operaciones avanzadas de archivos.

### Configuración de BCD
- **bcdedit**: Comando principal para crear entradas BCD temporales.
- **Windows APIs**: Posiblemente `BcdStore` APIs para manipulación programática de BCD.

### Manejo de Errores y Seguridad
- **Validaciones**: Verificar permisos administrativos, espacio en disco, integridad del ISO.
- **Backups**: Crear backups de configuraciones críticas antes de modificaciones.
- **Manejo de Excepciones**: Usar try-catch en C++ para errores robustos.

## Consideraciones de Seguridad
- El programa requiere permisos administrativos para modificar discos y BCD.
- Riesgos: Pérdida de datos, corrupción del sistema si hay errores.
- Implementar múltiples confirmaciones antes de operaciones destructivas.

## Arquitectura Propuesta
1. **Módulo de Validación**: Verificar requisitos del sistema.
2. **Módulo de Partición**: Crear/reducir partición usando diskpart o APIs.
3. **Módulo de Copia**: Copiar ISO a la partición.
4. **Módulo BCD**: Configurar entrada temporal y restauración.
5. **Manejo de Errores**: Logging y recuperación de fallos.

## Próximos Pasos
- Implementar prototipos para cada módulo.
- Probar en entornos virtuales antes de producción.
- Documentar APIs y interfaces.