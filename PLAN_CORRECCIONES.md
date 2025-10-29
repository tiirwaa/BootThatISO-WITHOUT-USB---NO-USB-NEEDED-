# Plan de Correcciones para EasyISOBoot

## Problema Actual
- Error de boot 0xc000000f (archivo no encontrado) en ambos ISOs (Windows 10 y Hiren's).
- Uso de RAMDISK consume mucha RAM (~3-5GB).
- La partición no es completamente booteable sin configuración adicional del firmware.

## Solución Propuesta
Cambiar el enfoque para copiar el contenido completo del ISO a la partición y hacerla booteable directamente, sin RAMDISK.

## Checklist de Correcciones

### 1. Modificar Extracción de ISO
- [ ] Cambiar `extractISOContents` en `isocopymanager.cpp`:
  - Para Windows ISOs: Copiar todo el contenido del ISO a la partición (no solo EFI).
  - Para no Windows ISOs: Copiar todo el contenido del ISO a la partición.
- [ ] Eliminar la copia separada del archivo ISO (`copyISOFile`).

### 2. Cambiar Formato de Partición
- [ ] Modificar `createPartition` en `partitionmanager.cpp`:
  - Para Windows ISOs: Formatear como NTFS.
  - Para no Windows ISOs: Mantener FAT32.

### 3. Actualizar Configuración BCD
- [ ] Modificar `configureBCD` en `bcdmanager.cpp`:
  - Eliminar configuración de RAMDISK.
  - Para Windows ISOs: `path \windows\system32\winload.efi`.
  - Para no Windows ISOs: `path \efi\boot\bootx64.efi`.
  - Mantener `device partition=<volume GUID>`.

### 4. Actualizar Lógica en MainWindow
- [ ] Modificar `OnCopyISO` en `mainwindow.cpp`:
  - Llamar a `extractISOContents` para copiar todo.
  - No llamar a `copyISOFile`.
- [ ] Ajustar logs y mensajes para reflejar el nuevo proceso.

### 5. Pruebas
- [ ] Probar con Windows 10 ISO: Verificar que se copie todo, partición NTFS, BCD correcto, y boot exitoso.
- [ ] Probar con Hiren's ISO: Verificar que se copie todo, partición FAT32, BCD correcto, y boot exitoso.
- [ ] Verificar que no se use RAMDISK y que la RAM no se sobrecargue.

### 6. Documentación
- [ ] Actualizar README con el nuevo enfoque.
- [ ] Agregar notas sobre configuración del firmware UEFI si es necesario.

## Notas Adicionales
- Asegurar compatibilidad con UEFI (paths case-insensitive en NTFS/FAT32).
- Si el firmware no reconoce la partición, puede requerir configuración manual en el menú de boot de la VM/BIOS.
- Este enfoque hace la partición "booteable" sin depender de RAMDISK.