# Revisión v0.31 — 5 de septiembre de 2026

## Alcance y límites

Se inventarió el repositorio completo (5.633 archivos seguidos antes de esta
revisión) y se revisaron en detalle las rutas modificadas del port: arranque,
memoria, hilos, presentación, interfaz, niebla, audio, scripts del mod y
empaquetado. Se contrastaron los parches con sus dependencias fijadas y los
dumps físicos disponibles. Esto no equivale a inspeccionar manualmente cada
línea de GZDoom y sus bibliotecas ni a jugar todos los mapas.

El árbol inicial contenía 77 archivos modificados y numerosos archivos nuevos
sin publicar. El commit `58f4821` conserva la implementación local v0.30 antes
de esta limpieza. Los cambios de v0.31 se pueden comparar con ese punto;
no deben atribuirse a esta revisión las mejoras anteriores.

## Cambios aplicados

| Problema | Corrección | Efecto comprobable |
|---|---|---|
| Tres esperas redundantes después de retirar los workers del canvas | Una espera antes de componer HUD/menús; se mantienen las esperas de las colas posteriores | Menos llamadas de sincronización por frame; no cambia el reparto de píxeles |
| `r_multithreaded` admite valores negativos o arbitrariamente grandes | Limitar trabajadores efectivos a 1–2 en 3DS | Evita convertir un negativo en una reserva enorme y crear trabajadores adicionales en CPU2 |
| Filtrado bilineal lee el padding de la textura de 512×256 | Replicar una columna y una fila del borde activo, y limpiar su rango de caché | Evita mezclar negro o contenido de una resolución anterior en el borde derecho/inferior |
| El upload ignoraba un error de limpieza de caché | Volver a SDL si falla el flush de staging | No enviar datos potencialmente incoherentes a GX |
| Cada muestra del terreno recorría hasta 24 entradas de caché | Acceso directo a la última textura usada | Una comparación para muestras consecutivas de la misma textura, sin reservas nuevas |
| Coordenada Y del minimapa calculada dentro del bucle X | Calcularla una vez por fila | Conserva las mismas coordenadas y geometría |
| Serpiente: `i-1 >= 0` con índice `uint` | Comprobar `i > 0` antes de acceder al cuerpo anterior | Evita underflow al recorrer el primer cuerpo |
| Serpiente: acceso al primer cuerpo sin comprobar tamaño | Comprobar lista no vacía | Evita acceso fuera de rango si no quedan cuerpos |
| Ganon: búsqueda aleatoria ilimitada | Hasta 100 candidatos; conservar posición al agotarlos | Acota el trabajo de un teletransporte imposible |
| Comentarios con historial y explicaciones repetidas | Acortar bloques del port y del build | Mantiene invariantes y licencias; el análisis queda en documentos |
| Pruebas dependientes de comentarios y versión literal | Comprobar orden de operaciones y formato de versión | La limpieza de prosa deja de romper esos contratos |
| QR antiguos entraban en el hash de fuentes no seguidas | Ignorar imágenes QR generadas en la raíz | El identificador deja de depender de esos archivos locales |

El relleno bilineal agrega 2.048 bytes al rango limpiado por frame. La
transferencia GX sigue siendo 512×256, de modo que no aumenta su tamaño.
No se cambió el reloj de simulación, la resolución por defecto ni el contenido
de las partidas. Los dos cambios de monstruos afectan rutas de error; no
constituyen una optimización general de la IA.

## Rendimiento: qué sabemos

Los nueve dumps recientes `001`–`009` del 1 de septiembre corresponden a
**v0.29**, build `385f5c417a00`, no a v0.30 ni a v0.31. Registran escalado
bilineal, resolución seleccionable y límite de FPS cero. El dump 008 captura
MAP04 y un menú activo. No se debe usar ese frame como medición de juego.
Estos dumps indican `frame_telemetry=unavailable`; no permiten obtener p95,
p99, 1% low ni separar tiempo de escena, UI, audio y presentación.

Los informes históricos registran unos 6,2 FPS a 320×200 en una versión antigua
con presentación SDL. No son una medida de la versión actual. Los bloqueos
PICA de versiones anteriores justifican mantener NovaGL para geometría como
experimental. El perfil entregado `hardware-hybrid` usa geometría por CPU y
PICA para escalar una textura; `hardware-safe` sigue siendo recuperación.

### Costes identificados en el código

1. **Minimapa:** 204×156 muestreado en bloques 2×2 produce 7.956 consultas a
   `PointInRenderSubsector` por refresco. A continuación se comprueba pertenencia
   al polígono, visibilidad, textura e iluminación. El mapa superior 320×192
   hace 15.360 consultas por frame mientras está abierto. Es un candidato fuerte
   a picos de CPU; no hay tiempos por fase que cuantifiquen su peso.
2. **Rasterización:** 320×192 son 61.440 píxeles; 400×240, 96.000. Pasar de 80%
   a 100% aumenta un 56,25% los píxeles, antes de contar overdraw. No implica
   que el frame completo sea un 56,25% más lento.
