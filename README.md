-crear particion de al menos 10GB que se pueda usar como particion booteable, en este proceso se reduce en 10GB el disco del sistema y usa ese espacio para este fin, si el espacio está disponible (si no hay espacio directamente da error)
-utilizando alguna herramienta por consola (ventoy, rufus, dd, etc) o directamente en C, C++ o Python  copiar el ISO a esta partición nueva de 10GB de forma que se pueda bootear.
-usando comandos C, C++ o Python crear una entrada de arranque BCD que solo se arranque en el siguiente reinicio que bootee ese ISO

## Interfaz Gráfica (GUI)

El proyecto incluye una interfaz gráfica desarrollada en C++ usando Qt para facilitar la configuración y ejecución de las operaciones.

### Requisitos para Compilar la GUI

1. **Qt5 o Qt6**: Descargar e instalar desde [qt.io](https://www.qt.io/download). Seleccionar Qt Creator o las librerías necesarias.
2. **CMake**: Ya instalado via winget.

### Compilación

1. Crear directorio build si no existe:
   ```
   mkdir build
   cd build
   ```

2. Configurar con CMake:
   ```
   cmake ..
   ```

3. Compilar:
   ```
   cmake --build .
   ```

4. Ejecutar:
   ```
   ./EasyISOBoot.exe
   ```

### Diseño de la GUI

Inspirado en el proyecto AGBackupMegaSync, la GUI cuenta con:
- **Header**: Logo, título y botón de guardar configuración.
- **Área Central**: Campo para seleccionar archivo ISO, botones para crear partición, copiar ISO y configurar BCD, área de logs.
- **Footer**: Versión y enlace a servicios.

Tema oscuro con colores similares al proyecto de referencia.