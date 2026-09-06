# v0.34: banner y acciones de guardado

El banner conserva su encuadre con las dimensiones XYZ al 85% de v0.33. Se escalan únicamente las posiciones del GLB original respecto a (0, 1, 0), sin cambiar normales, UV, índices ni textura. El CGFX se vuelve a generar con el mismo conversor pycgfx fijado en Banner_3D.

En LOAD GAME, tocar un guardado o pulsar A abre Load / Delete en la pantalla inferior y actualiza su vista previa superior. Delete abre Are you sure? con Yes / No y No seleccionado inicialmente. La cruceta cambia la opción, A confirma y B vuelve. El borrado comprueba que el archivo siga siendo el seleccionado; No, B y eventos táctiles de una pantalla anterior no pueden borrarlo. Si falla la eliminación, se conserva el guardado en la lista y aparece Could not delete save.

El menú SAVE GAME conserva la creación y sobrescritura de partidas.

## Reproducción del banner

Desde Source:

```sh
python3 platform/3ds/tools/scale-banner-glb.py ../Banner_3D/Legend_of_Doom_banner_512.glb ../Banner_3D/Legend_of_Doom_banner_85.glb
../Banner_3D/.venv/bin/python ../Banner_3D/tools/pycgfx/main.py ../Banner_3D/Legend_of_Doom_banner_85.glb platform/3ds/assets/banner.cgfx
```

La validación cubre las transiciones de carga y borrado, cancelación, cambio de archivo, último guardado, fallo de borrado y escalado geométrico. Los scripts se compilan con el motor nativo y los recursos actualizados, sin entrar en una partida. La comprobación en 3DS física queda pendiente del usuario; no se utiliza Azahar ni se publica el CIA en GitHub.
