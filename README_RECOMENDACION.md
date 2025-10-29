# Análisis del README.md
Se utilizará C++

## Análisis del Proyecto

El proyecto descrito en `README.md` implica la implementación de un sistema para crear una partición bootable de al menos 10GB en el disco del sistema, copiar un archivo ISO a esa partición para que sea bootable, y configurar una entrada de arranque en BCD (Boot Configuration Data) de Windows que se active solo en el siguiente reinicio. Los puntos clave son:

1. **Creación de partición**: Reducir el disco del sistema en 10GB para crear una partición bootable. Si no hay espacio suficiente, el programa debe fallar con un error. (ANTES DE INICIAR ESTE PROCESO SE LE DEBE AVISAR MEDIANTE DOS ALERTAR QUE EL USUARIO DEBE CONFIRMAR LA OPERACION)

2. **Copia del ISO**: Utilizar herramientas de consola (como Ventoy, Rufus, dd) o implementar directamente en código para copiar el ISO(la idea es que pueda ser cualquier ISO: Windows, Hirens, Linux, etc) a la partición de 10GB, asegurando que sea bootable.

3. **Configuración de BCD**: Crear una entrada de arranque temporal en BCD que bootee el ISO solo en el siguiente reinicio, probablemente restaurando la configuración original después.

Este proyecto requiere operaciones de bajo nivel en el sistema operativo, incluyendo manipulación de discos, particiones y configuración de boot. Es crítico manejar errores adecuadamente para evitar daños en el sistema (por ejemplo, pérdida de datos o corrupción del disco).