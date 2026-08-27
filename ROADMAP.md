# Roadmap técnico de Legend of Doom 3DS

Estado de la auditoría: 27 de agosto de 2026
Objetivo de hardware: New Nintendo 3DS, New Nintendo 3DS XL y New Nintendo 2DS XL
Resolución objetivo: 400×240, pantalla superior, sin 3D estereoscópico en la primera versión estable

Actualización de hardware: los logs de una New 3DS real confirmaron un bloqueo de la cola PICA200 antes del primer frame. El primer bloqueo era una lista cruda de Early-Z enviada antes de que Citro3D enlazara el framebuffer. La corrección actual elimina esa ruta, restaura `C3D_FRAME_SYNCDRAW` para el único frame slot, usa clears ordenados por Citro3D y prohíbe presentar memoria lineal como render target cuando falla la reserva de VRAM. El candidato instrumentado que contiene estas correcciones sigue pendiente de validación física; por tanto, ninguna afirmación visual o de rendimiento posterior al arranque se considera cerrada.

## 1. Dictamen

El proyecto ya supera la fase de “¿puede arrancar GZDoom en una 3DS?”. Existe una cadena completa de compilación, una CIA autónoma con RomFS, una 3DSX, controles nativos, audio OpenAL/NDSP, diagnósticos de memoria y un puente GZDoom/NovaGL capaz de dibujar MAP01. Sin embargo, el estado actual todavía es una plataforma de diagnóstico, no una versión jugable ni una base sobre la que sea seguro optimizar a ciegas.

El orden correcto de trabajo es:

1. congelar un baseline reproducible y medible;
2. terminar la fidelidad gráfica del puente NovaGL;
3. validar juego completo, controles, guardado, audio y ciclo de vida;
4. medir CPU y GPU en una New 3DS real;
5. optimizar los cuellos de botella demostrados;
6. decidir con datos si NovaGL puede llegar a 60 FPS o si el mundo necesita un backend PICA200 directo;
7. ejecutar una matriz completa de regresión antes de publicar.

El primer objetivo es una imagen estable a 30 FPS. La optimización no comienza hasta que la consola complete el primer frame, y ninguna observación de emulador se acepta como prueba de compatibilidad o rendimiento de New 3DS.

## 2. Definiciones de terminado

### 2.1 Baseline correcto

Un build alcanza el baseline correcto cuando:

- arranca en frío como CIA y como 3DSX;
- llega al menú y a MAP01 sin argumentos exclusivos de diagnóstico;
- mantiene la cámara correcta durante al menos diez minutos;
- no presenta polígonos explosivos, huecos, cuñas ni residuos de otro pass;
- cielo, suelo, paredes, sprites, armas y HUD coinciden con una captura de referencia de GZDoom 4.7.1;
- no produce errores GL, overflow de command list/rings, OOM ni fallos de lectura;
- acepta todos los controles necesarios para jugar;
- reproduce efectos y música sin cortes;
- permite guardar, salir, arrancar de nuevo y cargar el guardado.

### 2.2 Versión 100 % jugable

Además del baseline correcto:

- MAP01–MAP10 pueden completarse desde un archivo nuevo;
- funcionan todas las transiciones, cuevas, tiendas, secretos y jefes;
- funcionan espada, escudo, arco, bumerán, bombas, vela, flauta, carne, pociones, varita e inventario;
- morir, reiniciar, pausar, abrir el mapa y volver al menú no rompen el estado;
- tres ranuras de guardado sobreviven a diez ciclos de guardar/cargar y a un reinicio de la consola;
- suspender/cerrar la tapa, reanudar, pulsar HOME y cerrar la aplicación no corrompen datos ni bloquean servicios;
- una sesión continua de dos horas no pierde memoria de forma creciente ni degrada audio o renderizado.

### 2.3 Objetivo de 60 FPS

60 FPS significa 60 imágenes distintas por segundo, no sólo 60 VBlanks. La simulación Doom puede conservar sus 35 tics por segundo con interpolación de render.

La puerta de rendimiento propuesta es:

