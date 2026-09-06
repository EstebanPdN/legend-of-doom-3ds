# v0.35: inicio y revisión de los once menús

- La pantalla inferior del título muestra PRESS START TO BEGIN con NewSmallFont, alternando visible/oculto cada 600 ms. START y tocar el texto abren el menú.
- MainMenu y LegendPauseMenu centran la columna de texto independientemente del ancho del selector Link; se mantiene su diseño y tipografía.
- Carga/borrado y confirmaciones usan el mismo diálogo fijo: encabezado en mayúsculas, dos opciones horizontales y selector a la izquierda. CHOOSE AN ACTION ofrece LOAD / DELETE; ARE YOU SURE? ofrece YES / NO. No se recalcula su posición al cambiar el selector.
- DISPLAY, CONTROLLER, DEVELOPER y VOLUME conservan la escala de OPTIONS y márgenes de texto de aproximadamente 27 píxeles. Las barras compactas y sus zonas táctiles comparten celdas de 12 píxeles. Se conserva el contenido negro de los controles, antes perdido durante la extracción por diferencia contra el fondo.
- La miniatura de guardado se renderiza en BGRA con alfa inicialmente transparente, habilita la corrección del cielo únicamente para esa captura y escribe un PNG RGB. Las capturas antiguas se sustituyen al volver a guardar; las texturas de cámaras mantienen su comportamiento.
- YES al salir elimina la espera del sonido de despedida y omite el guardado final de configuración. Se conserva el cierre de los recursos del motor y no se crea una partida guardada.
- Se incorpora sin modificar el CGFX de Banner_3D/versión final, de 436344 bytes. No se vuelve a aplicar la reducción del banner anterior.

## Verificación

Las 123 pruebas locales incluyen márgenes y barras, independencia del centrado respecto al cursor y la ruta BGRA de captura. Se compilan los scripts con el motor nativo usando los recursos actualizados y se construye el CIA para New Nintendo 3DS. Se verifica el banner incluido en ExeFS y los SHA-256 de los artefactos. Los cambios visuales y el cierre requieren la comprobación final del usuario en su consola; no se utiliza Azahar ni se publica el CIA en GitHub.
