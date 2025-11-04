# 🌐 Strings Faltantes en Archivos de Localización

**Fecha**: Noviembre 3, 2025  
**Estado**: Pendiente de localización

## 📋 Resumen

Se encontraron **más de 80 mensajes hardcodeados en español** que deberían estar en los archivos `lang/*.xml` para soportar múltiples idiomas correctamente.

---

## 🔴 **PRIORIDAD ALTA** - Mensajes que el usuario ve directamente

### **isocopymanager.cpp**
```xml
<!-- Mensajes de progreso del proceso -->
<string id="log.iso.analyzing">Analizando contenido del ISO...</string>
<string id="log.iso.windowsDetected">ISO de Windows detectado.</string>
<string id="log.iso.nonWindowsDetected">ISO no-Windows detectado.</string>
<string id="log.iso.copyingInstallFile">Copiando archivo de instalacion...</string>
<string id="log.iso.installFileCopied">Archivo de instalacion copiado y validado correctamente.</string>
<string id="log.iso.installFileError">Error/Advertencia al validar install.*; revise iso_extract.log.</string>
<string id="log.iso.retryingInstall">Reintentando extraccion de install.*...</string>
<string id="log.iso.installFileErrorFatal">Error al copiar archivo de instalacion.</string>
<string id="log.iso.copyingSetupFiles">Copiando archivos criticos de Setup...</string>
<string id="log.iso.setupFilesCopied">Archivos criticos copiados ({0} archivos).</string>
<string id="log.iso.integratingPrograms">Integracion de Programs en boot.wim para arranque RAM.</string>
```

### **bcdmanager.cpp**
```xml
<!-- Configuración de BCD -->
<string id="log.bcd.configuring">Configurando Boot Configuration Data (BCD)...</string>
<string id="log.bcd.efiSelected">Archivo EFI seleccionado: {0}</string>
<string id="log.bcd.unsupportedArchitecture">Error: Arquitectura EFI no soportada.</string>
<string id="log.bcd.errorDefault">Error al configurar default: {0}</string>
<string id="log.bcd.errorDisplayorder">Error al configurar displayorder: {0}</string>
<string id="log.bcd.errorTimeout">Error al configurar timeout: {0}</string>
<string id="log.bcd.settingWindowsDefault">Estableciendo Windows como entrada predeterminada y ajustando el tiempo de espera...</string>
```

### **WindowsEditionSelector.cpp**
```xml
<!-- Selector de ediciones de Windows -->
<string id="log.edition.extracting">Extrayendo imagen de instalación de Windows...</string>
<string id="log.edition.extractError">Error al extraer imagen de instalación.</string>
<string id="log.edition.noEditionsFound">No se encontraron ediciones de Windows.</string>
<string id="log.edition.autoSelectingSingle">Solo hay una edición disponible, seleccionando automáticamente.</string>
<string id="log.edition.selected">Seleccionada edición: {0}</string>
<string id="log.edition.preparingRAM">Preparando edición seleccionada para arranque RAM...</string>
<string id="log.edition.creatingFiltered">Creando imagen de instalación filtrada...</string>
<string id="log.edition.filteredError">Error al crear imagen filtrada.</string>
<string id="log.edition.injectingDrivers">Inyectando controladores de almacenamiento en install.wim...</string>
<string id="log.edition.driversInjected">Controladores inyectados en install.wim.</string>
<string id="log.edition.driversSaveFailed">Advertencia: No se pudieron guardar los controladores.</string>
<string id="log.edition.driversInjectFailed">Advertencia: No se pudo inyectar controladores en install.wim.</string>
<string id="log.edition.configuringBoot">Configurando entorno de arranque...</string>
<string id="log.edition.bootMountError">Error al montar boot.wim.</string>
<string id="log.edition.savingConfig">Guardando configuración...</string>
<string id="log.edition.preparedSuccess">Edición de Windows preparada correctamente.</string>
<string id="log.edition.setupWillSearch">Windows Setup buscará install.wim en la partición de datos.</string>
<string id="log.edition.saveConfigError">Error al guardar configuración.</string>
<string id="log.edition.windowsIsoDetected">Detectado ISO de instalación de Windows.</string>
<string id="log.edition.copyingFullImage">Copiando imagen de instalación completa (todas las ediciones)...</string>
<string id="log.edition.copyImageError">Error al copiar imagen de instalación.</string>
<string id="log.edition.imageCopiedSuccess">Imagen de instalación copiada. Windows Setup mostrará lista de ediciones.</string>
```