- frame CPU p95 ≤ 16,67 ms;
- frame GPU p95 ≤ 16,67 ms;
- 1 % low ≥ 55 FPS en las rutas normales;
- ningún frame > 33,33 ms durante 30 minutos, salvo cargas y transiciones previamente marcadas;
- pacing sin alternancias perceptibles y sin corrupción al solapar CPU/GPU;
- medición en New 3DS real, con el mismo hash del CIA candidato.

Si el puente NovaGL no puede cumplir esa puerta después de optimizar los cuellos medidos, 60 FPS exige un backend PICA200 directo y específico para las funciones que Legend of Doom usa.

## 3. Inventario auditado

### 3.1 Código y cambios de plataforma

- Base: GZDoom 4.7.1, commit `107ff702423686414680d6458fea63a2647692c4`.
- El árbol `src/` contiene aproximadamente 555.000 líneas C/C++/headers; el repositorio completo añade herramientas, librerías y recursos.
- El port modifica unas 70 unidades del árbol original y añade la plataforma 3DS, scripts de build, siete parches de dependencias y recursos de empaquetado.
- El delta versionado visible en la auditoría es de unas 1.291 inserciones y 426 eliminaciones, además de unas 5.600 líneas nuevas todavía sin seguimiento.
- Subsistemas tocados: arranque SDL, memoria, input, audio, serialización, VM/ZScript, GLES, hardware renderer, SoftPoly, rutas, diagnósticos, CMake, CI y empaquetado.
- El primer commit público conserva GZDoom 4.7.1 como base identificable y reúne el baseline completo del port; las correcciones de hardware posteriores deben entrar en commits pequeños y bisectables.

### 3.2 Dependencias fijadas

- SDL2: `5d249570393f7a37e037abf22cd6012a4cc56a71`.
- ZMusic: `2b291705f2043f39d219a49c2671c80f1dd422e0`.
- minimp3: `ea99364f61c14656440e8d77e9c233ccf3124633`.
- NovaGL: `9cabf853fb57a1037bea55dbec81eea073b5ee6c`.
- Legend of Doom: `d7c66cf79fa00b112c17ea443fa63121120ff45b`.
- Freedoom: 0.13.0 con SHA-256 fijado.
- OpenAL Soft/NDSP: checkout y parche 3DS separados.

La pila de parches NovaGL vuelve a aplicar limpiamente sobre la revisión fijada. Esto es necesario, pero no prueba corrección gráfica ni reproducción bit a bit de los paquetes.

### 3.3 Recursos de juego

- El PK3 generado de Legend of Doom contiene 991 entradas y unos 15,3 MB sin comprimir.
- El origen contiene 841 PNG, 76 archivos ZScript, 11 WAD de mapas, 30 WAV, 7 MP3, 4 OGG y ACS compilado.
- ZScript suma aproximadamente 7.387 líneas; los tres fuentes ACS, aproximadamente 459 líneas.
- `MAP01.wad` es el mapa más grande, con aproximadamente 2,35 MB.
- Los cielos `SKYWW` y `SKYWW2` son 1024×128.
- Hay imágenes de mapa/HUD de hasta 2176×704 y varias de 1024–2048 px por eje.
- El límite global actual de textura 3DS es 256 px. Ese límite protege memoria, pero redimensiona indiscriminadamente cielos e imágenes grandes y debe sustituirse por una política por clase de recurso.
- La música principal es MP3 mono a 44,1 kHz. Algunos efectos son estéreo/44,1 kHz aunque la fuente visual y la consola no siempre justifican ese coste.

### 3.4 Paquete y memoria observada

- El CIA diagnóstico corregido ocupa aproximadamente 45 MB porque incluye GZDoom, Freedoom y Legend of Doom en RomFS.
- El ELF diagnóstico ocupa unos 13,1 MB entre texto, datos y BSS antes de heaps y recursos dinámicos.
- El perfil CIA solicita modo New 3DS de 124 MB y reserva 32 MiB de linear heap.
- En el primer frame observado quedaron aproximadamente 16,1 MiB del heap convencional, 24,8 MiB lineales y 1,7 MiB de VRAM.
- No hubo OOM, panic, overflow de rings ni parada del bucle durante la corrupción de cámara.
- NovaGL reserva una command list de 3 MiB, ring de vértices de 2 MiB, ring de índices de 512 KiB y staging de textura de 512 KiB.

