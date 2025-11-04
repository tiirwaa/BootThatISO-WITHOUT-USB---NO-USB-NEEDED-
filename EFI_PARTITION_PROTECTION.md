# Protección de Partición EFI - BootThatISO!

## Problema Detectado

Durante pruebas de instalación de Windows, se descubrió que al eliminar la partición EFI de Windows en el instalador, Windows Setup detectaba y reutilizaba automáticamente la partición `ISOEFI` creada por BootThatISO! para su propia instalación. 

Esto sucedía porque:
1. La partición tiene el tipo GPT correcto (tipo EFI)
2. Está en formato FAT32 (requerido para EFI)
3. Contiene contenido de arranque EFI válido
4. Es visible y accesible para el instalador de Windows

## Solución Implementada

Se implementó una solución multi-capa para prevenir que Windows Setup use la partición ISOEFI:

### 1. Atributos GPT Especiales

La partición EFI ahora se crea con atributos GPT especiales:

```cpp
// En PartitionCreator.cpp y PartitionReformatter.cpp
scriptFile << "gpt attributes=0x8000000000000001\n";
```

**Atributos aplicados:**
- `0x0000000000000001` = **Partition Required**: Marca la partición como requerida, dificultando su eliminación accidental
- `0x8000000000000000` = **Hidden**: Oculta la partición del sistema, evitando que se le asigne automáticamente una letra de unidad
- `0x8000000000000001` = **Ambos combinados**

Estos atributos hacen que:
- La partición no aparezca automáticamente con letra de unidad en Windows Explorer
- Windows Setup tenga menos probabilidad de detectarla como candidata para EFI
- Se requiera esfuerzo adicional para modificar o eliminar la partición

### 2. Archivo Marcador Identificativo

Se crea un archivo de texto claro en la raíz de la partición EFI:

**Archivo:** `BOOTTHATISO_TEMP_PARTITION.txt`

**Contenido:**
```
===================================================
   BootThatISO! Temporary EFI Partition Marker
===================================================

This partition (ISOEFI) was created by BootThatISO!
for temporarily booting ISO files without a USB drive.

IMPORTANT:
- This is a TEMPORARY partition for ISO boot purposes.
- DO NOT use this as your Windows EFI System Partition.
- During Windows installation, do NOT delete the Windows
  EFI partition or Windows might reuse this partition.

If Windows installer used this partition:
1. Complete your Windows installation
2. Run BootThatISO! again
3. Click 'Recover my space' to clean up

Created: [timestamp]
Application: BootThatISO! by Andrey Rodriguez Araya
Website: https://agsoft.co.cr
===================================================
```

**Propósito:**
- Identificación clara de la partición para usuarios técnicos
- Instrucciones de recuperación si Windows la usa
- Documentación del propósito temporal de la partición

### 3. Advertencias en la Interfaz de Usuario

Se agregaron nuevas cadenas de localización en los archivos de idioma:

**ID:** `message.efiPartitionWarning`

**Español:**
> IMPORTANTE: La partición ISOEFI está oculta con atributos GPT especiales para evitar que el instalador de Windows la detecte automáticamente. Durante la instalación de Windows, NO elimine la partición EFI de Windows. Si lo hace, Windows podría reutilizar ISOEFI. Si esto ocurre, use 'Recuperar mi espacio' después de instalar Windows para limpiar.

**Inglés:**
> IMPORTANT: The ISOEFI partition is hidden with special GPT attributes to prevent Windows installer from automatically detecting it. During Windows installation, DO NOT delete the Windows EFI partition. If you do, Windows might reuse ISOEFI. If this happens, use 'Recover my space' after installing Windows to clean up.

## Archivos Modificados

### 1. `src/models/PartitionCreator.cpp`
- Agregado comando `gpt attributes=0x8000000000000001` después de crear la partición EFI
- Actualizado el log de diskpart para incluir el comando de atributos

