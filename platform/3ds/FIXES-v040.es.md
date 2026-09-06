# v0.40: efectos de sonido y niebla por distancia

La compilación 3DS de ZMusic tenía MiniMP3, pero no libsndfile. Los WAV y OGG del juego no encontraban decodificador y SoundEngine::LoadSound sustituía el recurso fallido por sfx_empty. Los dumps tenían el sonido habilitado y el volumen de efectos a 1.0: no era un problema de volumen.

Se añade lectura directa de WAV PCM mono/estéreo de 8/16 bits y decodificación OGG Vorbis con stb_vorbis v1.22, fijado a una revisión y con su licencia incluida. Se mantienen los originales y sus puntos de repetición. Los MP3 conservan su lector. El lector WAV valida cabeceras, límites de chunks, formato y alineación; los buffers Vorbis se liberan después de subir el PCM a OpenAL. También se cierra el decodificador existente cuando devuelve cero muestras.

Se revisaron las dos pantallas de los dumps 008–012 del 5 de septiembre a las 22:07–22:10 y la memoria de los full 009 y 012. Ambos tenían resolución 0.70 y distancia LOW: inicio de niebla 1152, límite 1536. En MAP01, la pared BUSHG 865 estaba a 99.51 unidades de la cámara del dump 009. En el 012, las paredes 1268 y 1225 estaban a 51.56 y 76.44 unidades. Sus extremos estaban mucho más lejos: interpolar la niebla de esos extremos teñía superficies próximas.

Ahora cada columna de pared, incluidas las paredes enmascaradas de los arbustos, calcula la distancia radial al punto visible. La niebla exterior comienza únicamente al alcanzar su distancia configurada; el oscurecimiento ordinario ya no introduce el color de niebla antes de ese umbral. Esto también elimina el oscurecimiento ordinario de esos sectores exteriores, que compartía la misma tabla de color; se mantienen las exclusiones de interiores y cuevas BLACK. Los suelos usan distancia radial y subdividen los tramos que atraviesan la transición para evitar una sola intensidad en toda la fila.

Los valores predeterminados son 0.70 (280×168) y LOW (1152–1536). Se conservan las preferencias guardadas en INI. El manifiesto de diagnóstico informa las distancias reales, en lugar de escribir siempre los valores de NORMAL.

Validación: 134 pruebas, incluidas la decodificación de los 30 WAV y 4 OGG reales, archivos WAV truncados y chunks con padding, y comparaciones geométricas independientes de las paredes de los dumps con distintas cámaras y campos de visión. Compilación ARM y comprobación nativa de scripts con -norun. El empaquetado verifica checksums, estructura del banner y sonido HOME muestra por muestra. El logo y los menús se conservan; el CGFX debe mantener SHA256 168f01fc1960750a9a4dd9c1fbe061acf6ea17a4b7d87da3ac1615799cae1762.

No se ha ejecutado en Azahar ni en una 3DS. La audición final, el aspecto de la niebla y el rendimiento requieren la prueba física del usuario. No se publica ningún CIA ni release en GitHub.