## 4. Estado técnico por área

| Área | Estado | Evidencia | Puerta siguiente |
| --- | --- | --- | --- |
| Build cruzado | Parcial | CMake/devkitARM produce ELF, 3DSX y CIA | Build limpio desde cero y CI verde |
| Trazabilidad | Parcial | ID de 12 caracteres, hashes, ELF/map | Prohibir releases desde árbol dirty |
| CI | Implementada | El workflow resuelve el nombre desde el manifiesto, verifica hashes y bloquea contenido privado | Confirmar el primer run público limpio |
| Arranque CIA | Roto en New 3DS física | La cola PICA200 se detiene antes del primer frame | Probar el candidato conservador y devolver los cuatro logs |
| Arranque 3DSX | Compila | Rutas SD y CRT propios existen | Validación posterior en consola real |
| Cámara | Sin validar | No existe todavía un frame físico correcto | Validar después de cerrar el bloqueo de GPU |
| Cielo | Sin validar en hardware | Near clip está implementado, pero no hay imagen física estable | Suite visual después del primer frame |
| Color/luz | Sin validar en hardware | La ruta TEV/uniformes existe, pero no hay imagen física estable | Patrón de color y escenas de referencia |
| Texturas | Parcial | Subidas y fallback funcionan; límite 256 global | Samplers, wrap, traducciones y política por recurso |
| Portales/stencil | Parcial | Hay CPU user clip y Early-Z condicionado | Escenas de referencia y contadores de clipping |
| HUD/2D | Visible | HUD se dibuja | Colores, escalado, inventario y menú completo |
| Rendimiento hardware | Desconocido | No existe timing CPU/GPU del build actual en New 3DS | Telemetría y benchmark fijo |
| Rendimiento SoftPoly | No es objetivo | Las mediciones antiguas de emulador no representan el hardware | Mantener sólo como fallback diagnóstico |
| Input | Implementado | Mapeos de botones, sticks y touch | Matriz física en New 3DS/New 2DS XL |
| Audio | Implementado, no certificado | OpenAL inicia 32 fuentes; MP3 comienza | DSP real, loops, underruns, suspensión |
| Guardado | Sin validar | Rutas SD y serialización compilan | Suite guardar/cargar por mapa |
| Ciclo de vida | Parcial | Se corrigió un teardown antiguo | HOME, tapa, suspensión, salida y errores |
| Juego completo | Sin validar | Sólo MAP01 ha sido foco | Recorrido MAP01–MAP10 |
| Licencias | Bloqueo de publicación | Legend of Doom no declara licencia general | Permiso explícito de redistribución |

## 5. Causas confirmadas y defectos abiertos

### 5.1 Lista cruda de Early-Z — causa confirmada en hardware

`gpu-diagnostic.log` registró `TIMEOUT` con `draw_serial=0`: el memory fill del clear terminaba, pero la operación siguiente era una lista PICA de 48 bytes que nunca completaba. La ruta experimental emitía Early-Z antes de que Citro3D hubiera enlazado un framebuffer dentro del frame. El orden era inválido para la consola aunque un emulador lo tolerara.

La ruta de producción mantiene Early-Z desactivado. El clear experimental por shader también queda desactivado salvo compilación explícita y los clears normales pasan por `C3D_RenderTargetClear`.

### 5.2 Orden de frame y render targets — errores corregidos, pendientes de prueba

Con un único frame slot, el port había sustituido `C3D_FRAME_SYNCDRAW` por `0`, eliminando la barrera de presentación usada por el ejemplo oficial de Citro3D. Además, `nova_texture_make_vram_target` podía marcar como renderizable una textura que seguía en memoria lineal cuando `vramAlloc` fallaba, a pesar de que `C3D_RenderTargetCreateFromTex` sólo admite almacenamiento VRAM.