### **efimanager.cpp**
```xml
<string id="log.efi.extracting">Extrayendo archivos EFI al ESP...</string>
```

### **partitionmanager.cpp**
```xml
<!-- Gestión de particiones -->
<string id="log.partition.efiDetected">Partición EFI detectada con tamaño: {0} MB</string>
<string id="log.partition.efiIncorrectSize">ADVERTENCIA: Partición EFI con tamaño incorrecto ({0} MB, esperado {1} MB)</string>
<string id="log.partition.deletingOld">Error: No se pudieron eliminar las particiones antiguas.</string>
<string id="log.partition.deletedSuccess">Particiones antiguas eliminadas exitosamente.</string>
<string id="log.partition.efiCorrectSize">Partición EFI tiene el tamaño correcto ({0} MB)</string>
<string id="log.partition.recovering">Recuperando espacio para particiones...</string>
<string id="log.partition.recoveryFailed">Error: Falló la recuperación de espacio.</string>
<string id="log.partition.attemptingRestart">Intentando reiniciar el sistema...</string>
<string id="log.partition.tokenError">Error: No se pudo abrir el token del proceso.</string>
<string id="log.partition.privilegeError">Error: No se pudo ajustar los privilegios.</string>
<string id="log.partition.privilegeCheckError">Error: Falló la verificación de privilegios.</string>
```

### **ContentExtractor.cpp**
```xml
<string id="log.content.copying">Copiando contenido del ISO a {0}...</string>
<string id="log.content.copied">Contenido del ISO copiado correctamente.</string>
```

### **DiskpartExecutor.cpp / PartitionCreator.cpp**
```xml
<string id="log.diskpart.creatingScript">Creando script de diskpart para particiones...</string>
<string id="log.diskpart.scriptError">Error: No se pudo crear el archivo de script de diskpart.</string>
<string id="log.diskpart.executing">Ejecutando diskpart para crear particiones...</string>
<string id="log.diskpart.success">Diskpart ejecutado exitosamente. Verificando particiones...</string>
<string id="log.diskpart.failed">Error: Diskpart falló con código de salida {0}</string>
<string id="log.diskpart.partitionsCreated">Particiones creadas exitosamente.</string>
<string id="log.diskpart.partitionsFailed">Error: Falló la creación de particiones.</string>
```

### **mainwindow.cpp** (Algunos ya están localizados, otros no)
```xml
<string id="log.recovery.success">Recuperacion de espacio finalizada correctamente.</string>
<string id="log.recovery.failed">Recuperacion de espacio fallida. Revisa los detalles en los registros.</string>
```

---

## 🟡 **PRIORIDAD MEDIA** - Mensajes técnicos/debug

### **filecopymanager.cpp**
```xml
<!-- Errores técnicos de copia -->
<string id="log.filecopy.dirCreateError">Error: Failed to create directory {0} (Error {1})</string>
<string id="log.filecopy.dirCopyError">Error: Failed to copy directory {0} to {1} (Error {2})</string>
<string id="log.filecopy.fileCopyError">Error: Failed to copy file {0} to {1} (Error {2})</string>
<string id="log.filecopy.invalidPE">Error: Copied file appears invalid (not PE): {0}</string>
```

### **bcdmanager.cpp** (Mensajes debug)
```xml
<string id="log.bcd.volumeNameError">GetVolumeNameForVolumeMountPointW failed for {0}</string>
<string id="log.bcd.candidateEfi">Candidate EFI: {0}, machine=0x{1}</string>
<string id="log.bcd.resultDefault">Resultado /default: {0}</string>
<string id="log.bcd.resultTimeout">Resultado /timeout: {0}</string>
```

