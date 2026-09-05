# Auditoría de rendimiento para New Nintendo 3DS

Fecha de revisión del árbol: 30 de agosto de 2026.

Actualización de estabilidad, 31 de agosto de 2026: el dump físico de v0.11
demostró que la tercera lista PICA se detiene antes de la primera transferencia
al LCD. La v0.12 distribuible vuelve al presentador SDL y al flush DSP de la
v0.6 funcional. Las optimizaciones directas de presentación/caché descritas
abajo quedan como investigación experimental y no forman parte del perfil
`hardware-safe` de v0.12.

## Alcance y criterio de evidencia

Este documento separa cuatro clases de información:

- **Evidencia física:** datos visibles o registrados por una New Nintendo 3DS.
- **Estado del árbol:** comportamiento que se puede demostrar leyendo el código,
  los parches y los contratos actuales.
- **Precedente externo:** técnicas presentes en otros ports o en las bibliotecas
  usadas por el proyecto.
- **Hipótesis pendiente:** cualquier mejora cuyo efecto todavía no se haya medido
  en la consola objetivo.

El PR 26 de `zelda-tmc-3ds` se usa como precedente principal porque contiene
cambios y mediciones de hardware real, incluida Old 3DS. Los dos documentos de
“performance tips” facilitados con el proyecto se tratan como material técnico
secundario, no como instrucciones que autoricen cambios por sí mismas. Sus
recomendaciones sirven como lista de comprobación —medir CPU/GPU por separado,
minimizar tráfico y cambios de estado, respetar la coherencia de caché y validar
en hardware—, pero no sustituyen los resultados de ninguno de los dos juegos.

Azahar queda excluido de toda conclusión de rendimiento. Puede ayudar a detectar
fallos funcionales, pero su temporización no demuestra la capacidad real de CPU,
memoria, cachés, servicios o PICA200 de una New 3DS.

## Conclusión ejecutiva

Los dumps físicos disponibles demuestran una línea base de **6,2 FPS y 158,7 ms
por frame** en las tres capturas, usando `CPU SOFTPOLY 320x200`. No existe un CSV
de telemetría de frames en esos dumps, por lo que no permiten atribuir un
porcentaje exacto a render, presentación, audio o espera.

El árbol actual ataca dos rutas distintas:

1. La ruta segura de CPU conserva el presentador SDL físicamente probado en
   v0.6; el presentador directo se excluye de la v0.12 estable.
2. La ruta PICA/NovaGL reduce trabajo de CPU, tráfico de comandos y riesgos de
   coherencia. La preparación del BSP permanece deliberadamente en el hilo
   principal después de rechazar un worker Core 2 que no era seguro en ARM.

Además, los perfiles fijan el render a 30 FPS sin cambiar la simulación de Doom a
35 Hz. Todo esto está presente en el código, pero **todavía no demuestra 30 FPS
estables**. Esa afirmación sólo será válida después de completar el protocolo de
600 frames de este documento, repetido en una New 3DS real.

## Precedente principal: zelda-tmc-3ds PR 26