La ruta conservadora restaura `C3D_FRAME_SYNCDRAW`, evita dividir una command list vacía antes del clear y hace fallar limpiamente la promoción a render target si no existe VRAM. Estas correcciones están compiladas, pero sólo la siguiente ejecución física puede demostrar cuál era el segundo bloqueo observado después de retirar Early-Z.

### 5.3 Supervisor del primer frame

Cada draw anterior al primer swap se envía por separado con un límite de dos segundos. Ante un timeout se guardan el serial, las palabras de comando acotadas, las direcciones físicas de color/depth, atributos, buffers y estado de la cola GX. El proceso sale mediante `svcExitProcess` para no entrar en un teardown que espere indefinidamente a la GPU.

Este supervisor no convierte el build en una versión jugable; convierte el siguiente fallo físico en evidencia accionable y pretende evitar que el usuario tenga que reiniciar la consola a la fuerza.

### 5.4 Rendimiento — todavía no medible

La escena de MAP01 genera más de 2.500 draw calls en el camino instrumentado, por lo que el coste por envío y los cambios de estado serán candidatos prioritarios. No se optimizará esa ruta ni se afirmarán 30 FPS hasta completar al menos un frame estable en hardware. Después se medirán tiempos CPU/GPU p50, p95 y p99 con el perfil de telemetría en una consola real.

## 6. Roadmap por fases

### Fase 0 — Higiene, baseline y publicación controlada (P0)

Objetivo: que cada cambio parta de un estado identificable y pueda revertirse.

- `HYG-001`: retirar builds, checkouts, logs duplicados, ROM ajeno y temporales del workspace.
- `HYG-002`: conservar un único CIA interno del último avance y evidencia mínima; marcarlo explícitamente como no estable.
- `HYG-003`: convertir el port actual en commits pequeños: build, plataforma, input, audio, memoria, render, diagnósticos y packaging.
- `HYG-004`: eliminar diferencias vacías de fin de archivo y revisar `git diff --check`.
- `HYG-005`: corregir el workflow para localizar `artifact_stem` desde `BUILD-MANIFEST.txt`, no reconstruir un nombre antiguo.
- `HYG-006`: hacer que CI falle si el árbol de release está dirty o contiene recursos prohibidos.
- `HYG-007`: añadir un test de aplicación limpia de cada parche sobre su revisión fijada.
- `HYG-008`: distinguir “trazable” de “reproducible”: ejecutar dos builds limpios y comparar hashes/ZIP metadata.
- `HYG-009`: obtener permiso escrito para redistribuir Legend of Doom o limitar releases a un instalador que lo descargue del origen autorizado.

Criterio de salida:

- checkout limpio y dividido en commits;
- CI produce un paquete y verifica exactamente ese paquete;
- pila de parches aplica desde cero;
- no hay ROM, saves, dumps ni binarios generados dentro del repo;
- política de redistribución resuelta.

### Fase 1 — Observabilidad que responda preguntas (P0)

Objetivo: sustituir pistas dispersas por una captura determinista por ejecución.

- `OBS-001`: añadir CSV circular por frame con tiempo total, simulación, BSP/visibilidad, generación de draw lists, 2D, NovaGL CPU y espera/presentación.
- `OBS-002`: registrar `C3D_GetProcessingTime()` y `C3D_GetDrawingTime()` después de que termine la cola; Citro3D ya mantiene contadores CPU/GPU.
- `OBS-003`: contar draws por primitive, triángulos de entrada/salida, cambios de textura, cambios TEV, matrices, blends y depth/stencil.
- `OBS-004`: contar vértices recortados por near plane y user clip, batches que entran en fast path y razones de fallback.
- `OBS-005`: registrar high-water de command list, vertex ring, index ring, staging, VRAM, linear heap y heap convencional.
- `OBS-006`: registrar uploads/evictions de texturas y bytes por frame, separados de cargas iniciales.
- `OBS-007`: crear un modo benchmark reproducible: MAP01, posición/yaw fijos, IA congelada opcional, 600 frames, warm-up de 120.
- `OBS-008`: crear un perfil `full-diagnostic` con audio y menú; el perfil actual `hardware-diagnostic` usa `-nosound` y salta directamente a MAP01.
- `OBS-009`: incluir build ID, perfil, mapa, posición, CVars forzadas y modelo de consola en cada salida.
- `OBS-010`: crear una herramienta host que resuma p50/p95/p99, 1 % low, draw calls y memoria sin abrir el juego.

