# Investigación: Instalador de Windows no detecta discos tras iniciar en modo RAM

## Escenario reportado
- La aplicación crea `ISOEFI` y `ISOBOOT`, formatea y copia los artefactos sin errores aparentes.
- El arranque mediante la entrada `ISOBOOT_RAM` funciona y WinPE se carga.
- Dentro del asistente de instalación de Windows no aparecen discos disponibles para seleccionar el destino de la instalación.

## Evidencia en el código
- El script de particionado siempre opera sobre `disk 0` y asume que la unidad `C:` pertenece a ese disco (`src/services/partitionmanager.cpp:1545-1564`). En hardware con otro orden lógico, las particiones de soporte podrían quedar en un disco distinto al que se pretende reinstalar.
- El modo RAM solo copia `boot.wim`, `boot.sdi`, `install.wim` y algunos ejecutables auxiliares; no se inyectan controladores adicionales ni se modifica el `boot.wim` (`src/services/isocopymanager.cpp:252-333`). WinPE arranca con los controladores que trae el ISO original.
- La entrada BCD que lanza WinPE se crea como `OSLOADER` y apunta a `[ISOBOOT]\sources\boot.wim`, dejando `{ramdiskoptions}` con `partition=<letra>` (`src/models/RamdiskBootStrategy.h:27-91`). No se aplica ningún ajuste posterior sobre la política SAN ni sobre el estado online/offline de los discos una vez que WinPE está en memoria.

## Hipótesis principales
1. **El disco que contiene `ISOBOOT` queda offline en WinPE.**  
   WinPE suelen proteger el volumen donde reside el origen al iniciarse como RAMDisk. Si el disco queda marcado como `Offline` o `ReadOnly`, el asistente no lista ningún destino válido. Al no existir código que fuerce `san policy=OnlineAll` ni se habilite el disco tras el arranque, esta condición puede pasar desapercibida.

2. **Faltan controladores de almacenamiento para el hardware objetivo.**  
   En equipos con Intel VMD/RAID, NVMe propietario u otros controladores que requieren drivers OEM, la imagen de fábrica en `boot.wim` no tiene soporte. Al ejecutarse desde RAM no hay mecanismo que cargue esos controladores. El síntoma más común es precisamente un listado vacío de discos hasta cargar un `.inf` manualmente.

3. **Sesgo hacia `disk 0` en las rutinas de particionado.**  
   Si el disco del sistema no es `0`, el script `diskpart` tocará otra unidad. El arranque desde `ISOBOOT` funcionará (porque reside en ese otro disco), pero el asistente seguirá sin ver el disco donde realmente se quiere instalar Windows.

## Pasos de validación sugeridos
- Dentro de WinPE, abrir `diskpart` y ejecutar:
  ```
  san policy
  list disk
  select disk <n>
  online disk
  attributes disk clear readonly
  ```
  Confirmar si el disco aparece tras forzar el estado online.
- Revisar `X:\Windows\Panther\setupact.log` y `setuperr.log` para señales de discos offline o drivers faltantes.
- Ejecutar `wmic diskdrive get model,busType` o `Get-Disk` antes de lanzar la app para comprobar qué número de disco corresponde a la unidad del sistema.
- Probar con `Load driver` en el asistente cargando el paquete del controlador de almacenamiento del fabricante para descartar hipótesis 2.

## Líneas de trabajo potenciales
- Incorporar, en el proceso que prepara el modo RAM, un script post-arranque (por ejemplo `startnet.cmd`) que aplique `san policy=onlineall` y habilite el disco si aparece offline.
- Permitir al usuario seleccionar el `DiskNumber` objetivo antes de ejecutar `diskpart`, o detectarlo automáticamente en `PartitionManager` en lugar de asumir `disk 0`.
- Añadir soporte para integrar controladores a `boot.wim`/`install.wim` (DISM `Add-Driver`) cuando se detecte hardware que lo requiera.

No se realizaron modificaciones en el código fuente durante esta investigación.