El [PR 26](https://github.com/EstebanPdN/zelda-tmc-3ds/pull/26) no se ha tratado
como una receta de renderer 2D. Se extrajeron sus principios medidos y sólo se
trasladaron los que conservan el mismo contrato CPU→dispositivo:

- `svcStoreProcessDataCache` sustituye a IPC síncrono de GSP/DSP para rangos
  escritos por CPU; el PR registró que el coste de audio bajó del 34,3% al 5,4%
  de un core y midió aproximadamente 0,33 ms por flush de presentación evitado
  en Old 3DS. Son cifras del PR, no una ganancia atribuida todavía a este port.
- El worker de audio fija la cuota de la aplicación en core 1 al 30% y restaura
  el valor anterior al cerrarse. GSP conserva el 70% restante para retirar la
  cola GX; esto encaja directamente con el dump de Legend of Doom que dejó una
  lista PICA sin progreso durante cinco segundos.
- Telemetría de SD fuera del frame, redibujado inferior omitido cuando no cambia,
  rangos dirty acotados, representación retenida, índices persistentes y caché de
  estado ya estaban presentes en el árbol; se verificaron y no se duplicaron.
- El renderer PPU, el mezclador MP2K y la rutina `arm11_fast_mem` no se copiaron:
  son específicos de un juego 2D/Old 3DS y no existe evidencia de que sustituyan
  correctamente las rutas 3D, OpenAL o New-3DS/L2 de Legend of Doom.

La implementación resultante está en `cache_3ds.cpp`,
`novagl-direct-cache-clean.patch` y los dos parches de OpenAL. Se conserva un
fallback de servicio para launchers que rechacen el SVC y la CIA declara
explícitamente `StoreProcessDataCache` (syscall 0x53).

## 1. Diagnóstico de los dumps físicos

Los tres dumps corresponden al mismo artefacto:
`legend-of-doom-3ds-v0.3-hardware-safe-4b94e8f9bcc3`, `build_id=4b94e8f9bcc3`,
lanzado como CIA en New 3DS. Su manifiesto identifica el renderer como
`softpoly-sdl-linear-framebuffer`, con NovaGL enlazado pero sin inicializar.

| Dump | Lectura de la captura | Heap libre | Linear libre | Telemetría |
|---|---:|---:|---:|---|
| `quick-20260828-095421-000-00` | 6,2 FPS / 158,7 ms | 15,8 MiB | 16,8 MiB | compilada fuera |
| `quick-20260828-095548-001-00` | 6,2 FPS / 158,7 ms | 19,4 MiB | 16,8 MiB | compilada fuera |
| `full-20260828-095600-002-00` | 6,2 FPS / 158,7 ms | 19,4 MiB | 16,8 MiB | compilada fuera |

Conclusiones que sí permiten los dumps:

- La línea base física reproducida en las tres capturas es 6,2 FPS.
- Acercarse a una pared y alcanzar aproximadamente 11 FPS es una observación del
  usuario; no es una cifra registrada por estas tres capturas.
- Hay margen libre en ambos heaps durante las capturas. Eso descarta una falta de
  memoria inmediata, pero no descarta presión de ancho de banda, fallos de caché,
  fragmentación o exceso de trabajo por frame.
- Los contadores de Citro3D están a cero porque la ruta PICA no está activa. Por
  tanto, estos dumps no miden el renderer que se pretende usar como candidato.
- Los contadores de relleno de cielo prueban que hubo trabajo real de escena,
  pero no localizan por sí solos el coste dominante.

Límites de la evidencia:

- Ninguno de los tres directorios contiene `frame-telemetry.csv`;
  `frame_telemetry=compiled-out` aparece en sus manifiestos.
- Una captura de pantalla sólo da una lectura puntual/suavizada, no percentiles ni
  cadencia frame a frame.
- El dump completo tardó 192.025 ms (unos 192 s). Ese intervalo de E/S y pausa
  no debe entrar en ninguna ventana de rendimiento.
- Estos dumps son la línea base del artefacto `4b94e8f9bcc3`; no son una medición
  del árbol optimizado actual.

## 2. Cuello de botella de presentación software

La ruta física medida renderizaba una imagen BGRA de 320x200 en CPU y la llevaba
a la LCD superior lógica de 400x240. Antes de la ruta directa, la presentación
implicaba tres trabajos completos sobre el frame:

1. copiar el canvas del Poly renderer a una textura SDL;
2. escalar por vecino más cercano en el renderer software de SDL;
3. convertir/rotar otra vez al layout físico vertical de la LCD de 240x400.

Es una cantidad importante de lectura y escritura de memoria para un ARM11, y se
pagaba después de haber renderizado ya la escena. Los dumps no incluyen tiempos
por fase, así que es correcto llamarlo un cuello de botella plausible y
eliminable, no afirmar que explica por sí solo los 158,7 ms.

El árbol actual incorpora una vía específica en
[`sdlglvideo.cpp`](../../src/common/platform/posix/sdl/sdlglvideo.cpp), declarada
en [`i_video.h`](../../src/common/rendering/i_video.h) y usada desde
[`poly_framebuffer.cpp`](../../src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp):

- acepta exclusivamente canvas 320x200 de 32 bits, sin letterbox, hacia 400x240;
- comprueba que libctru entrega el framebuffer físico esperado, 240x400;
- reproduce el acumulador 16.16 de vecino más cercano de SDL;
- convierte el valor `AARRGGBB` del canvas al `RRGGBBAA` esperado por el scanout;
- escribe rotado directamente, en bloques de 8 filas, sin textura SDL intermedia;
- ejecuta una única limpieza directa `svcStoreProcessDataCache`, un único swap de la pantalla superior
  y una única espera de VBlank;
- espera antes a los workers que todavía puedan escribir el canvas;
- devuelve `false` y conserva la ruta SDL cuando cambian dimensiones, pitch,
  letterbox o transformaciones de color.

La propiedad del framebuffer sigue siendo de SDL/libctru: SDL inicializa y
configura el scanout RGBA8 antes de que el juego lo toque. La ruta directa sólo
abrevia la escritura de un frame cuya disposición ya ha sido establecida.

Esta optimización reduce presentación; **no convierte en GPU el render de la
escena**. SoftPoly sigue pagando geometría, columnas, sprites, cielo y escritura
del canvas completo. Si el render puro supera 33,33 ms, la fusión por sí sola no
puede producir 30 FPS.

## 3. Ruta PICA200/NovaGL

Los perfiles `hardware-candidate` y `hardware-diagnostic` usan NovaGL/Citro3D a
400x240. El primero conserva audio; el segundo es silencioso para aislarlo. El
perfil `release` usa la misma familia de renderer pero compila fuera la
telemetría, por lo que no debe ser el primer artefacto de medición.

El árbol actual contiene las siguientes defensas y optimizaciones:

- un solo slot de frame y una frontera real de finalización mediante la cola de
  Citro3D antes de reutilizar o liberar rangos;
- `C3D_FrameEnd(GX_CMDLIST_FLUSH)` junto con flush explícito de cada rango escrito
  por CPU, en vez del fallback que barre todo el linear heap;
- pre-flush alrededor de helpers de Citro3D que pueden dividir internamente una
  lista, y flush de texturas y mipmaps cuando corresponde;
- staging de transferencias único hasta que termina la cola y destrucción
  diferida de texturas, FBO, rings, VBO y transferencias;
- `glFinish()` drena incondicionalmente tanto comandos P3D como operaciones GX,
  incluso si el frame sólo contiene clears o copias;
- backing de comandos de 3 MiB segmentado a 192 KiB, muy por debajo de la lista
  histórica de 861.024 bytes que se bloqueó en hardware;
- rings acotados de 2 MiB para vértices y 512 KiB para índices;
- matriz model-view-projection combinada y recalculada sólo cuando queda sucia;
- deduplicación de estado, caché de color fijo, ordenación de texturas opacas e
  índices secuenciales estables para quads;
- rechazo y clipping antes de copiar geometría, reserva exacta del stream,
  tratamiento de planos de portal y guardas para stencil/Early-Z;
- targets de render en VRAM y texturas ordinarias muestreadas desde memoria
  linear escribible por CPU, evitando agotar la cola durante cargas masivas.

La sincronización se implementa en
[`novagl-explicit-cache-sync.patch`](patches/novagl-explicit-cache-sync.patch).
Depende de `C3Di_RenderQueueWaitDone`, una interfaz privada de la revisión fijada
de Citro3D; cualquier actualización de Citro3D obliga a reauditar ese contrato.

El bloqueo físico previo —incluido un clear temprano sin el framebuffer todavía
fijado— obliga a validar primero corrección y watchdog. Que la nueva ruta sea más
acotada no prueba todavía que la PICA complete todos los mapas ni que sostenga el
presupuesto de 33,33 ms.

## 4. Auditoría y rechazo del worker Core 2

Se prototipó un worker libctru persistente para mover la preparación del BSP a
Core 2. La revisión posterior encontró que activarlo habría convertido supuestas
ganancias en riesgos de corrupción o bloqueo:

- la cola 3DS tenía 50.000 entradas y escribía fuera del array sin comprobar el
  límite; además sus dos índices `seq_cst` compartían línea de caché y generaban
  barreras y contención en cada sondeo;
- `in_area` podía cambiar en el productor mientras el consumidor lo usaba;
- un portal visual unilateral podía entrar desde el worker en el allocator
  global no sincronizado de fake sectors;
- `section_renderflags`, `sector_t::MoreFlags` y `linedef->flags` tenían accesos
  concurrentes de lectura/modificación;
- una excepción o falta de memoria antes de publicar la terminación podía dejar
  el hilo principal esperando indefinidamente.

Por ello [`hw_bsp.cpp`](../../src/rendering/hwrenderer/scene/hw_bsp.cpp) compila
fuera en 3DS el pool, la cola y `WorkerThread`; todos los perfiles fuerzan
`gl_multithread=0`. Además de mantener semántica determinista, se elimina el pool
handheld de unas 600 KiB. La ruta multihilo original de escritorio queda intacta.

Un futuro intento requiere como mínimo un ring SPSC acotado con backpressure y
copia por valor, payloads con fake sectors ya preparados, separación de flags,
cancelación segura y A/B en New 3DS física. No se habilitará por intuición ni por
temporización de emulador.

## 5. Defaults de presupuesto: render 30 Hz, lógica 35 Hz

[`i_main.cpp`](../../src/common/platform/posix/sdl/i_main.cpp) fija en los
perfiles 3DS:

```text
vid_vsync=1
vid_maxfps=30
cl_capfps=0
```

La pantalla trabaja a 60 Hz y el límite de 30 FPS busca una cadencia de dos
VBlanks por frame; la distribución real debe comprobarse en el CSV físico.
La simulación original de Doom sigue a **35 Hz**; no se ha cambiado el tick ni la
lógica de juego. `cl_capfps=0` mantiene el desacoplamiento/interpolación entre
ticks y frames. El manifiesto deja ambos valores por separado:
`render_cap_fps=30` y `game_tick_hz=35`.

Los defaults también recortan trabajo no esencial para el presupuesto portátil:
luces dinámicas, sombras de actores, filtros/mipmaps, efectos de partículas
costosos y recursión profunda de portales/espejos; limitan partículas a 1024,
ponen recursiones a 1 y habilitan ordenación de texturas. Son decisiones de
calidad/rendimiento explícitas, no cambios en las reglas del juego.

## 6. Auditoría de compilador

La toolchain instalada es `devkitARM 15.2.0`. Su plataforma CMake ya compila para
la máquina correcta:

```text
-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
-mword-relocations -ffunction-sections -D__3DS__
```

La configuración Release de devkitPro usa `-O2 -DNDEBUG`. Por tanto, volver a
añadir esos flags de arquitectura no aportaría una optimización nueva.

Resultados de la auditoría local de alternativas:

- `-O3` aumentó el tamaño de código total un **11,82 %** y `r_all.o` un
  **18,52 %**. No se obtuvo una medición física que compensara ese crecimiento y
  el mayor footprint puede perjudicar la caché de instrucciones.
- LTO no produjo un artefacto válido: falló en las construcciones de ensamblador
  `areg`/`freg` y símbolos locales `.L*`.
- `-fdata-sections` empeoró el problema de anchors observado; no se incorpora.
- Se conserva **`-O2`** como baseline reproducible. Una futura prueba de `-O3`
  debería limitarse a archivos calientes y aceptarse sólo con A/B físico, no por
  tamaño o intuición.

La documentación oficial de
[opciones de optimización de GCC 15.2](https://gcc.gnu.org/onlinedocs/gcc-15.2.0/gcc/Optimize-Options.html)
define qué activa cada nivel. Las cifras anteriores son mediciones de este
proyecto, no afirmaciones de GCC ni resultados de otro port.

## 7. Flush directo de caché: reactivado sólo para audio en v0.19

Los parches históricos
[`openal-soft-3ds-fast-cache-flush.patch`](patches/openal-soft-3ds-fast-cache-flush.patch)
y [`novagl-direct-cache-clean.patch`](patches/novagl-direct-cache-clean.patch)
ensayaron `svcStoreProcessDataCache` (clean-only). La v0.12 retiró ambos al
recuperar la ruta estable. La v0.19 reactiva esa semántica exclusivamente en
OpenAL mediante
[`openal-soft-3ds-audio-stability.patch`](patches/openal-soft-3ds-audio-stability.patch):
NovaGL sigue fuera del render del mundo y el presentador CPU conserva su
contrato probado. El audio mantiene `DSP_FlushDataCache` como fallback si el
launcher rechaza el SVC y duplica la cola NDSP de cuatro a ocho buffers.

La línea se investigó por dos fuentes primarias:

- [libctru issue #556](https://github.com/devkitPro/libctru/issues/556), un RFC
  abierto/reporte de usuario que observa jitter de varios milisegundos en
  `DSP_FlushDataCache` bajo determinadas condiciones de cuota de CPU;
- [audio de sm64-port para 3DS](https://github.com/mkst/sm64-port/blob/3ds-port/src/pc/audio/audio_3ds.c#L132-L135),
  que usa el flush del proceso como precedente de implementación.

El issue no es una garantía oficial cerrada y su medición no equivale por sí
sola a este juego. La decisión de v0.19 se apoya además en el dump físico 002:
el decoder seguía dentro de contenido audible, sin error, y la fuente OpenAL
permanecía en reproducción cuando la salida desapareció. Eso localiza el corte
en la entrega posterior al mixer y justifica el cambio acotado; la consola real
sigue siendo quien debe validar que el síntoma desapareció.

## 8. Investigación de ports y bibliotecas

Las referencias siguientes son fuentes primarias de sus propios proyectos. Sólo
se trasladan patrones que encajan con el contrato local; sus afirmaciones de
rendimiento no se trasladan a Legend of Doom.

| Fuente primaria | Técnica relevante observada | Límite de la inferencia |
|---|---|---|
| [Citro3D: `renderqueue.c`](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c) y [`texture.c`](https://github.com/devkitPro/citro3d/blob/master/source/texture.c) | Semántica real de cola, final de frame, transferencias y coherencia de texturas | Es la base del contrato de sincronización; la función privada fijada exige conservar la revisión |
| [Forsaken-3DS](https://github.com/colbyshores/forsaken-3ds) | Agrupación por textura/estado, draws combinados, PVS/scissor y texturas compactas | Su README se atribuye a sí mismo una cadencia alta; no es un benchmark comparable a GZDoom |
| [SRB2 3DS](https://github.com/derrekr/srb2_3ds) | Renderer Citro3D, caché/precarga de texturas y reducción de cambios de estado | El repositorio demuestra una arquitectura, no 30 FPS para este contenido |
| [DXX-3DS](https://github.com/RossMeikleham/DXX-3DS) | Precarga por nivel y cachés de texturas combinadas | Motor, geometría y carga son diferentes; no permite extrapolar FPS |
| [PrBoom 3DS](https://github.com/devinacker/prboom-3ds) | Port clásico preliminar con framebuffer directo y caché por nivel | Un Doom clásico software tiene un coste muy inferior al Poly/GZDoom usado aquí |
| [PrBoom-Plus 3DS](https://github.com/Voxel9/PrBoom-Plus-3DS) | Wrapper PICA/Citro3D y precarga; su README compara sus propios modos | Sus comentarios de rendimiento son auto-reportados y no constituyen evidencia para Legend of Doom |

La conclusión común útil es reducir copias completas, ordenar y agrupar estado,
precargar con límites, mantener datos en una representación compatible con el
consumidor y sincronizar únicamente los rangos cuya propiedad cambia. El árbol
actual aplica esos principios, pero su eficacia debe aparecer en los contadores
y tiempos propios.

## 9. Protocolo exacto de validación en New 3DS

### 9.1 Preparación

1. Usar una **New Nintendo 3DS o New Nintendo 2DS XL real**, cargada o conectada
   a alimentación, con el slider 3D a cero y sin descargas ni aplicaciones en
   segundo plano. No usar Azahar para aceptar o rechazar rendimiento.
2. Registrar modelo, versión de sistema/Luma, método de lanzamiento y tarjeta SD.
3. Conservar el CIA/3DSX exacto, ZIP de símbolos y `BUILD-MANIFEST.txt`. Verificar
   que `build_id`, perfil y hashes son idénticos en todas las repeticiones.
4. Empezar por `hardware-diagnostic` para medir PICA sin audio. Después repetir
   con `hardware-candidate`, que conserva OpenAL/NDSP. Sólo probar `release`
   después de superar corrección y rendimiento en el candidato instrumentado.
5. Usar un save y una posición/cámara reproducibles. Preparar tres escenas: una
   vista abierta exigente, combate representativo y la pared como control. No
   mover cámara ni introducir input durante cada captura.

`hardware-safe` sirve como control funcional de CPU, pero actualmente tiene la
telemetría compilada fuera. Una afirmación percentil de esa ruta requiere un
artefacto safe instrumentado equivalente; no debe reconstruirse a partir de la
lectura puntual del overlay.

### 9.2 Ventana sin contaminación de E/S

Para cada perfil, escena y repetición:

1. Arrancar desde cero, cargar el save, fijar la cámara y esperar a que terminen
   cargas/precaches visibles.
2. Pulsar **L+R+A una sola vez antes de la ventana medida**. Ese quick dump guarda
   manifiesto, estado, pantallas y el segmento parcial de telemetría. Esperar a
   que la interfaz vuelva por completo al juego.
3. Desde ese momento no pulsar ningún chord de dump, no abrir HOME, no pausar y
   no producir E/S voluntaria.
4. Dejar completar al menos **1.440 frames**. La telemetría rota cada 720 frames;
   dos rotaciones garantizan que el CSV raíz más reciente sea un segmento
   completo situado íntegramente después del quick dump, con independencia de
   la fase en que se pulsó el chord. A 30 FPS son al menos 48 s; con la línea base
   de 6,2 FPS son unos 4 min. Contar frames es la regla; el tiempo sólo es una
   ayuda conservadora.
5. Salir normalmente, sin crear otro quick/full dump. Un dump posterior volvería
   a escribir el CSV raíz con el segmento parcial y contaminaría el protocolo.
6. Copiar por separado:
   `sdmc:/3ds/legend-of-doom/frame-telemetry.csv` y el directorio del quick dump
   creado en el paso 2. No analizar el CSV incluido en ese quick: es la marca
   anterior a la ventana.

Comprobaciones antes de resumir:

- el CSV raíz debe tener cabecera más **720 filas** válidas;
- su `build_id`, `profile` y `hardware_target` deben coincidir con el manifiesto;
- el primer número de `frame` del CSV raíz debe ser mayor que el último `frame`
  del CSV guardado en el quick dump previo;
- los timestamps deben ser crecientes y no debe haber mezcla de escenas;
- si falta el CSV, hay menos filas o falla la comparación de seriales, rechazar
  la captura y repetir; no rellenar datos ni combinar ejecuciones.

### 9.3 Resumen de 120 + 600 frames

Desde la raíz del repositorio:

```sh
python3 platform/3ds/tools/summarize-telemetry.py \
  /ruta/al/frame-telemetry.csv --warmup 120 --frames 600

python3 platform/3ds/tools/summarize-telemetry.py \
  /ruta/al/frame-telemetry.csv --warmup 120 --frames 600 --json \
  > /ruta/al/resumen.json
```

El resultado aceptable debe decir exactamente:

```text
frames: 600 (warm-up discarded: 120)
```

El script prefiere `timestamp_delta_ms`, que mide el intervalo real entre frames
terminados. `render_present_ms` sólo cubre una fase y no se debe convertir por sí
solo en FPS. El filtro del script descarta discontinuidades de dump/depurador de
más de un segundo y cinco veces la mediana, pero el protocolo coloca el dump
fuera de la ventana para no depender de ese filtro.

Repetir **tres veces** cada combinación perfil/escena. Conservar todos los CSV,
JSON, quick dumps y hashes; no publicar sólo la mejor ejecución.

## 10. Puerta de aceptación y límites honestos

Para calificar una escena como 30 FPS estables, las tres repeticiones deberían
cumplir, como mínimo:

- arranque, primer frame y 600 frames sin watchdog, cuelgue, corrupción, pérdida
  de controles ni error de HOME/resume;
- `average_fps` entre 29,5 y 30,5 y `one_percent_low_fps` no inferior a 29;
- `timestamp_delta_ms` con p50 alrededor de 33-34 ms y p99 no superior a
  34,5 ms;
- ausencia de frames de 50/66,7 ms, que indicarían uno o más VBlanks perdidos;
- memoria libre sin tendencia descendente entre repeticiones y sin crecimiento
  no acotado de uploads, draws o buffers diferidos;
- el perfil con audio no debe introducir una regresión reproducible frente al
  perfil silencioso.

`frames_over_33_33_ms` no debe usarse como un booleano de aprobado: 30 Hz ideal
son 33,333... ms y la cuantización a milisegundos puede contar frames correctos
como superiores a 33,33. Deben juzgarse distribución, múltiplos de VBlank,
percentiles y 1 % low conjuntamente.

Si PICA falla corrección, `hardware-safe` sigue siendo la recuperación válida,
pero su nuevo presentador también necesita una captura física instrumentada para
saber cuánto se ha ganado. Si PICA es correcta pero supera 33,33 ms, los
contadores de draw calls, vértices, topologías, memoria y tiempos Citro3D deben
decidir la siguiente optimización; no se debe adivinar.

## Estado final de esta auditoría

- **Demostrado en hardware anterior:** 6,2 FPS / 158,7 ms, SoftPoly 320x200.
- **Demostrado en el árbol:** presentador directo con fallback, ruta PICA acotada,
  sincronización explícita, BSP síncrono sin la cola insegura de 3DS, cap de
  render a 30 y lógica a 35 Hz, y flush rápido de audio integrado con fallback.
- **No demostrado todavía:** ahorro individual de cada cambio, ausencia de
  regresiones físicas y 30 FPS estables.

La versión puede considerarse una candidata seriamente optimizada y medible,
pero no una versión de 30 FPS confirmados hasta completar la matriz física
anterior y conservar sus 600 frames por ejecución.