Criterio de salida:

- una ejecución explica si el límite es game logic, render traversal, driver CPU, GPU, upload o espera;
- la telemetría tiene overhead medido y puede compilarse totalmente fuera de release;
- dos ejecuciones consecutivas del benchmark no difieren más del 5 % en hardware.

### Fase 2 — Fidelidad gráfica del puente NovaGL (P0)

Objetivo: imagen de referencia correcta antes de optimizar.

- `GFX-001`: integrar near clipping en ambos fast paths de vértice 24 B y cerrar la cuña del cielo.
- `GFX-002`: reactivar o reemplazar `SetDepthClamp` con una máquina de estado documentada y pruebas por pass.
- `GFX-003`: añadir escena de barras de color previa a recursos para aislar framebuffer/transfer/swizzle.
- `GFX-004`: construir una tabla de todos los uniformes escritos por `FGLRenderState::ApplyShader` y su implementación NovaGL: implementado, constante, emulado o no soportado.
- `GFX-005`: implementar desaturación, fixed/special colormap y texture modulate/add/blend requeridos por Legend of Doom.
- `GFX-006`: verificar que vertex color y object color no se multipliquen dos veces y que add color no altere alpha.
- `GFX-007`: verificar RGB/RGBA, paletted translation, alpha test y canal transparente con texturas sintéticas pequeñas.
- `GFX-008`: implementar sampler/wrap/clamp por unidad; `glBindSampler` y parámetros no pueden permanecer no-op si el juego depende de ellos.
- `GFX-009`: auditar los materiales de 1–4 capas. No recortar una cuarta capa globalmente sin demostrar que es neutral para cada material usado.
- `GFX-010`: validar blend modes Normal, Translucent, Add, Stencil y TranslucentStencil con sprites reales.
- `GFX-011`: validar sky portal, line portal, reflective flat, fog boundary, stencil mask y Early-Z.
- `GFX-012`: validar HUD, automap, menú, mensajes, arma y flash de daño/bonificación.
- `GFX-013`: guardar capturas golden de 12 escenas y compararlas por píxel con tolerancia documentada.

Criterio de salida:

- cero defectos visibles conocidos en la suite golden;
- cero estado no soportado usado silenciosamente;
- cada fallback tiene contador y documentación;
- cámara, cielo, color y HUD correctos durante diez minutos de movimiento.

### Fase 3 — Jugabilidad, input y persistencia (P0/P1)

Objetivo: completar el juego sin depender de teclado o consola de desarrollo.

- `PLAY-001`: probar cada botón físico y combinaciones simultáneas en New 3DS/New 2DS XL.
- `PLAY-002`: calibrar deadzone, curva y sensibilidad de Circle Pad/C-Stick.
- `PLAY-003`: impedir que el combo L+R+A dispare Use/cambio de arma durante el dump.
- `PLAY-004`: validar touch look, captura de GUI y cancelación al levantar el dedo.
- `PLAY-005`: comprobar navegación completa de menús sólo con controles 3DS.
- `PLAY-006`: crear checklist MAP01–MAP10 con entradas, salidas, secretos, NPC/tiendas y jefes.
- `PLAY-007`: probar todas las armas/ítems afectados por el parche que elimina offhand/two-handed.
- `PLAY-008`: guardar al inicio y final de cada mapa; comparar inventario, salud, flags globales y controladores tras cargar.
- `PLAY-009`: probar guardado interrumpido simulando espacio insuficiente y cierre controlado; nunca debe truncar el último save válido.
- `PLAY-010`: versionar el formato/config y manejar un INI viejo sin heredar CVars de escritorio incompatibles.

Criterio de salida:

- recorrido completo documentado;
- ningún control obligatorio inaccesible;
- 30 ciclos consecutivos de guardar/cargar sin diferencia de estado;
- tests de error conservan el guardado anterior.

### Fase 4 — Audio y ciclo de vida (P1)

Objetivo: audio continuo y salida limpia en consola real.

- `AUD-001`: validar inicio con `dspfirm.cdc` presente y mensaje útil cuando falta.
- `AUD-002`: medir tiempo de mezcla, decodificación MP3 y número real de fuentes activas.
- `AUD-003`: contar underruns, reinicios de stream y desincronización.
- `AUD-004`: validar loops, cambios overworld/dungeon, fanfarrias, pausa y muerte.
- `AUD-005`: probar mezcla de efectos simultáneos, estéreo/mono y clipping.
- `AUD-006`: verificar que el worker NDSP usa el core permitido sin competir con el hilo principal.
- `LIFE-001`: probar tapa/suspensión/reanudación durante mapa, música y guardado.
- `LIFE-002`: probar HOME y cierre desde menú, error fatal y salida normal.
- `LIFE-003`: ejecutar 20 ciclos de arranque/salida CIA y 3DSX.
- `LIFE-004`: ejecutar soak de dos horas y graficar heap/linear/VRAM/handles.

Criterio de salida:

- cero underruns audibles en sesión normal;
- cero crash/hang en la matriz de ciclo de vida;
- memoria estable después del warm-up;
- recursos RomFS/SD permanecen válidos hasta los destructores finales.

### Fase 5 — Rendimiento medido (P1)

Objetivo: llevar el baseline correcto a 60 FPS o demostrar exactamente qué arquitectura lo impide.

Orden obligatorio:

1. medir;
2. elegir el mayor coste;
3. cambiar una sola familia de coste;
4. repetir benchmark y suite gráfica;
5. conservar únicamente mejoras estadísticamente claras.

Tareas:

- `PERF-001`: baseline release en New 3DS real con CPU/GPU p50/p95/p99.
- `PERF-002`: desglosar los >2.500 draws: sky, flats, walls, sprites, weapon y HUD.
- `PERF-003`: reducir llamadas redundantes de matrices/uniformes/TEV sin reintroducir fugas entre variantes.
- `PERF-004`: deduplicar cambios de textura, sampler, blend, depth y stencil conservando orden semántico.
- `PERF-005`: agrupar geometría estática compatible por material y pass; medir coste de construir versus reutilizar.
- `PERF-006`: mantener zero-copy sólo donde el contador prueba que no requiere clipping.
- `PERF-007`: preconvertir texturas a formato/tile PICA para eliminar swizzle y resize en carga.
- `PERF-008`: eliminar uploads por frame; canvas dinámicos deben usar regiones y buffers persistentes.
- `PERF-009`: medir command-list splits. Dividir evita overflow, pero demasiados splits serializan la GPU.
- `PERF-010`: añadir una fence real y probar dos frames en vuelo. No reactivar doble buffer sin fence: ya produjo VBO reciclado y geometría plegada.
- `PERF-011`: mover sólo trabajos independientes al core adicional mediante la API de threads/libctru y permisos APT correctos.
- `PERF-012`: mantener render/GSP en el hilo propietario; candidatos auxiliares: audio, decode/preparación de recursos y jobs de escena demostrablemente seguros.
- `PERF-013`: medir LTO, `-O3`, PGO o flags ARM únicamente después de estabilizar el perfil; revertir si crece demasiado el ELF o cambia precisión.
- `PERF-014`: medir coste de ZScript/controladores por tic en MAP01 con IA activa.
- `PERF-015`: establecer pacing 60 Hz y telemetría de missed VBlank.

Puerta de arquitectura:

- si, tras batching/estado/uploads/fence, el coste de NovaGL CPU sigue por encima de 6–8 ms p95 o el frame total no puede bajar de 20 ms, iniciar backend PICA200 directo;
- el backend directo debe implementar primero el subconjunto usado: posición/UV/color, matrices, textura base, alpha/blend, depth/stencil, fog/colormap, clip y sky;
- mantener NovaGL como referencia hasta que las 12 escenas golden coincidan.