---

## ✅ **Recomendaciones de Implementación**

### 1. **Agregar todas las strings a los XMLs**
```xml
<!-- En lang/es_cr.xml -->
<string id="log.iso.analyzing">Analizando contenido del ISO...</string>

<!-- En lang/en_us.xml -->
<string id="log.iso.analyzing">Analyzing ISO content...</string>
```

### 2. **Modificar el código para usar LocalizedOrUtf8()**
```cpp
// ANTES:
eventManager.notifyLogUpdate("Analizando contenido del ISO...\r\n");

// DESPUÉS:
eventManager.notifyLogUpdate(
    LocalizedOrUtf8("log.iso.analyzing", "Analyzing ISO content...\r\n")
);
```

### 3. **Strings con parámetros (usar formateo)**
```cpp
// Para mensajes con valores dinámicos:
std::string msg = LocalizedOrUtf8("log.partition.efiDetected", 
    "EFI partition detected with size: {0} MB");
// Reemplazar {0} con el valor:
msg = std::regex_replace(msg, std::regex("\\{0\\}"), std::to_string(sizeMB));
eventManager.notifyLogUpdate(msg + "\r\n");
```

---

## 📊 **Estadísticas**

| Archivo | Mensajes Hardcodeados | Estado |
|---------|----------------------|--------|
| **isocopymanager.cpp** | ~15 | ❌ Sin localizar |
| **bcdmanager.cpp** | ~12 | ❌ Sin localizar |
| **WindowsEditionSelector.cpp** | ~25 | ❌ Sin localizar |
| **partitionmanager.cpp** | ~12 | ❌ Sin localizar |
| **efimanager.cpp** | ~1 | ❌ Sin localizar |
| **ContentExtractor.cpp** | ~2 | ❌ Sin localizar |
| **DiskpartExecutor.cpp** | ~7 | ❌ Sin localizar |
| **PartitionCreator.cpp** | ~7 | ❌ Sin localizar |
| **filecopymanager.cpp** | ~4 | ❌ Sin localizar |
| **mainwindow.cpp** | ~2 | ❌ Sin localizar |
| **TOTAL** | **~87 mensajes** | ❌ **0% localizado** |

---

## 🎯 **Plan de Acción Sugerido**

### Fase 1: Prioridad Alta (Usuario directo)
1. ✅ Agregar todos los mensajes de **isocopymanager.cpp**
2. ✅ Agregar todos los mensajes de **WindowsEditionSelector.cpp**
3. ✅ Agregar todos los mensajes de **bcdmanager.cpp** (usuario)
4. ✅ Agregar todos los mensajes de **partitionmanager.cpp**

### Fase 2: Prioridad Media (Técnico)
5. ⚠️ Agregar mensajes de **DiskpartExecutor.cpp**
6. ⚠️ Agregar mensajes de **ContentExtractor.cpp**
7. ⚠️ Agregar mensajes de **mainwindow.cpp** faltantes

### Fase 3: Prioridad Baja (Debug)
8. ⚪ Agregar mensajes debug de **bcdmanager.cpp**
9. ⚪ Agregar mensajes de **filecopymanager.cpp**

---

## 💡 **Notas Importantes**

1. **No todos los mensajes necesitan localización**:
   - Rutas de archivo: NO
   - Códigos de error técnicos: NO
   - Mensajes de log interno (solo para debugging): OPCIONAL
   - Mensajes que ve el usuario: **SÍ** ✅

2. **Mantener fallback en inglés**:
   ```cpp
   LocalizedOrUtf8("log.iso.analyzing", "Analyzing ISO content...")
   ```
   Siempre usar **inglés** como fallback, no español.

3. **Usar placeholders para valores dinámicos**:
   ```xml
   <string id="log.files.copied">Files copied: {0} of {1}</string>
   ```

---

**Generado por**: GitHub Copilot  
**Última actualización**: Noviembre 3, 2025  
**Estado**: Pendiente de implementación
