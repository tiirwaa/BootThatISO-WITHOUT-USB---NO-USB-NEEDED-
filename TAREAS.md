# Checklist de Tareas para EasyISOBoot
Se utilizará C++

Basado en el análisis del proyecto en `README_RECOMENDACION.md`, a continuación se presenta un checklist de las tareas principales a completar. Marca cada tarea como completada usando `[x]` cuando esté terminada.

## Tareas Principales

- [x] **Análisis y Diseño Inicial**
  - [x] Revisar y comprender los requisitos del proyecto descritos en README.md
  - [x] Identificar herramientas y bibliotecas necesarias para manipulación de discos y particiones (ej. diskpart, bcdedit, o bibliotecas en el lenguaje elegido)

- [x] **Implementación de Creación de Partición**
  - [x] Implementar reducción del disco del sistema en 10GB para crear una partición bootable
  - [x] Agregar validación de espacio disponible (fallar si no hay suficiente espacio)
  - [x] Implementar dos alertas de confirmación antes de iniciar la operación de modificación del disco

- [ ] **Implementación de Copia del ISO**
  - [ ] Elegir método para copiar el ISO (herramientas de consola como Ventoy, Rufus, dd, o implementación directa en código)
  - [ ] Asegurar que el ISO copiado sea bootable (compatible con Windows, Hirens, Linux, etc.)
  - [ ] Implementar copia del archivo ISO a la partición de 10GB

- [ ] **Configuración de BCD (Boot Configuration Data)**
  - [ ] Crear una entrada de arranque temporal en BCD que bootee el ISO solo en el siguiente reinicio
  - [ ] Implementar restauración de la configuración original de BCD después del boot temporal

- [ ] **Manejo de Errores y Seguridad**
  - [ ] Implementar manejo robusto de errores para evitar daños en el sistema (pérdida de datos, corrupción del disco)
  - [ ] Agregar validaciones adicionales y pruebas de seguridad

- [ ] **Pruebas y Validación**
  - [ ] Probar la creación de partición en un entorno seguro (no en producción)
  - [ ] Probar la copia del ISO y verificación de bootabilidad
  - [ ] Probar la configuración de BCD y restauración
  - [ ] Realizar pruebas integrales del sistema completo

- [ ] **Documentación y Finalización**
  - [ ] Documentar el código y el proceso de uso
  - [ ] Crear instrucciones de instalación y uso en `README.md`
  - [ ] Revisar y actualizar `README_RECOMENDACION.md` si es necesario

## Notas Adicionales
- Asegurarse de que el lenguaje de programación elegido soporte operaciones de bajo nivel (ej. C++, Python con bibliotecas específicas, o PowerShell/Batch para Windows).
- Considerar permisos administrativos requeridos para manipulación de discos y BCD.
- Mantener backups y precauciones durante el desarrollo y pruebas.