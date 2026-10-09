# Legend of Doom 3DS — mantenimiento

Documentación actualizada el 9 de octubre de 2026.

## Estado publicado

La [v1.0](https://github.com/EstebanPdN/legend-of-doom-3ds/releases/tag/v1.0) es la versión estable publicada. Incluye CIA, 3DSX, paquete SD, símbolos de depuración, manifiesto y checksums.

El objetivo de hardware es New Nintendo 3DS, New Nintendo 3DS XL y New Nintendo 2DS XL. El paquete publicado utiliza `hardware-hybrid`: SoftPoly en CPU0/CPU2 y PICA200 para presentar el frame terminado. No utiliza NovaGL para dibujar el mundo.

El [README](README.md) contiene las descargas, instalación, controles y soporte. La [guía técnica](platform/3ds/README.md) explica los perfiles de compilación. El perfil predeterminado y el de CI siguen siendo `hardware-safe`; sus paquetes son distintos del perfil de la release.

## Seguimiento

- Los errores y propuestas actuales se registran en [GitHub Issues](https://github.com/EstebanPdN/legend-of-doom-3ds/issues).
- Los cambios de cada versión publicada se consultan en [Releases](https://github.com/EstebanPdN/legend-of-doom-3ds/releases).
- Los problemas del actualizador se documentan en [UPDATER.md](platform/3ds/UPDATER.md).
- Las rutas NovaGL, `hardware-candidate` y `hardware-diagnostic` se conservan para investigación del renderer; no sustituyen el perfil de v1.0.

Las mediciones de rendimiento y compatibilidad deben identificar la versión, el perfil, la consola y los ajustes. Las pruebas de host y emulador se registran por separado de las pruebas físicas; el estado de una release no sustituye las mediciones de cada compilación y escenario.

## Historial técnico

La auditoría del 27 de agosto describía bloqueos y objetivos anteriores a v1.0. Se conserva como [archivo histórico](docs/history/ROADMAP-2026-08-27.es.md), junto con las [notas anteriores de compilación](platform/3ds/history/README-before-v1.0.md).

Las revisiones y notas `CODE-REVIEW`, `FIXES`, `BOOT-FIX` y `BANNER` de `platform/3ds/` pertenecen a las versiones indicadas en sus nombres. Sirven como evidencia histórica y no describen por sí solas el estado de la versión publicada.
