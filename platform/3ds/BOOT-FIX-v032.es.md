# Corrección del arranque v0.32

El CIA v0.31, build `c5a1db23bd01`, aborta al analizar ZScript. El dump físico
termina en `actors: compiler environment` y `fatal-error`; `fatal.log` registra
`1 errors while parsing LegendOfDoom.pk3:zscript`.

La causa está en `legend-of-doom-3ds.patch`: el bloque que crea
`actors/LegendPauseMenu.zs` anuncia 54 líneas, pero contiene 55. `git apply`
acepta el parche y omite la última llave de cierre. El PK3 de v0.31 contiene
literalmente ese archivo incompleto. El analizador reproduce `Unexpected end
of file` en la línea 54. La regeneración del mod para esa entrega expuso el
parche inconsistente; comprobar únicamente su aplicabilidad no lo detectó.

La v0.32 corrige el contador a 55 y añade validación estricta de los tamaños de
los bloques antes de descargar, aplicar parches o compilar. La prueba de
regresión altera de nuevo el contador a 54 y exige que sea rechazado; otra
aplica el bloque con Git y compara el archivo generado con todas las líneas
previstas. Las 116 pruebas Python y las seis pilas de parches pasan.

Se compiló además el motor de este árbol para macOS y se validó el mod corregido
con `-norun`: completa la inicialización y devuelve 1337 (57 en POSIX), tal como
establece `D_DoomMain`. Para permitir esa compilación se añadieron dos guardas
sin alterar la ruta 3DS: fallback falso de culling exclusivo de MAP01 en
escritorio y constantes OpenAL excluidas cuando `NO_OPENAL` está activo.

`validate-game-scripts.py` permite repetir la validación. El motor nativo debe
compilarse desde este árbol y tener sus `gzdoom.pk3` y `game_support.pk3`
correspondientes junto al ejecutable. `LOD3DS_SCRIPT_VALIDATOR` habilita esta
comprobación durante el empaquetado, antes de crear el CIA.

No se utilizó Azahar en esta corrección. La validación de scripts no demuestra
el arranque físico ni el rendimiento; esos resultados quedan para la New 3DS.
No hace falta borrar configuración ni partidas para corregir este fallo.
