# v0.36: reparación del banner HOME

## Evidencia

El crash_dump_00000112.dmp corresponde a ARM11, proceso `menu` (0004003000009802): data abort, PC 0x0019efa4, lectura de 0x00000008. El juego todavía no se ha iniciado. La secuencia de instrucciones recorre una referencia relativa, compara los tipos 0x80000000 y 0x40000000 y termina leyendo desde un puntero nulo. Los tipos y desplazamientos coinciden con los registros LutTable/ReferenceLookupTable de la iluminación del CGFX; la identificación se infiere del código del conversor, sin símbolos de HOME.

El banner suministrado reproduce exactamente la salida de pycgfx. Su generador Patricia calcula los bits de búsqueda mediante `ord()` sobre caracteres Unicode, mientras que la tabla de cadenas escribe UTF-8. Con los nombres de materiales que contienen `•` y `ú`, los índices no corresponden a los bytes almacenados. La comprobación de búsqueda detecta 45 claves incorrectas en tres diccionarios, incluyendo el material Logo y su LUT. Esto explica una referencia de iluminación no resoluble y es consistente con el dump.

Fuentes primarias: [conversor pycgfx](https://github.com/skyfloogle/pycgfx), [parser oficial de Luma3DS](https://github.com/LumaTeam/luma3ds_exception_dump_parser). Revisiones locales del conversor: 1f78850086f3a77c41e07162e842f97a5bf3c18a.

## Reparación

Se reconstruyen únicamente los bits e índices hijo de los diccionarios defectuosos, usando los bytes UTF-8. Cambian 114 bytes de los 436344 del archivo. Geometría, texturas, colores, materiales, cadenas y punteros a los datos permanecen idénticos. El original de Banner_3D/versión final se conserva.

```sh
python3 platform/3ds/tools/validate-banner.py '../Banner_3D/versión final/Legend_of_Doom_banner.cgfx' --repair platform/3ds/assets/banner.cgfx
```

El empaquetado valida ahora que todas las claves encuentren su entrada exacta; una cabecera correcta y un tamaño inferior a 512 KiB ya no son suficientes para aceptar el banner.

## Verificación

Los 12 diccionarios y sus 71 claves pasan la comprobación tras la reparación. Pasan 128 pruebas, incluida la regresión con los tres nombres y los índices del banner defectuoso, preservación de datos fuera de los índices, idempotencia y claves UTF-8 adicionales. Se comprueba además que el CGFX corregido sea el contenido real del banner del CIA y se verifican los SHA-256. No se utiliza Azahar. La confirmación del resultado en HOME requiere la prueba del usuario en su 3DS.