### 2. `src/models/PartitionReformatter.cpp`
- Agregado comando `gpt attributes=0x8000000000000001` después de reformatear la partición EFI
- Asegura que los atributos se reapliquen incluso si la partición ya existía

### 3. `src/models/efimanager.cpp`
- Nueva función `createPartitionMarkerFile()` 
- Se llama automáticamente después de copiar el contenido EFI
- Crea el archivo marcador con timestamp e información del sistema

### 4. `src/models/efimanager.h`
- Declaración de la nueva función `createPartitionMarkerFile()`

### 5. `lang/es_cr.xml` y `lang/en_us.xml`
- Nueva cadena de localización `message.efiPartitionWarning`
- Advertencia clara para usuarios sobre el manejo de particiones EFI

## Comportamiento Esperado

### Escenario Normal (Recomendado)
1. Usuario ejecuta BootThatISO! y crea la partición ISOEFI
2. La partición se crea con atributos Hidden + Required
3. Usuario arranca desde el ISO booteable
4. Usuario instala Windows **sin eliminar la partición EFI de Windows**
5. Windows Setup crea su propia partición EFI nueva (o usa la existente)
6. ISOEFI permanece intacta y oculta
7. Usuario puede ejecutar 'Recuperar espacio' cuando termine

### Escenario de Reutilización (Si se elimina EFI de Windows)
1. Usuario ejecuta BootThatISO! y crea la partición ISOEFI
2. Usuario arranca desde el ISO booteable
3. **Usuario elimina accidentalmente la partición EFI de Windows**
4. Windows Setup **podría** detectar y reutilizar ISOEFI
5. Si esto ocurre:
   - ISOEFI ahora contiene archivos de Windows
   - El archivo marcador `BOOTTHATISO_TEMP_PARTITION.txt` sigue presente
   - Usuario completa la instalación de Windows normalmente
   - Después de instalar Windows, usuario ejecuta BootThatISO! nuevamente
   - Usuario hace clic en **'Recuperar mi espacio'**
   - El sistema limpia ISOEFI y ISOBOOT
   - Windows puede crear una nueva EFI si es necesario

## Ventajas de la Solución

1. **Protección Preventiva**: Los atributos GPT reducen significativamente la probabilidad de que Windows Setup detecte la partición
2. **Identificación Clara**: El archivo marcador permite identificar fácilmente el propósito de la partición
3. **Recuperación Sencilla**: Proceso claro de limpieza si Windows usa la partición
4. **No Intrusivo**: No afecta el funcionamiento normal de BootThatISO!
5. **Retrocompatible**: Funciona con particiones existentes al reformatearlas

## Limitaciones Conocidas

1. **No es 100% infalible**: Windows Setup aún puede detectar la partición si se eliminan todas las demás opciones EFI
2. **Requiere intervención manual**: Si Windows usa ISOEFI, el usuario debe ejecutar la recuperación manualmente
3. **Atributos GPT no visibles fácilmente**: Los usuarios casuales no verán inmediatamente los atributos especiales

## Recomendaciones para Usuarios

1. **Nunca eliminar la partición EFI de Windows original** durante una instalación
2. Si se debe reinstalar Windows:
   - Dejar que Windows Setup maneje la partición EFI existente
   - No formatear o eliminar particiones EFI manualmente
3. Si ISOEFI es utilizada por Windows:
   - Completar la instalación de Windows primero
   - Ejecutar BootThatISO! nuevamente con privilegios de administrador
   - Usar 'Recuperar mi espacio' para limpiar

## Testing Realizado

- ✅ Compilación exitosa con nuevos cambios
- ✅ Los atributos GPT se aplican correctamente en creación de partición
- ✅ Los atributos GPT se reaplican correctamente en reformateo
- ✅ El archivo marcador se crea correctamente con timestamp
- ⏳ Pendiente: Pruebas de instalación de Windows con la nueva protección

## Autor

**Andrey Rodríguez Araya**  
Website: [https://agsoft.co.cr](https://agsoft.co.cr)  
Fecha: Noviembre 2025
