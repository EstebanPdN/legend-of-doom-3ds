# v0.39: corrección del arranque y del audio HOME

El crash_dump_00000113.dmp corresponde al juego Legend o (0004000005a2d000), core 0. PC 0x00484bec es FBaseCVar::SetGenericRep; LR 0x004e0ff4 pertenece al callback de lod3ds_render_scale. SP=0x08000000 y FAR=0x07ffffec muestran que el siguiente push salió del límite inferior de la pila. Los registros y bloques repetidos de 48 bytes en la pila confirman la recursión.

En v0.38 la normalización asignaba incondicionalmente self en los callbacks de escala y distancia. FBaseCVar::ForceSet llama al callback incluso si el valor no cambia. Por eso la inicialización se repetía indefinidamente. La prueba numérica anterior usaba variables simples y no representaba ese comportamiento; el compilador de scripts nativo tampoco ejecuta estos callbacks exclusivos de 3DS.

Ahora solo se reasigna si el valor normalizado es distinto y se retorna tras esa única reentrada. Una nueva prueba ejecuta los cuerpos C++ actuales con setters que reproducen la invocación síncrona del callback, incluso al asignar el mismo valor. Comprueba valores válidos, valores fuera de rango, NaN, resoluciones resultantes y distancias. También demuestra que la asignación incondicional falla tanto en escala como en distancia.

El audio v0.38 duraba 3.2628125 segundos y excedía el máximo documentado para banners HOME. Se adapta el mismo splash a 2.95 segundos con atempo (sin variar el tono) y una salida suave de 20 ms. Permanece estéreo PCM16, 32 kHz. El empaquetado rechaza ahora audio superior a 3 segundos, mono o truncado. Fuente técnica: https://3dbrew.org/wiki/CBMD .

El CGFX del logo permanece idéntico a v0.37: SHA256 168f01fc1960750a9a4dd9c1fbe061acf6ea17a4b7d87da3ac1615799cae1762. Se conservan los cambios de menús solicitados. No se prueba en Azahar ni se publica el CIA en GitHub. La confirmación final del arranque y del sonido corresponde a la prueba en la consola.
