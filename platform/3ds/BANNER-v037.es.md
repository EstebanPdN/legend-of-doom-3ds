# v0.37: tamaño y posición del logo HOME

Logo final reducido al 85 % de su tamaño en v0.36. Desplazamiento a la izquierda del 10 % de su ancho reducido (2.084428 unidades), aproximando el eje visual al espacio entre las dos O. Se mantienen las texturas, materiales y la corrección de índices UTF-8 de v0.36.

Reproducción:
```sh
python3 platform/3ds/tools/scale-banner-glb.py '../Banner_3D/versión final/Legend_of_Doom_banner_512.glb' /tmp/lod-v037-banner.glb --scale 0.85 --shift-x -0.10
../Banner_3D/.venv/bin/python ../Banner_3D/tools/pycgfx/main.py /tmp/lod-v037-banner.glb /tmp/lod-v037-banner.cgfx
python3 platform/3ds/tools/validate-banner.py /tmp/lod-v037-banner.cgfx --repair platform/3ds/assets/banner.cgfx
```

Sin pruebas en Azahar. La posición final se confirma en la consola.
