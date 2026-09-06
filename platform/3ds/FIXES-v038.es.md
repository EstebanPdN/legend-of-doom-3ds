# v0.38: menús, distancia y sonido HOME

## Dumps revisados

Se revisaron las dos pantallas, manifiestos y estado del motor de los siete dumps nuevos de Dumps/-dumps: 001, 002, 003, 004, 005-full, 006 y 007, tomados el 5 de septiembre entre 21:20 y 21:25. Los cuatro primeros muestran el fondo negro de Options, Volume, Controls y Controller. El quinto muestra el menú principal. Los dos últimos muestran la cueva, el diálogo del anciano y el cofre; no hay un crash registrado en estas capturas.

La memoria completa de 005 pertenece exactamente a v0.37, build 4b36d0ec0b42. Usando el mapa de regiones y los símbolos del ELF correspondiente:

- LastFrameMilliseconds, dirección 0x00aa2138: 288.395 ms.
- LastCount, dirección 0x00aa2150: 3 FPS.
- GameTicRate, dirección 0x00971a18: 35.
- Las nueve entradas bDown de MenuButtons estaban liberadas.
- Heap libre: 16,901,552 bytes. Memoria lineal libre: 15,675,392 bytes.
- Tiempo GPU informado: 3.145 ms.

Esto muestra fotogramas lentos en el menú y no evidencia agotamiento de memoria o una tecla retenida. No es un perfil por función: no permite atribuir todos los 288 ms a una instrucción concreta. La revisión encuentra una búsqueda dinámica de clase en cada comprobación de píxel (más de 96.000 consultas por fotograma) y la decodificación repetida del fondo estático. Se calcula el tipo de menú una vez y se reutiliza el fondo ya decodificado. La respuesta inicial de la cruceta ya se despacha directamente; se conserva la repetición al mantenerla pulsada. Los siguientes dumps incluyen el tiempo de fotograma y FPS sin necesitar volcar toda la RAM.

## Cambios

- PRESS START TO BEGIN centrado en ambos ejes de la pantalla inferior, parpadeando en título y lore. Área táctil ajustada a su nueva posición. El lore conserva su fondo inferior negro.
- El compositor omite los píxeles vacíos de Options y submenús, conservando la limpieza del lienzo para evitar restos en los deslizadores.
- Controls sin INPUT/ACTION ni título; tabla ampliada hasta los márgenes disponibles, con todas las filas visibles.
- Lista principal de Options desplazada 8 píxeles a la izquierda. Cam Sensitivity, aproximadamente 4 píxeles a la derecha. Las demás columnas se conservan.
- Render: 0.50 a 1.00 en once posiciones, pasos de 0.05 y valor visible. Resolución real de 200×120 a 400×240; los menús siguen a resolución nativa. Los valores archivados anteriores siguen funcionando.
- Distance: LOW 1536, NORMAL 2048, HIGH 2560 unidades. La niebla comienza al 75 % del límite y termina en él. El límite de paredes y sprites usa la misma distancia, incluso con configuraciones antiguas. Se mantienen la protección de cuevas y la integridad del recorrido BSP.
- splash.mp3 completo convertido a PCM16 estéreo, 32 kHz, para el audio del banner HOME. Geometría y posición del logo v0.37 intactas.

## Verificación

Pruebas de regresión y ejecución de los callbacks reales de escala/distancia extraídos del código C++: once resoluciones, valores anteriores, límites y niebla normal idéntica. Compilación ARM, validación de scripts con el compilador nativo en modo -norun y verificación del contenido del CIA. La fluidez final y el audio HOME requieren confirmación en la consola. Sin Azahar ni publicación de CIA/releases en GitHub.
