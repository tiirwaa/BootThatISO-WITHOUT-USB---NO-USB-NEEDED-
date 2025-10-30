# EasyISOBoot

Proyecto para crear particiones bootables desde ISOs de Windows.

## Información Importante para Copilot

**IMPORTANTE**: Todas las pruebas y ejecución del programa se realizan en una máquina virtual donde existe una copia exacta del ISO `Windows10_22H2_X64.iso` en la ruta `C:\Users\vboxuser\Desktop\Windows10_22H2_X64.iso`.

El código fuente se desarrolla en la máquina local (`C:\Users\Andrey\Documentos\EasyISOBoot`), pero la ejecución y pruebas se hacen siempre en la máquina virtual con el ISO en la ubicación mencionada.

## Compilación

Para compilar el proyecto:

```batch
.\compilar.bat
```

Esto generará el ejecutable en `build\Release\EasyISOBoot.exe`.

## Logs de Depuración

Los logs se guardan en la carpeta `logs/`:


## Funcionalidades

- Detección automática de ISOs de Windows
- Extracción de archivos EFI
- Creación de particiones bootables
- Soporte para Windows 10/11 ISOs

## Desarrollo

El proyecto utiliza:
- C++ con WinAPI
- CMake para compilación
- PowerShell para montaje de ISOs