# Polifonía del FM1: comparación de JIANT 0.9.17 y Bigorna

Fecha: 2026-10-21. Síntoma comunicado: FM6 y VOICE cargan más; al superar tres pistas empiezan a desaparecer notas/pistas. Versión instalada: **0.9.17**. Aún no se confirmó si esas pistas usan notas individuales, acordes o UNISON.

## Fuentes fijadas

- JIANT 0.9.17: [98a04cf](https://github.com/juanjiant-bit/JIANT/tree/98a04cfbd753372dc66370c56b71500db21fc399), identificado por el commit que publicó esa versión en el instalador.
- Rama de correcciones del PR #189: [a65c58e](https://github.com/juanjiant-bit/JIANT/tree/a65c58e623b615a6154db2820e183e4355b5ade8).
- Bigorna: [39a46bf](https://github.com/nzimas/bigorna/tree/39a46bfbe4e85b5aaa1b61afcc9488beec7fbe92).
- Código clave de Bigorna: [tracks.c](https://github.com/nzimas/bigorna/blob/39a46bfbe4e85b5aaa1b61afcc9488beec7fbe92/firmware/src/tracks.c), [fmt.c](https://github.com/nzimas/bigorna/blob/39a46bfbe4e85b5aaa1b61afcc9488beec7fbe92/firmware/src/fmt.c), [audio_worker.h](https://github.com/nzimas/bigorna/blob/39a46bfbe4e85b5aaa1b61afcc9488beec7fbe92/firmware/src/audio_worker.h), [changelog](https://github.com/nzimas/bigorna/blob/39a46bfbe4e85b5aaa1b61afcc9488beec7fbe92/docs/changelog.html).

Es una comparación de código y una sonda en host. No se midió CPU ni capacidad sostenible en un FM1 físico. Las mediciones de hardware del changelog de Bigorna son del autor y de sus motores, no de JIANT.

## Qué está limitando a JIANT

En voice.c, voice_cap() permite 12 voces compartidas con dos núcleos, 8 con uno, 6 en ECO; gov_cap puede reducir el presupuesto hasta 4. No son 12 voces por pista. FM6 tiene además un máximo de 4 voces por pista; LOAD LITE, incluido el activado automáticamente, lo reduce a 2.

El regulador de audio.c reduce el presupuesto tras carga sostenida, más deprisa por encima de GOV_HOT, y activa LITE tras sobrecarga persistente. shed_voice() apaga colas y voces adicionales. Estas protecciones conservan la nota principal de cada pista; el presupuesto puede sobrepasarse cuando todas las restantes están protegidas. Por eso un CAP 4 no equivale a un máximo absoluto de cuatro pistas.

Se comprobaron iguales entre 0.9.17 y la rama del PR los bloques de definición del presupuesto/asignación, voice_room(), gov_tick() y shed_voice(). El reparto entre núcleos sí cambia en el PR.

La sonda tools/voice_capacity_probe.c usa el DSP real de la rama, notas sostenidas, envíos secos y 128 bloques de asentamiento. El regulador de tiempo de la ISR no se ejecuta en esta sonda: su CAP es una entrada. Resultados para ambos motores:

| Escena | Presupuesto | Voces tras asentarse | Voces tomadas para otras notas |
| --- | ---: | ---: | ---: |
| 3 pistas, tríada en cada una, 2 núcleos | 12 | 9 | 0 |
| 4 pistas, tríada en cada una, 2 núcleos | 12 | 12 | 0 |
| 5 pistas, tríada en cada una, 2 núcleos | 12 | 12 | 3 |
| 6 pistas, una nota por pista, 2 núcleos | 12 | 6 | 0 |
| 3 pistas, tríada en cada una, 1 núcleo | 8 | 8 | 1 |

Esto descarta un límite fijo de tres pistas en la asignación. No demuestra que el FM1 tenga tiempo para renderizar esas escenas: con carga real puede bajar el presupuesto, funcionar un solo núcleo o activarse el descarte de voces.

## Qué sirve de Bigorna

### 1. Repartir voces individuales por coste: la prioridad

Bigorna vr_block() ordena las voces por su coste medido y asigna cada una al núcleo menos ocupado. CPU1 empieza con el coste de los efectos. Cada voz escribe un búfer propio; CPU0 espera la finalización y después mezcla las pistas.

JIANT mc_split() reparte pistas completas. Un acorde FM6 no se puede dividir entre los núcleos. En la sonda de tres tríadas FM6 el reparto de voces es 6/3, aun con nueve voces admitidas. Bigorna puede repartir unidades más pequeñas. El beneficio real depende también de efectos, mezcla, caché y preparación: no se puede deducir un porcentaje de CPU de ese conteo.

Aplicación propuesta: separar preparación por pista, render por voz y mezcla final. La preparación actualiza LFO, articulación, modulación y rampas una vez; el render de cada voz usa una vista estable y salida propia; la mezcla suma en el orden original después del join. Empezar por FM6, manteniendo sus seis operadores, sus 32 algoritmos y los presets existentes.

### 2. Quitar dependencias compartidas de VOICE antes de repartirlo

VOICE usa formant_nz, un estado de ruido global. La 0.9.17 permite que varias pistas lo modifiquen desde núcleos distintos. El PR #189 evita la carrera agrupándolas; esta corrección de integridad puede dejar todas las pistas VOICE en un solo núcleo. No constituye por sí sola una mejora de su polifonía.

La arquitectura de Bigorna mantiene memoria independiente por voz, alineada a líneas de caché. Para aprovecharla en JIANT hay que diseñar también la propiedad del ruido, la articulación y los demás estados. Dos alternativas a evaluar: reservar por adelantado tramos deterministas del ruido conservando el orden audible, o introducir estado por voz con una diferencia sonora explícitamente evaluada. No basta con volver a separar las pistas que comparten formant_nz: reaparecería la carrera.

### 3. Evitar trabajo repetido en los motores

Bigorna prepara coeficientes cuando cambian los parámetros y especializa el bucle FM por algoritmo. También conserva estados en variables locales durante el bloque y omite etapas sin aportación. Su changelog describe mejoras importantes de sus motores por esas vías.

JIANT ya selecciona rutas fuera del bucle por muestra en fm6_op_run(), omite operadores FM6 inaudibles, usa búferes por núcleo y ejecuta parte del código caliente en RAM. No corresponde atribuirle el mismo ahorro que obtuvo Bigorna al eliminar comprobaciones que JIANT ya hace fuera del bucle.

Candidatos a medir: cálculos repetidos de coeficientes de formantes, divisiones y conversiones de frecuencia al comienzo del bloque, el bucle de feedback FM6 y la ubicación del código en RAM de cada núcleo. Cualquier caché de coeficientes debe invalidarse exactamente ante pitch, articulación, modulación y cambios de sonido; sólo sirve si ahorra más trabajo del que agrega y cabe en RAM.

### 4. Afinar la selección de colas, sin confundirla con más capacidad

Bigorna voice_pick() elige la cola en liberación más silenciosa; tail_victim() intenta preservar ataques recientes durante unos 150 ms. JIANT ya elige la cola más baja en shed_voice(), pero voice_victim() prioriza antigüedad dentro de la categoría de voces liberadas.

Se puede mejorar qué nota se sacrifica cuando la capacidad se agota, conservando la protección musical de JIANT. Para FM6 hay que considerar sus envolventes propias: la envolvente genérica no representa necesariamente el nivel real de sus operadores. Esto reduce cortes perceptibles; no aumenta por sí mismo la potencia del procesador.

## Qué no trasladar directamente

Bigorna tiene 12 voces globales, igual que el máximo actual de JIANT con dos núcleos; sus motores son 4-OP, DRUM, WAVY y HIVE. No tiene el FM6 de seis operadores ni el cantante VOICE. Subir la constante de voces o copiar el umbral del 90 % de Bigorna no demuestra que el audio llegue a tiempo.

Su reverb trabaja a media frecuencia. Aplicar algo similar a una reverb de JIANT que aún trabaje a frecuencia completa es una opción de calidad/coste: cambia la respuesta y necesita escucha y pruebas. JIANT ya tiene otras etapas a media frecuencia, código en RAM, efectos en CPU1 y perfilado de hardware; no son novedades que se deban añadir indiscriminadamente.

El código consultado es GPL-3.0; cualquier adaptación debe conservar licencias y atribuciones, incluyendo los componentes de Melodee/TONITOR que Bigorna identifica.

## Medición desde el FM1, sin computadora

La 0.9.17 ya tiene MENU > ABOUT > PROFILE, accesible con OCT+ desde ABOUT; SELECT desplaza el informe. Con la secuencia problemática sonando conviene registrar:

- T1..T6 y sus AVG/MAX; CORE 0 WAITS y CORE 1 BUSY.
- FX BUSES / REVERB y WHOLE HALF; LATE HALVES.
- Al final: VOICES, CAP, FULL/LITE/ECO y el estado de CPU/núcleos.

Así se distingue robo por presupuesto, sobrecarga de un núcleo, efectos costosos y retirada de CPU1. Puede hacerse mostrando/fotografiando la pantalla: la actualización por Chrome en Android sigue sirviendo para un futuro paquete compilado.

## Reproducir la sonda

Tras generar build/gen:

```sh
mkdir -p build/host
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/voice_capacity_probe tools/voice_capacity_probe.c -lm
build/host/voice_capacity_probe fm6 4 3 1 12
build/host/voice_capacity_probe voice 6 1 1 12
build/host/voice_capacity_probe fm6 3 3 0 12
```

Argumentos: motor (fm6/voice), pistas (1..6), notas por pista (1..4), segundo núcleo (0/1), CAP del regulador (4..12). La máscara informada es la decisión del planificador, no una medición de ocupación física. Con un solo núcleo no se envían trabajos aunque se muestre la decisión hipotética.

## Orden de trabajo y criterio de éxito

1. Capturar el perfil de la escena real en 0.9.17.
2. Prototipar reparto por voz para FM6, comparando PCM/estado contra la ejecución serial con igual presupuesto.
3. Hacer repartible VOICE resolviendo primero sus estados compartidos; repetir pruebas concurrentes, cambios de presets y retirada del segundo núcleo.
4. Medir en FM1 cuántas voces FM6/VOICE se sostienen sin LATE ni SHED; después ajustar el regulador con esa evidencia.

El éxito es conservar más notas de la misma escena sin cortes ni retrasos del audio. Las correcciones del PR #189 son una base de estabilidad; todavía no validan un aumento de polifonía en hardware.