3. **Presentación:** staging de 512×256 y una transferencia/quad por frame.
   El uso de PICA no elimina el coste CPU de paredes, planos, sprites y UI.
   La limpieza de vértices Citro2D conserva un margen de hasta 64 KiB; reducirlo
   requiere demostrar el tamaño real del buffer en la biblioteca fijada.
4. **Escena/BSP:** recorrido con un solo propietario. Los anteriores recortes
   de subárboles causaron agujeros y oclusión incorrecta. No se reintroducen.
   Las reglas especiales de niebla pertenecen a MAP01; otras mazmorras deben
   medirse por separado.
5. **Spawner del mod:** comprueba spawners cada 20 tics y distancia de monstruos
   cada 100 tics. Puede concentrar trabajo en determinados tics. Cambiar su
   cadencia o repartirlo altera cuándo aparecen enemigos; requiere pruebas de
   lógica y partidas, no sólo un benchmark.
6. **Audio:** el thread de streaming se despierta cada 20 ms, y OpenAL/NDSP
   mantiene su propietario y buffers. No se reduce el buffering para ahorrar
   RAM: las versiones anteriores ya tuvieron problemas de silencio/underrun.
7. **Memoria:** el límite convencional y la reserva temprana de vértices
   responden a fallos físicos documentados. Eliminar esas defensas por parecer
   redundantes sería incorrecto. La caché de texturas del minimapa contiene
   hasta 24 imágenes; conviene medir su tamaño real antes de ampliarla.
8. **Diagnóstico:** `diagnostics_3ds.cpp` mezcla dumps, HUD, automapa, fuentes,
   menús y ciclo de vida. Separarlo en módulos sería útil, pero una división
   mecánica de miles de líneas sin pruebas funcionales de cada pantalla
   escondería riesgos en este mismo cambio.

## Próxima optimización con mayor potencial

Instrumentar escena, presentación y minimapa por separado mediante ticks y un
buffer RAM, sin escribir cada frame a SD. Capturar 120 frames de calentamiento
y 600 válidos por escena: exterior con pared cercana, exterior abierto,
mazmorra, combate, mapa superior y menús; repetir tres veces en la misma New 3DS.
Comparar v0.30 y v0.31 en idéntica posición, resolución y ajustes de audio.

Después, priorizar una caché espacial del terreno del minimapa con invalidación
por cambio de mapa, posición, descubrimiento, suelo animado y sector móvil.
Omitir esas invalidaciones puede revelar zonas no exploradas o dibujar puertas
con un estado antiguo. No se implementa una caché visual incorrecta para
publicar una cifra de FPS más atractiva.

No habilitar más hilos BSP, bajar distancia globalmente ni activar `-ffast-math`
a ciegas. ARM11 no ofrece NEON: no son trasladables sin más las optimizaciones
SIMD de máquinas ARM modernas. Mantener revisión específica de SSE/fallbacks,
clipping y sincronización antes de cambiar opciones del compilador.

## Validación reproducible

```sh
python3 -m unittest discover -s platform/3ds/tests -v
c++ -std=c++17 -fsanitize=address,undefined -g -Isrc \
  platform/3ds/tests/test_present_pixels.cpp -o /tmp/lod-present-test
/tmp/lod-present-test
./platform/3ds/test-patches.sh
./platform/3ds/build.sh hardware-hybrid
```

El test C++ ejecuta la copia real con AddressSanitizer y UndefinedBehaviorSanitizer:
400×240 → 200×120 → 320×192 → 1×1, con filas compactas y padding, comprobando
cada píxel y ambos bordes. Los 112 tests Python iniciales pasaban; muchos son
contratos estáticos, no ejecución de juego. Aplicar un parche correctamente
no demuestra que sus scripts se ejecuten sin errores en todos los encuentros.
La compilación ARM y el CIA tampoco sustituyen pruebas físicas de arranque,
HOME, sonido, guardado/carga, combate y cambios de resolución.

## Fuentes y reproducibilidad

- [Mod usado por el port](https://github.com/emawind84/legend-of-doom/tree/d7c66cf79fa00b112c17ea443fa63121120ff45b): scripts revisados en la copia local fijada.
- [Legend of Doom, DeTwelve](https://www.moddb.com/mods/legend-of-doom): contexto del juego y conversión completa.
- [Citro3D](https://github.com/devkitPro/citro3d/blob/master/source/texture.c): referencia de texturas; la instalación local determina el ABI de la compilación.
- [libctru](https://github.com/devkitPro/libctru/blob/master/libctru/source/thread.c): creación y cierre de threads; se revisó además la API instalada.
- `dependencies.sh`, manifiestos y hashes: identidad concreta de cada dependencia y artefacto.

No se atribuye ninguna ganancia porcentual de FPS a esta revisión sin capturas
nuevas de hardware. El objetivo verificable es reducir redundancia, corregir
rutas defectuosas y producir una candidata trazable.
