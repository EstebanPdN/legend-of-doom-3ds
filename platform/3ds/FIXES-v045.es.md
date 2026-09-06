# v0.45 — memoria de arranque del CIA

## Evidencia

El startup.log recibido identifica v0.44 / aec2812b834b y termina entre «actors: compiler environment» y «actors: ZScript parsed». fatal.log informa que realloc(768) falló: heap utilizado 46,192,472 bytes, arena 48,111,616, libres fragmentados 1,919,144. El proceso recibió 64 MiB; tras imagen estática y memoria lineal, el heap reservado era 48,635,904 bytes.

El dump físico 003-quick-20260905-233201 de v0.42 identifica New 3DS y 124 MiB de memoria de aplicación, con heap de 92 MiB. Superó la compilación ZScript. Sus valores al entrar al compilador son prácticamente idénticos a v0.44; la diferencia decisiva es el límite de memoria concedido.

Los CIA v0.42, v0.43 y v0.44 contienen el mismo modo extendido 124 MiB, pero también un modo legado 64 MiB. El SMDH genérico tiene flags 0x141 y carece del indicador New 3DS-only. El módulo PM de Luma puede seleccionar el límite legado por las banderas de lanzamiento, aunque el exheader solicite el modo New 3DS. Los registros no permiten identificar qué ruta o ajuste produjo ese lanzamiento de 64 MiB; sí confirman el fallo de reserva durante el arranque. No se atribuye a LOAD / DELETE ni al contenido del INI.

## Corrección

- SMDH marcado para New 3DS (bit 0x1000), conservando títulos, iconos y demás banderas.
- Modo principal 124 MiB; modo legado elevado a 96 MiB para que tampoco solicite 64 MiB si se fuerza esa ruta.
- Verificación obligatoria sobre el CIA empaquetado: lee el exheader y su icono en ExeFS y rechaza modos de memoria o indicador HOME incorrectos. Las comprobaciones anteriores solo verificaban el texto RSF y no detectaban este problema de lanzamiento.
- Motor, menú LOAD / DELETE, imagen inferior, banner y sonido HOME idénticos a v0.44. No se recupera la estereoscopía ni se alteran partidas o configuración del usuario.

139 pruebas superadas, incluidos casos de CIA con modo legado 64 MiB, modo New 3DS desactivado, indicador HOME ausente y archivos truncados. El verificador rechaza también el CIA real de v0.44. El arranque físico de esta corrección queda pendiente de prueba en New 3DS; no se ha utilizado Azahar.

## Referencias

- [Luma3DS: selección del límite de memoria al lanzar un programa](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/pm/source/launch.c), contrastado con la copia local en Diagnostics/upstream/Luma3DS.
- [bannertool: SMDH_FLAG_NEW_3DS](https://github.com/Steveice10/bannertool/blob/master/source/3ds/smdh.h), contrastado con la copia local de la herramienta.
- [Project_CTR: codificación de los modos en el exheader](https://github.com/3DSGuy/Project_CTR/blob/master/makerom/src/exheader.c), contrastado con la copia local de makerom.
