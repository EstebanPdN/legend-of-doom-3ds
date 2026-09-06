# v0.33: pausa, guardados y banner

- START cierra los menús durante una partida y reanuda el juego. La captura de teclas para reasignar controles conserva su comportamiento.
- La fuente nativa incorpora minúsculas y `<`. Los nombres como `Link` conservan todas sus letras en ambas pantallas. La entrada de guardado nueva muestra exactamente `<New Save>`.
- Antes del teclado, el presentador híbrido espera a la cola GPU. La captura lee el framebuffer mostrado mediante GSP e invalida su caché antes de copiarlo a ambos buffers superiores. Se elimina la selección por brillo de buffers sin sincronizar. La espera usa la misma API privada de la revisión fijada de Citro3D que ya utiliza NovaGL.
- La limpieza del área de pestañas termina antes de la columna del inventario: ya no borra las últimas filas del icono de bombas.
- El banner HOME se crea con `Banner_3D/Legend_of_Doom_banner.cgfx`, copiado sin cambios a `platform/3ds/assets/banner.cgfx`, y el audio existente.

## Señora del dump 004

En MAP01, la posición del jugador es aproximadamente (-11777, -4616). La señora de esa sala está en (-11776, -4416), tipo 17020 (`ZeldaOldLady`); la entrada es el destino 7, en (-11776, -5232). `acs/lodoverworld.acs`, script `StairTravel`, cambia el destino 12 por el 7 cuando falta `ZeldaLetter`. El propio mod define la sala 7 como vacía y la 12 como la sala de medicinas con disparador de diálogo. Su silencio es intencional hasta conseguir la carta; no se modifica esa mecánica.

## Verificación

Las 118 pruebas locales pasan, incluidas dos regresiones nuevas: cobertura visible de los nombres y ejecución del código de captura con una GPU simulada que requiere sincronización e invalidación. Se valida el CGFX y su empaquetado CBMD con bannertool. La compilación del CIA incluye la validación de parches y del compilador nativo de scripts. La comprobación final de START, teclado, HUD y banner en una 3DS física queda pendiente del usuario. No se utiliza Azahar ni se publica una release o un CIA en GitHub.