### Fase 6 — Recursos específicos de 3DS (P1/P2)

Objetivo: reducir carga, memoria y trabajo runtime sin alterar el juego.

- `RES-001`: generar manifest de cada recurso: path, tipo, dimensiones, bytes, uso y mapa de referencia.
- `RES-002`: detectar recursos nunca referenciados por ZScript/ACS/MAPINFO/texturas/mapas; retirar sólo después de un recorrido completo instrumentado.
- `RES-003`: sustituir el clamp global 256 por reglas: sprites nativos, sky tiled, mapas/HUD preescalados y texturas de mundo con límite propio.
- `RES-004`: preparar `SKYWW`/`SKYWW2` en tiles que preserven el periodo horizontal en lugar de comprimir 1024 a 256.
- `RES-005`: preescalar imágenes de mapa de 1024–2176 px a variantes 3DS, conservando originales fuera del runtime.
- `RES-006`: evaluar 32 kHz/mono para efectos y música donde ABX/escucha no revele degradación; medir CPU y tamaño antes de adoptar.
- `RES-007`: verificar que brightmaps/lights/widescreen desactivados no se empaqueten si el juego nunca los usa.
- `RES-008`: conservar credits/licencias por componente y el `CREDITS` original del mod.
- `RES-009`: validar que PK3 no incluya `.git`, `.DS_Store`, objetos de build ni fuentes ACS innecesarias.
- `RES-010`: analizar MAP01 por subsector, draw count y materiales; optimizar el mapa sólo si no basta la ruta de engine/backend.

Criterio de salida:

- manifest sin archivos desconocidos;
- cero resize/swizzle costoso durante un frame normal;
- pico VRAM/linear con margen definido;
- capturas y audio pasan comparación antes/después.

### Fase 7 — Matriz de QA (P0 antes de release)

| Dimensión | Casos mínimos |
| --- | --- |
| Formato | CIA con RomFS; 3DSX con datos SD |
| Hardware | New 3DS; New 3DS XL; New 2DS XL si hay acceso |
| Audio | DSP presente; DSP ausente; música on/off |
| Arranque | Frío; segundo arranque; INI nuevo; INI antiguo |
| Juego | MAP01–MAP10; muerte; pausa; menú; créditos/final |
| Render | 12 escenas golden; cuatro orientaciones del sky; interiores/exteriores |
| Guardado | Nuevo; sobrescribir; cargar; tres slots; SD casi llena |
| Ciclo de vida | Tapa; HOME; cierre normal; error controlado |
| Duración | Smoke 10 min; recorrido completo; soak 2 h |
| Rendimiento | Benchmark fijo; ruta normal; combate cargado; HUD/automap |

Cada caso debe guardar:

- build ID y SHA-256 del CIA/3DSX;
- modelo/firmware de consola;
- configuración y perfil;
- resultado pass/fail;
- captura/log asociado;
- FPS y p95 CPU/GPU cuando aplique;
- issue enlazado para cada fallo.

### Fase 8 — Release (P0)

- `REL-001`: congelar candidato; cualquier cambio crea un candidato nuevo.
- `REL-002`: build desde checkout limpio en CI fijada.
- `REL-003`: verificar hashes, contenido RomFS/ZIP, licencias y ausencia de datos privados/prohibidos.
- `REL-004`: instalar exactamente el CIA publicado y ejecutar smoke en hardware.
- `REL-005`: extraer exactamente el ZIP publicado y ejecutar la 3DSX.
- `REL-006`: simbolizar un crash sintético con el debug ZIP correspondiente.
- `REL-007`: publicar limitaciones reales; no usar “60 FPS” sin datos adjuntos.
- `REL-008`: conservar candidato anterior y procedimiento de rollback.

Criterio de salida:

- todas las puertas de definición de terminado pasan;
- permiso de contenido resuelto;
- CI y hardware validan el mismo hash;
- release notes describen métricas y defectos conocidos.

## 7. Orden inmediato de ejecución

Ésta es la cola concreta recomendada; no comenzar el siguiente bloque si el anterior falla:

1. Limpiar workspace y preservar sólo el CIA/evidencia actuales.
2. Crear commits lógicos del port existente y una etiqueta interna de baseline.
3. Corregir la CI de nombres de artefactos y añadir patch-apply tests.
4. Compilar desde cero el perfil `hardware-diagnostic` y luego `release`.
5. Añadir contadores/timing CPU-GPU de Fase 1.
6. Corregir el fast path 24 B del sky fan y validar cuatro capturas.
7. Definir `GL_DEPTH_CLAMP` por pass y ejecutar regresión de portales.
8. Ejecutar patrón de color antes/después del display transfer.
9. Completar tabla de uniformes/TEV y corregir la dominante de color demostrada.
10. Cerrar suite gráfica de 12 escenas.
11. Validar movimiento, controles, menú, guardado y carga en MAP01.
12. Validar audio real y ciclo de vida.
13. Ejecutar primer benchmark real y ordenar costes.
14. Optimizar draws/estado/uploads; después probar fence y dos frames en vuelo.
15. Aplicar la puerta NovaGL versus backend PICA200 directo.
16. Completar MAP01–MAP10 y la matriz de QA.
17. Generar y probar un candidato de release limpio.

## 8. Riesgos principales

| Riesgo | Probabilidad/impacto | Mitigación |
| --- | --- | --- |
| NovaGL no cubre el modelo de shaders de GZDoom | Alta/Alta | Contrato explícito de estados; backend directo si el coste/cobertura no cierra |
| Arreglo visual reduce rendimiento | Alta/Media | Counters de fast/fallback y benchmark por cada cambio |
| Optimización reintroduce corrupción entre frames | Alta/Alta | Fence real, suite de movimiento y soak antes de doble buffer |
| 2.500+ draws impiden 60 FPS | Alta/Alta | Breakdown, batching y puerta temprana de arquitectura |
| Clamp 256 degrada recursos grandes | Alta/Media | Variantes offline por clase y sky tiled |
| Cambios offhand rompen armas/escudo | Media/Alta | Checklist de cada arma e inventario |
| Audio roba CPU o falla al suspender | Media/Media | Timing, underrun counter y matriz lifecycle |
| Guardado SD se corrompe | Media/Alta | Escritura atómica y pruebas de fallo/espacio |
| Build dirty no es reproducible | Alta/Alta | Commits, CI limpia, hashes y dos builds comparados |
| Mod sin licencia bloquea distribución | Alta/Alta | Permiso escrito o instalador sin redistribución |

## 9. Formato obligatorio de cada cambio

Cada issue/commit técnico debe responder:

1. ¿Qué síntoma exacto corrige?
2. ¿Qué evidencia identifica la causa?
3. ¿Qué archivo/estado es propietario del problema?
4. ¿Qué cambio mínimo se hizo?
5. ¿Qué medición antes/después existe?
6. ¿Qué suite de regresión se ejecutó?
7. ¿Cómo se revierte?
8. ¿Qué queda deliberadamente fuera?

No se acepta como cierre “se ve mejor”, “parece más rápido” o “no falló una vez”.

## 10. Fuentes técnicas primarias

- [GZDoom 4.7.1: estado GLES](https://github.com/ZDoom/gzdoom/blob/g4.7.1/src/common/rendering/gles/gles_renderstate.cpp)
- [GZDoom 4.7.1: renderizado del cielo](https://github.com/ZDoom/gzdoom/blob/g4.7.1/src/rendering/hwrenderer/scene/hw_sky.cpp)
- [NovaGL en la revisión fijada](https://github.com/efimandreev0/NovaGL/tree/9cabf853fb57a1037bea55dbec81eea073b5ee6c)
- [Citro3D: cola, sincronización y tiempos CPU/GPU](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c)
- [Citro3D: wrapper PICA200](https://github.com/devkitPro/citro3d)
- [libctru: framebuffer y formatos de pantalla](https://github.com/devkitPro/libctru/blob/master/libctru/source/gfx.c)
- [libctru: API de threads](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/thread.h)
- [libctru: afinidad y creación de threads](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/svc.h)
