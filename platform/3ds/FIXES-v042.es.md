# v0.42: cabeza centrada en el HUD inferior

Se revisan los dumps 001-quick-20260905-231704 y 002-quick-20260905-231734. La posición anterior reservaba seis píxeles bajo los corazones, mientras el espacio hasta las rupias era mayor.

La cabeza de 46×55 se centra verticalmente entre el final de la última fila de corazones y el inicio del contador de rupias. La diferencia entre el margen superior e inferior es como máximo un píxel por el redondeo. Los contadores conservan exactamente sus posiciones de v0.41. Para 3, 6, 9, 12 y 16 corazones, la coordenada Y de la cabeza pasa a 77, 87, 87, 87 y 98, respectivamente.

Las 137 pruebas pasan. Se ejecuta la distribución C++ real para capacidades de 1 a 20 y se comprueban el centrado vertical, los márgenes, la ausencia de solapamientos y la posición invariable de los contadores. Se revisa también una vista renderizada del bloque de corazones y cabeza. Se compila el CIA y se verifica su banner y audio HOME. No se prueba en Azahar ni se publica en GitHub.

Los cinco saves de v0.41 siguen siendo válidos. El banner reducido un 8 %, el sonido, la niebla y la pausa del mapa permanecen sin cambios. La comprobación visual final corresponde a la consola del usuario.
