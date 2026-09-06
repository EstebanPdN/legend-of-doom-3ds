# v0.43 — 3D estereoscópico experimental

- Imagen inferior del título ampliada un 5%, centrada y calculada al cargar su caché. Los controles conservan su posición.
- LOAD / DELETE y YES / NO centrados como grupo según la anchura real de sus textos. El cursor queda a la izquierda de la opción.
- Display incorpora STEREOSCOPIC 3D. OFF impide activar el efecto aunque el deslizador físico esté atascado. Sigue el mismo sistema de configuración que las demás opciones.
- Dos cámaras paralelas dibujan el mundo desde el mismo estado de simulación. El deslizador regula la intensidad; el cero, los menús y el mapa mantienen la presentación 2D.
- Profundidad dirigida hacia dentro: separación adaptada a paredes, suelo, techo y actores próximos, con disparidad lejana limitada a ocho píxeles de LCD. Armas e interfaz permanecen en el plano de pantalla. Los destellos que alteran más de media imagen se muestran iguales en ambos ojos.
- Recursos del ojo derecho reservados al necesitarlos; si falta memoria, se conserva el modo 2D. Ambos ojos se envían en la misma presentación, esperando a los trabajadores antes de reutilizar la memoria.
- Los dumps con 3D activo incluyen top-right-screen.bmp y el framebuffer derecho, además de intensidad y separación de cámaras.

## Validación

141 pruebas automatizadas superadas: incluye búferes a distintas resoluciones, interpolación, bordes y memoria de relleno, composición de interfaz, retorno a 2D, límites de disparidad, zoom simétrico y centrado de opciones.

Prueba adicional con una compilación temporal del motor nativo de escritorio usando el nuevo código de cámaras y MAP01: dos imágenes 400 × 240 distintas, con diferencias de perspectiva incluso tras compensar el desplazamiento horizontal. Esta prueba no valida el controlador gráfico de la consola ni sustituye la comprobación física. No se ha usado Azahar.

El 3D duplica el dibujo del mundo por software y puede reducir los FPS. Es experimental: comprobar en New 3DS, especialmente cerca de paredes, enemigos y durante efectos de daño. La adaptación es conservadora, pero geometrías especiales necesitan validación visual. Con OFF se conserva el recorrido de renderizado monoscópico.

Banner 3D y audio HOME sin cambios. Partidas anteriores compatibles. CIA local; sin publicación en GitHub.

## Referencias

Documentación proporcionada en TIPS/stereoscopy web Docs: cámaras paralelas, sincronización de ojos y disparidad acotada. API de pantalla contrastada con [libctru gfx.c](https://github.com/devkitPro/libctru/blob/master/libctru/source/gfx.c).
