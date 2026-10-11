**Seguimiento de implementación — 9 de octubre de 2026**

**Pausa solicitada el 11 de octubre de 2026.** El alcance autorizado para retomar
queda limitado a unas dos horas, prioridad del BPM del DAW, correcciones de fallos y
pruebas esenciales. Se aplazan las calibraciones musicales y las campañas
exhaustivas. Se conserva la estimación existente cuando el DAW no aporta tempo;
mejorarla no es prioritario. Leer primero [PAUSA_Y_REANUDACION.md](PAUSA_Y_REANUDACION.md): conserva
el estado confirmado, la batería ASan todavía parcial y la limitación para
confirmar desde esta conversación lateral la pausa del objetivo principal.

Este archivo conserva los avances y pendientes del informe
`ANALISIS_FUNCIONAMIENTO_RENDIMIENTO.md`; no sustituye la auditoría final.
La tabla describe el estado actual; las secciones posteriores conservan el
alcance y los pendientes que existían al validar cada tanda.
El gate combinado de estrés ASan pasa en 280,27 s con sus 300 s originales,
tras cambiar el SHA-256 compartido a CNG nativo. Conserva cuatro grabaciones de
120 s y todo el lifecycle. Plugin Release 12/12 y ASan 12/12 (once suites y el
estrés en dos invocaciones), app 3/3 en ambos modos y pluginval nivel 5 pasan.
La evidencia de estas fuentes se registra en
[checksums y fases](out/validation-20261009-stress-phases/VALIDACION.md).
Los fallos anteriores y su evidencia permanecen archivados.
La continuación de arranque corrige la publicación inicial y los avisos de rutas
ausentes. Trece procesos nuevos por modo pasan en Release/ASan; las tres suites
anteriores de la app pasan sobre las mismas fuentes de producción. El gate de
arranque se repite por separado tras corregir su fixture; no se presenta como una
pasada 4/4 conjunta. Alcance, fuentes, imágenes y JUnit reales están en
[arranque](out/validation-20261009-startup/VALIDACION.md).

| ID | Estado actual | Trabajo y comprobación pendiente |
|---|---|---|
| 01 | Implementado y probado en fixtures Windows | Instantánea y comandos sin I/O bajo projectLock; prepare sin join. Fachada separada de un Core retenido: cierre solicita cancelación y la limpieza espera en segundo plano a todos los hilos nativos. Handles de finalización y referencias de DLL evitan liberar código/buffers antes de tiempo. Cierre durante detector bloqueado y ocho descargas de una DLL auxiliar pasan; falta la validación en DAWs reales. |
| 02 | Parcial, notificaciones y recuperación del índice probadas | Índice asíncrono con publicaciones inmutables y cachés de metadata/candidatos. Watchers Windows de config, previews y raíces con debounce de 200 ms (máximo de ráfaga 1 s), invalidación y polling de respaldo a 30 s; máximo 16 watchers de 32 KiB, cierre tras completar cancelación nativa. Config inválida/inaccesible, raíz offline y enumeración fallida conservan entradas/carpetas/tags con aviso y ruta; retry y biblioteca vacía válida probados. Manifests ilegibles/dañados/ausentes y audio ausente conservan la fila conocida o muestran un placeholder; entradas sanas siguen actualizándose. Inventario de candidatos transaccional ante raíz ausente/permisos, sin inferir unicidad desde un árbol incompleto; junctions/ciclos/aliases verificados en ASan. Raíces explícitas, incluidas vacías, mandan en app/plugin. Logs de prepare/editor en el worker, con escritura bloqueada probada. prepareToPlay solicita una instantánea a un worker independiente y captura desde cero mientras espera; duración, mono, calidad y destino se aplican juntos antes del commit. Error de configuración conserva el audio; lectura retenida, reintento y cierre probados. La preparación general de RecordingEngine también solicita ajustes asíncronos y adopta el destino compartido actual; las fixtures con destino independiente usan Snapshot explícito. Matriz 100/1.000/10.000 y escena completa pasan. El arranque del contenido principal se verifica ahora en trece procesos nuevos por modo, sin precargar el índice: carga/vacía/error, búsqueda conservada, avisos de rutas ausentes, carpeta inaccesible y cierre temprano. Se elimina la verificación duplicada y la cola de diálogos por ruta. El fixture comparte sólo archivos fuera de las cachés del proceso; no prueba disco frío ni ventana/bandeja nativas. Faltan aceptación de I/O físico lento y pipeline final conjunta al cerrar las mejoras. |
| 03 | Implementado y probado | Parches por campo y deltas de tags/carpetas, lock entre procesos. Pruebas de copias antiguas y dos escritores pasan. |
| 04 | Implementado y probado | Validación por identidad, traslado y sustitución atómica; resolución conservadora. Caché detecta sustitución de metadata con igual fecha/tamaño y conserva la evidencia histórica tras rechazar un proyecto reemplazado. Abrir valida de nuevo identidad en un worker. Adopción del plugin verifica ID/hash y rechaza múltiples entradas; una ruta reutilizada no adopta el audio ajeno. Homónimos/copias idénticas ambiguos, cancelación y Save As homónimo por título probados. |
| 05 | Implementado | Finalización de captura publica atomics; no señal de WaitableEvent desde audio. Matriz recoge p99/p99,9/máximo del modo de tiempo real. Falta carga controlada en DAWs. Las exportaciones marcadas offline pueden esperar al spooler; se miden por separado. |
| 06 | Implementado y probado, también con ASan | Desconexión del transport antes de liberar el reader; la fuente conserva decoder y caché hasta terminar el hilo nativo. Cargas rápidas/corruptas, 100 cierres durante carga, cierre con lectura bloqueada y cierre con selector de audio abierto pasan en Release y AddressSanitizer. |
| 07 | Implementado y probado en fixtures | ReadAheadSource sustituye la ruta BufferingAudioSource/TimeSliceThread: decoder exclusivo del worker, páginas atómicas, prefill asíncrono de 250 ms y cero I/O/notificación bloqueante en su callback. Caché de unos dos segundos, máximo ~3,1 MiB por lector y 64 MiB compartidos, incluidas fuentes retiradas. Saltos, EOF, disco simulado bloqueado y underruns observables pasan; falta validar dispositivos/discos físicos bajo carga. El dibujo usa una envolvente inmutable calculada en segundo plano. |
| 08 | Implementado y probado en fixtures Windows | Revisiones WAV/OGG inmutables, manifiesto único, CAS y tombstones; recibos con SHA256 y respaldo de metadata. Recuperación desde revisión previa/respaldo/historial completo preserva organización e identidad actuales. Siete fases de commit y tres de conservación explícita con interrupción real de proceso probadas, junto con corrupción de PCM, manifest/backup inválidos y lectores abiertos. Save As publica una revisión nueva bajo el ID destino, sin copiar recibos del ID anterior. El índice repara manifests ausentes/dañados y archivos faltantes en workers; reparación completa por menú. No equivale a validar cortes de energía o fallos físicos de disco. |
| 09 | Parcial, memoria sostenida y ciclo de capturas largas medidos | Captura en ring y WAV temporal, mezcla/codificación por bloques y PCM float. Ajustes mono ya publicados permiten reservar un canal desde prepare; ajustes pendientes conservan estéreo en ring/temporal y el encoder promedia a mono. Límites: 16 MiB por banco, 64 MiB por instancia con bancos retirados y 256 MiB de colas por proceso; matriz verifica 18 instancias. Cuatro capturas simultáneas de 120 s/192 kHz verifican archivos, ausencia de descartes y liberación de reservas; miden commit privado y working set del proceso Windows. Reconfiguración tras 75 s acelerados conserva cola de 120 s y cambia 192→96 kHz/biblioteca/mono; cierre tras 90 s con descubrimiento retenido conserva el audio confirmado. Release y ASan verifican reservas máximas de 32 MiB y retirada nativa. ASan informa memoria de proceso y comprueba asignaciones vivas, sin aplicar el umbral de commit de Release a su runtime. Ajustes incluyen WAV+OGG, historial, uso real y espacio libre; retención conservadora de propietarios desconocidos. Faltan I/O física controlada y validación en hosts reales. |
| 10 | Parcial, cuatro instancias bajo captura sostenida probadas | Workers separados de descubrimiento, spool, codificación y análisis. Análisis del PCM inmutable puede solaparse con OGG; comparten dos permisos globales, también para checksums, liberados antes de esperar análisis. Índice a 30 s, títulos a 250 ms y polling confirmado a 1 s. Tablas de nombre/ID/hash y cancelación. Hash usa buffer de 64 KiB para evitar lecturas nativas de 64 bytes. Lecturas de preparación independientes, con máximo 32 workers retenidos por módulo y rechazo/retry explícito al saturar; reserva liberada tras completar el hilo nativo. Detector bloqueado, resultados obsoletos y cuatro pipelines de 120 s pasan; miden latencias y memoria de proceso. Faltan I/O física lenta controlada y coste dentro de DAWs reales. |
| 11 | Implementado y probado en fixtures Windows | Instantánea con revisión, fecha y SHA256 del mismo texto JSON; las ediciones externas con revisión/fecha/tamaño reutilizados se distinguen. Cada banco fija destino y ajustes; las capturas en cola terminan en su carpeta original. Cambio de biblioteca con commit bloqueado, dos capturas antiguas y una nueva pasa, también vuelta a la identidad original y limpieza de pendientes en ambas raíces. Metadata conserva versión/fingerprint/duración/calidad/mono de la captura. Preparación asíncrona retiene la muestra cero con lectura bloqueada más allá de la duración; bancos de 8/16 kHz cuyos ajustes terminan fuera de orden mantienen destino/formato y orden. Restauración conserva el orden de las acciones posteriores. saveState publica sólo el último destino conocido mientras espera, sin I/O. La app carga una vez en worker, preserva 45 s/calidad 9/mono y guarda en un commit. El editor del plugin también lee/guarda fuera de UI, preserva campos ajenos y explica que output se aplica al preparar/recargar. Errores, retry y cierre durante guardado pasan; interacción del chooser nativo pendiente en 16/17. |
| 12 | Parcial, bucles cortos y límite de un segundo cubiertos | Posición desconocida separada y disparo al cero. Capturas inferiores a un segundo mantienen audio confirmado, muestran instrucción de replay y tienen contador separado en tooltip; exactamente un segundo se acepta. Matriz de 25 combinaciones y exportación acelerada de 15 s/192 kHz pasan. Cuatro casos con el procesador real (44,1/192 kHz y 32/2048) comprueban ocho bucles cortos, dos loops aceptados y tercero rechazado con commit retenido, ausencia de reinicio a mitad de loop, rearme al cero, passthrough y cola/recibos intactos. Editor real muestra aviso/contador. Lifecycle cubre saturación y prepare repetido. La asociación automática pending pasa a Done tras promoción; regresión verifica identidad/audio. Faltan extremos de carga física y aceptación en DAWs. |
| 13 | Implementado y probado en fixtures Windows | Selector de archivo y conservación explícita sin archivo, persistentes en XML/metadata. Origen automático/manual/ambiguo y evidencia categórica visibles; no es probabilidad calibrada. Conservación, rename, cierre/restauración, traslado por identidad, ruta reutilizada, homónimos y Save As sin ruta probados. Captura bloqueada ante archivo manual sin verificar; selección obsoleta no cambia otra entrada. Reutiliza un ID ya conservado desde la app y mezcla con un destino existente sin duplicar su asociación. Error del árbol retiene identidad/preview y frena captura no verificable; recuperación y asociación manual directa pasan en la tanda terminal de 12 suites plugin. Menú/chooser usan SafePointer e ID capturado; render del editor revisado. Inventario estable a 30 s y retry tras error a 5 s. Interacción nativa y DAWs reales pendientes en 16/19. |
| 14 | Parcial, candidato v5 aceptado en el corpus reservado | Análisis a 22,05 kHz por canal; picos espectrales interpolados/fondo local y perfiles mayor/menor ajustados sólo en desarrollo. Tempo combina autocorrelación normalizada y fuerza Fourier entre 40–240 BPM, sin conversión fija de octavas; conserva prioridad del host y rechaza cambios reales 120→150/240. Caché de cola exacta con representación/límites intactos; resampling mono calcula una vez y planes FFT se reutilizan por estimación. Calibración, corpus sintético y fuentes/candidato fijados antes de holdout. GiantSteps nativo 1.225/1.225, cero errores, mismos originales/PCM que v4 y auditoría independiente completa. En holdout (621 entradas), BPM exacto 208/335 (62,09%) frente a 54/335 (16,12%); tonalidad 146/303 (48,18%) frente a 74/303 (24,42%). Cobertura 94,03/76,24%, precisión entre emitidos 66,03/63,20%. Completo: BPM 418/661 (63,24%) y tonalidad 305/604 (50,50%). Confianza sigue heurística: BPM con score ≥0,75 acierta 3/9 reservados. Falta calibrarla y verificar otros géneros/datos independientes; el holdout usado no vuelve a considerarse intacto. Evidencia y límites en validation-20261009-calibration. |
| 15 | Implementado y probado en fixtures Windows | WAV float conserva exactamente muestras y headroom. Crossfade de 5 ms dentro del prefijo reemplazado; cola posterior intacta a igual frecuencia. OGG se decodifica completo antes de publicar y se recodifica desde PCM intacto si supera ±1; error conserva la revisión previa. Metadata de gain/pico/pases/política se restaura conjuntamente. Sinc centrado, 25 pares de frecuencia, impulsos, bloques variables, equivalencia archivo/memoria, cancelación y espectro probados; longitud de salida común incluye redondeos de media muestra. Caché por ventanas de chroma/flux reutiliza sólo cola de una revisión publicada y verificada, con igual formato/duración y margen del sinc/FFT. Recalcula estimación global; ahorra normalización/FFT del resto. Límites de vectores 512 KiB/instancia y 16 MiB/proceso, exhaustión real y retirada probadas. Comparación exacta de ventanas/resultados, invalidación, retry y pipeline de recuperación pasan. Aceptación musical real sigue en 14 y dispositivos/DAWs en 18/19. El guard controla pico de muestra, no true peak. |
| 16 | Parcial, escena completa y cierres de menú probados | SafePointer, acciones capturadas al abrir menú, selección por projectId, re-filtrado y escrituras asíncronas. Rename conserva acción/texto al reutilizar filas. El UAF del popup del sidebar se reprodujo/corrigió con ASan. Tags nativos cubren 512 valores y scroll/commit; contenido de 10.000 tags/sugerencias cubre botones virtuales, edición con layout pendiente, reciclado y cierre seguro. MainComponent real con 100/1.000/10.000 entradas cubre última fila, reciclado (11 controles), búsqueda, filtros de carpeta/favoritos, hotkey F, selección por ID tras reordenar y cierre; renders a 640x480/1024x768 revisados. TooltipWindow único compartido en la app y propio del editor con parent para scaling; nombres cortos ya no sufren elipsis por ancho insuficiente. El selector real de revisiones ambiguas cubre elección sin default, carga OGG, cancelación, fila reciclada y cierre antes/después de aceptar una acción con worker retenido; la revisión/identidad capturada manda. Un refresco del mismo ID conserva texto y actualiza el destino del commit si se traslada metadata; menú verifica ID y ruta antes de rename inline. Cinco popups reales después de rebind, tags con guardado real tras reordenación/cambios concurrentes y cierre de lista antes de entregar una selección pasan en Release/ASan. Regresión app 2/2 en ambos modos (173,62/287,86 s). Faltan selectores nativos de archivo/carpeta, otras combinaciones de interacción y aceptación dentro de DAWs. |
| 17 | Parcial, recuperación explícita de huérfanos ambiguos y fallos de escritura probados | Saves/settings/device devuelven Result y errores con fase/ruta. Config/directorios del índice conservan la última publicación. Manifests confirmados dañados/ausentes, lectura exclusiva tras invalidar caché, audio ausente y directorio sustituido conservan datos conocidos sin prestar identidad a otra carpeta; placeholder nuevo y tombstone probados. La lista muestra número de entradas no disponibles y acción/ruta, permite cambios sanos y recupera al restaurar archivos. Permiso nativo de enumeración denegado a una subcarpeta se prueba con DACL conservada/restaurada; no convierte homónimos en una coincidencia única. Editor conserva errores y permite retry sin esperar a escrituras al cerrar. Promoción propia exige token y commit exitoso actualiza Done; propietarios activos/desconocidos protegidos. Settings distingue configuración dañada legible de lectura fallida: acción explícita Rebuild o Reload, copia de bytes original y SHA bajo lock antes de reemplazar. Raíces JSON array se rechazan en app/plugin/manifest/receipt/patch. Archivos truncados/UTF-16/vacíos/arrays, preferencias ajenas, cambios concurrentes, sharing nativo de lectura/reemplazo y cierre tras aceptar reconstrucción pasan en Release/ASan; renders de Settings revisados. JSON Windows comprueba bloques/flush y usa reemplazo nativo con copia del original; errores preservan fase/ruta/código/acción. Fallos inyectados tras temporal parcial/completo conservan bytes/identidad; permisos DACL de parent/temporal/respaldo, read-only y sharing pasan con retry en app/plugin Release/ASan. La ruta de 272 caracteres se reproduce y explica con acción de acortar sin alterar el original; no se afirma soporte arbitrario de rutas largas. Dos escritores interrumpidos tras recibo producen opciones verificadas sin selección inicial; la app permite escuchar/restaurar/cancelar. SHA256 de bytes distingue manifest ausente/vacío; fingerprint del recibo, checksum/audio y propietario se verifican antes de publicar bajo lock. Cambios concurrentes, corrupción elegida, cancelación, lectura exclusiva y propietarios activos/desconocidos se rechazan; confirmación desconocida también protege Repair. Restauración conserva organización/análisis y otras revisiones. UI real con fila reciclada y worker retenido prueba cierre seguro antes/después de aceptar; App 2/2 y plugin 6/6 Release/ASan pasan, VST3 pluginval 5. ASan app exigió reconstrucción limpia por MainComponent.obj antiguo y registro de cabeceras incompleto; después pasa una comprobación incremental de la cabecera. Completar disco lleno físico/PCM/OGG, estados parciales nativos de reemplazo y selectores de archivo/carpeta. |
| 18 | Implementado y probado con driver simulado | Dispositivo lazy, selector Audio con estado persistido y liberación dos segundos después de pausa/stop/EOF. La bandeja conserva reproducción, reduce timer de 42 a 250 ms y pausa dibujo/labels; biblioteca oculta a 30 s. Driver simulado verifica audio, pausa/reapertura, desconexión/reintento, error de inicialización, recuperación mediante selector y cierre con diálogo abierto. Recuperar salida elimina su error y conserva un error de preview corrupto. Falta medir CPU y validar drivers reales. |
| 19 | Implementado y probado | Buses mono/estéreo coincidentes probados y layouts asimétricos/multicanal rechazados; selector incluye bwproject. README/BETA_NOTES especifican VST3 sin Pro Tools nativo/AAX y distinguen fixtures de verificación real de hosts. Smoke tests de los DAWs siguen pendientes. |
| 20 | Pipeline y CI implementados y validados localmente; aceptación de distribución en curso | VERSION central, commit/dirty, build ID y fingerprint compilados; build nuevo con todas las suites, pluginval 5 sobre el bundle staged, manifiesto/hash y publicador con instalador explícito/tags. Origen único de fetch/push registrado y --repo explícito para gh. Compilador portable y extractor fijados por SHA; comparación de contenido del instalador, identidad, arquitectura y versión. 74 contratos y siete casos de aislamiento pasan. Pipeline con empaquetado termina con 12 suites plugin, 3 app, pluginval y 25 artefactos verificados; candidato de desarrollo no publicable, anterior a las correcciones posteriores de UI. CI Release/app ASan/plugin ASan escrito, sin publicación automática; gate de las nueve suites plugin ASan probado localmente; las doce suites del plugin pasan con ASan, incluida la de estrés combinado con sus 300 s originales. Faltan workflow remoto, pipeline final de todas las fuentes al cerrar las mejoras y aceptación de instalación/actualización/desinstalación. |
| 21 | Parcial, tags/árbol y escena de 10.000 entradas probados/renderizados | Conteos deduplicados, árbol por padre, selección por ID y publicación común de entradas/carpetas/tags. Worker deduplica/ordena tags; diálogo reutiliza botones/anchos y usa set para sugerencias, scroll y elipsis legible con texto completo en tooltip. Fixture de 1.102 rutas/512 tags y escena MainComponent de 100/1.000/10.000 entradas pasan en Release/ASan; últimos renders muestran árbol, nombres completos y límites mínimos. ListBox mantiene 11 componentes; último Release mide publicación/dispatch de 10.000 filas 24,12 ms y filtro de carpeta 6,33 ms, ensayos locales sin garantía. Bounds mínimos/persistentes implementados. Tags ya mantiene sólo chips/sugerencias visibles y mide conjuntos grandes de anchos desconocidos en worker; conserva valores/acciones al reciclar y descarta resultados obsoletos. Colecciones de 100/512/10.000 y 10.000 sugerencias pasan por separado en Release/ASan, con 55/20 controles; edición/commit, cierre y worker retenido probados. Último separado Release: contenido de 10.000 tags 3,27 ms, ready en fondo 260,80 ms, edición 5,19 ms. Son ensayos, no coste de ventana/driver ni garantía. Interacción de filas/menús y tags con guardado después de reordenación/edición concurrente ampliada y probada en Release/ASan; cierre tardío seguro. Arranque de MainComponent con índice nuevo, nueve combinaciones de tamaño/rutas ausentes y cuatro escenarios de vacía/error/offline/cierre pasa en Release/ASan. Mantiene 10/11 filas reales y distingue la espera de una biblioteca vacía, sin modales automáticos por proyectos ausentes. Faltan otras combinaciones de interacción, selectores nativos y aceptación de ventana/bandeja, disco frío y hardware. |
| 22 | Parcial | Logger con rotación; 600 mensajes verifican cuatro archivos de hasta 1 MiB. Métricas de inventario, hash, descubrimiento, PCM, OGG, análisis, espera del análisis, commit/espera global, total y bytes de cola. Underruns fuera del callback; índice registra entradas/candidatos, latencias, cambios y lecturas/hits. Benchmarks separados miden dispatch, copia/filtro y captura sostenida de cuatro instancias con p99/p99,9/máximo y memoria de proceso Windows. Falta telemetría bajo carga en DAWs reales. |

Validación anterior al trabajo de recuperación: cuatro suites del plugin pasan en Release
(RememberRegression 9,29 s, RememberAnalysis 3,08 s, RememberCaptureMatrix 3,45 s
y RememberHostLoading 0,33 s; 16,18 s total). Incluyen cierre durante descubrimiento
bloqueado, ocho descargas de DLL auxiliar, rechazo de resultados obsoletos,
guardado independiente del descubrimiento, cuatro pipelines concurrentes,
crossfade, límites 40/240 BPM y exportación acelerada.
La suite de regresión app actual pasa en 6,48 s en Release y 7,13 s con AddressSanitizer.
La matriz de biblioteca pasó en 197,37 s en Release, incluidos creación y limpieza de fixtures.
La app y el VST3 actuales compilaron; la copia automática del VST3 está desactivada.
`git diff --check` pasa. Son fixtures locales y pruebas de compilación, sin
certificación de DAWs reales ni fallo de disco físico. La instrumentación ASan
cubre ahora el ejecutable de pruebas de la app, incluido JUCE; no el VST3 en un DAW.

La confianza musical registrada es heurística, no una probabilidad calibrada.
Los límites de 16/64/256 MiB corresponden a colas de captura, no a la memoria total
del proceso. En modo offline el motor puede esperar hasta cinco segundos por
bloque para que el spooler libere espacio; en reproducción en tiempo real nunca
espera al disco. La interfaz de ajustes realiza medición y guardado en workers
distintos, para que una consulta larga no detenga una edición.

En la última matriz sintética se midieron 20.343 callbacks de tiempo real:
p99 0,0025 ms, p99,9 0,0090 ms y máximo 0,0159 ms. El fixture offline de 15 s a
192 kHz se capturó y guardó en 987,11 ms. Son resultados locales de este ensayo,
no garantías del plazo de audio en un DAW ni porcentajes de CPU.
Las mediciones históricas del informe y sus
binarios auxiliares no se consideran validación de estos cambios.

La destrucción de la fachada durante un detector bloqueado tardó 0,0685 ms;
se comprobó que las reservas seguían vivas mientras el detector estaba retenido
y se liberaban después de desbloquearlo. Los cuatro pipelines de capturas de
1,2 s/8 kHz finalizaron en 100,09 ms, conservando identidad y audio de cada instancia.
Son fixtures sintéticos. La DLL auxiliar ejercita el servicio de retirada y su
vida útil nativa, no sustituye una descarga real del VST3 por un DAW. El ejecutable
de pruebas espera a la retirada de cores antes de cerrar JUCE y sus globales.
Una primera pasada coincidió con compilación y tardó 42,21 s; las cifras anteriores
proceden de la pasada final sin compilación simultánea. No permiten atribuir una
mejora porcentual a estos cambios sin un benchmark con carga controlada.

**Reproducción y dispositivo — validación del 7 de octubre.** El ensayo de
ReadAheadSource recorre 12.016 callbacks en cuatro frecuencias (44,1/48/96/192 kHz),
incluyendo bloques de 32/128/2048/8192, reutilización continua de páginas, mono,
estéreo, 100 saltos por frecuencia y final de archivo. En Release: p99 0,0005 ms,
p99,9 0,0020 ms y máximo 0,0319 ms. Se mide solamente la fuente de caché; no el
driver, el resampler ni la cadena de salida completa. La prueba sintética consume
audio más rápido que tiempo real, esperando disponibilidad entre bloques.

Una lectura bloqueada devuelve silencio sin avanzar por muestras ausentes;
el contador de underruns aumenta. El cierre de la fachada durante esa lectura
tardó 0,0894 ms, mantuvo vivos decoder/páginas y liberó los recursos después.
La saturación de fuentes activas/retiradas llegó a 65.372.160 bytes reservados y
rechazó nuevas reservas antes de superar 64 MiB. Es un presupuesto de buffers,
no una cota de memoria total. No se usa espera del decoder ni eliminación de
TimeSliceClient al preparar, cambiar selección o cerrar la fuente.

La envolvente contiene como máximo 2.048 picos por canal y se genera leyendo en
bloques de 8.192 frames; la pintura no abre/decodifica archivos. La salida se
abre después del prefill, se libera tras dos segundos en reposo y se puede elegir
en Audio. Las pruebas de driver son simuladas. El cierre de la app mantiene
dispatch mientras terminan tickets de carga, cores retirados, consultas/ediciones
y sus callbacks. Cancela el índice y comprueba su finalización nativa antes de
salir. Una consulta bloqueada verifica que la solicitud de cierre no espera a I/O,
que dispatch sigue vivo, y que los trabajos nuevos reciben un Result de cierre.

Resultados completos: [Release](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/LastTest.log>)
y [AddressSanitizer](<E:/JUCE PLUGINS/Remember/App/build-asan/Testing/Temporary/LastTest.log>).
El log instrumentado confirma `AddressSanitizer instrumentation enabled`.
La configuración de ASan sigue las [opciones y límites de MSVC](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues):
desactiva RTC, Edit-and-Continue y enlace incremental en ese build, y copia el
runtime junto al ejecutable de pruebas. No se instalaron binarios en Program Files.

La repetición del ensayo de cuatro pipelines detectó una carrera adicional en
la creación de `.pending`: dos entradas tienen UUID distintos, pero comparten
padres. JUCE puede observar un padre ausente y luego recibir ERROR_ALREADY_EXISTS
cuando otro worker lo crea. El test original atribuía cualquier timeout a permisos
de análisis; se añadieron diagnósticos de estado/error y se reprodujo el fallo de
directorio. LibraryStore::ensureDirectory confirma cada padre y acepta la creación
concurrente solamente si el directorio requerido existe. Una colisión con un
archivo sigue siendo error. Esta ruta se comparte con revisiones, JSON y logs.
El fixture añade ocho grupos de ocho creadores simultáneos. Tras corregir la
carrera, RememberRegression pasó seis veces consecutivas (68,01 s total) y la
pasada completa final volvió a pasar las cuatro suites. Los resultados finales
están en [log del plugin](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/LastTest.log>).

**Índice y consultas de la app — validación adicional del 7 de octubre.**
La inspección encontró que el supuesto fast path del escáner leía metadata.json
antes de consultar su caché, y que las entradas ausentes/pending nunca usaban
ese fast path. Ahora se cachea la entrada histórica, se resuelve una copia para
mostrar y se comprueba identidad/fecha/tamaño antes de reutilizar el JSON.
Una sustitución concurrente con la lectura se descarta para reintentar; una
asociación cancelada no escribe una identidad/hash incompletos. La fuente de
audio sigue comprobándose en cada scan para detectar un preview eliminado.

La carga del diálogo lee una configuración coherente en un worker; sus controles
se habilitan al completar. Conserva valores válidos fuera de los presets. La
validación de carpetas tiene generación cancelable y el guardado revalida las
rutas, por si se desconectó una unidad. Lectura de bounds/primer uso, verificación
de proyectos y Abrir/Explorer dejan sus comprobaciones de disco fuera de UI.
Abrir revalida identidad antes de invocar el sistema; la prueba de rechazo no
lanza un DAW. Las funciones nativas de dispositivo, chooser y shell todavía
requieren pruebas reales de interacción y errores. El fixture de ajustes retiene
la lectura, verifica dispatch y cierra 100 diálogos inmediatamente; Release y
ASan pasan, preservando 45 s/calidad 9/mono.

El índice publica un shared_ptr inmutable. Copiar filas ocurre después de
adquirir esa referencia, sin conservar el lock de publicación. La firma usa
elementos por entrada, en lugar de concatenar/serializar una biblioteca completa
en un String creciente. Los ancestros se deduplican con un conjunto y los empates
de fecha se ordenan de forma estable por archivo. El resolver común de app y
plugin usa tablas por nombre/ID/hash; conserva el rechazo de homónimos y copias
ambiguas, revalida candidatos positivos e invalida negativos al reconstruir.

| Entradas | Ausentes | Candidatos | Inventario (ms) | Primer scan tras rebuild (ms) | Scan con caché (ms) | Copia (ms) | Filtro de datos (ms) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 0 % | 100 | 1,28 | 45,30 | 18,58 | 0,04 | 0,10 |
| 100 | 10 % | 90 | 1,13 | 27,88 | 19,04 | 0,04 | 0,10 |
| 100 | 50 % | 50 | 0,62 | 20,02 | 16,20 | 0,02 | 0,10 |
| 1.000 | 0 % | 1.000 | 5,14 | 372,11 | 157,63 | 0,27 | 0,95 |
| 1.000 | 10 % | 900 | 3,90 | 203,90 | 152,78 | 0,26 | 0,88 |
| 1.000 | 50 % | 500 | 2,18 | 172,36 | 140,71 | 0,27 | 0,88 |
| 10.000 | 0 % | 10.000 | 42,16 | 3.745,35 | 1.607,24 | 3,96 | 11,25 |
| 10.000 | 10 % | 9.000 | 39,76 | 2.070,18 | 1.603,36 | 4,09 | 10,68 |
| 10.000 | 50 % | 5.000 | 20,80 | 1.742,32 | 1.465,32 | 3,98 | 10,80 |

Todos los scans con caché tienen **cero lecturas JSON** y un hit por entrada,
incluidas las ausentes. El caso 0 % parte de metadatos no parseados en el proceso;
los casos 10/50 % reutilizan esa caché de JSON, reconstruyen inventario y vacían
resultados de resolución. El ensayo mueve cinco grupos fuera de las raíces
buscadas; valida que no se resuelvan por accidente. Usa archivos reales de
metadata y candidatos con ID, con marcadores de preview sin decodificación y
caché del SO disponible. No mide I/O de unidades remotas, ni la carga de hashes
de archivos grandes. Generar/limpiar fixtures está incluido en el tiempo de
CTest, no en las columnas de scan.

Una pasada previa, antes de añadir las tablas, obtuvo 13.953,70 ms para el primer
scan de 10.000/50 %. La actual obtiene 1.742,32 ms. Son observaciones locales de
dos pasadas, sin garantía porcentual para otras bibliotecas. Al empezar la
segunda matriz aún terminaba la compilación de la app; los casos de 10.000 se
midieron después. Se separan esos tiempos de las prestaciones de un DAW.

Durante la matriz se despacharon 80.896 intervalos de UI solicitados a 2 ms:
p99 3,52 ms, p99,9 4,00 ms y máximo 4,78 ms. Comprueban continuidad del loop de
mensajes mientras trabaja el worker. Copia/filtro son operaciones de datos
medidas en el worker: **no incluyen** ListBox, pintura, sidebar, selección ni
interacción humana, cuya aceptación sigue pendiente. Tampoco son un porcentaje
de CPU en reposo. El benchmark completo está registrado como CTest separado
`RememberLibraryBenchmark`, con timeout 600 s; ASan cubre la regresión app, no
se ejecutó esta matriz de tamaño instrumentada en esta pasada.

El log conservado de la matriz está en
[LibraryBenchmark-2026-10-07.log](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/LibraryBenchmark-2026-10-07.log>).
Tras finalizar, se recompilaron los fixtures actuales y pasaron de nuevo
regresión app Release (6,48 s), ASan (7,13 s) y las cuatro suites del plugin
(16,18 s total). App y VST3 compilados con estos cambios; sin instalar ni publicar.

**Recuperación y publicación — comprobación adicional del 7 de octubre.**
Cada revisión nueva guarda un recibo con SHA256 de WAV/OGG y una instantánea de
sus datos de audio. El manifiesto sigue siendo el único punto de publicación;
un recibo preparado no sustituye un manifiesto válido. Se conserva un respaldo
atómico de metadata antes de editar/publicar. Si hay audio dañado, la reparación
recupera los datos de audio de una revisión completa y conserva tags, favorito,
asociación y campos desconocidos del manifiesto actual legible. Si también se
pierden manifiesto y respaldo, sólo pueden reconstruirse los datos que constan
en los recibos; no se inventan ediciones de organización posteriores.

Las comprobaciones de formato y hash trabajan por bloques fuera del lock de
entrada; antes de reparar se revalida que metadata no haya cambiado. Los
checksums del commit comparten los dos permisos de I/O/análisis del proceso.
El índice no hashea todas las grabaciones en cada poll: intenta reparar manifests
ausentes/invalidos y archivos faltantes, limita los reintentos fallidos a 30 s
y permite pedir una comprobación completa desde el menú de una grabación.
Las marcas de borrado se recopilan una vez por scan; no se hace un stat de
`.deleted/<id>` por cada entrada sana. Las entradas borradas no se reconstruyen.

Los pendientes llevan una prueba de propietario por sesión. En Windows usa un
evento de kernel con UUID, máquina y sesión de inicio: la desaparición del objeto
demuestra que terminaron sus propietarios; un PID reutilizado no basta. Se
rechaza recuperar/borrar un propietario activo. Otra sesión/máquina, acceso
denegado o formato antiguo se clasifican como desconocidos y requieren la acción
explícita de confirmar que se cerró el DAW. Este comportamiento se apoya en la
[vida útil de los eventos Windows](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw)
y su [namespace por sesión](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces).
En otros sistemas el protocolo no demuestra abandono y mantiene el estado
desconocido; esta pasada sólo valida Windows.

«Keep recording» convierte un pendiente completo en una entrada permanente sin
sobrescribir una entrada con el mismo ID. Conserva una marca antes de mover y
termina la promoción al reiniciar el índice si la caída ocurrió después del
movimiento. También persiste la confirmación de propietario desconocido.
El reproductor cancela cargas y retira su reader antes de editar/mover/borrar
sus archivos; la operación permite una espera breve en el worker por lectores
que aún terminan. Los pendientes PCM-only permanecen visibles con error y
acciones de reparación/borrado, con reproducción y ediciones deshabilitadas.
El cierre normal del plugin elimina únicamente el pendiente que posee y no
crea tombstone, para permitir restaurar el ID guardado en el estado del DAW.

La limpieza conserva revisión actual y anterior; sólo elimina revisiones
obsoletas con propietario demostrado huérfano y publicadas, o de más de un día.
Las activas, desconocidas y antiguas sin prueba de propietario se conservan.
Un fallo de eliminación por reader/permisos queda registrado para el siguiente
intento. Por tanto, «dos revisiones» describe la retención normal de revisiones
gestionadas, no una cota garantizada para carpetas heredadas o archivos ocupados.

`RememberStorageRecovery` y `RememberAppStorageRecovery` ejercitan 16 cierres
abruptos de procesos auxiliares por ejecución, sin ejecutar destructores:
siete puntos entre PCM, OGG, recibo, respaldo y publicación; primer pendiente
sin manifiesto; PCM-only; las tres etapas de promoción y promoción legacy con
consentimiento persistido. También prueban corrupción de PCM que sigue siendo
legible, manifest/backup inválidos, cancelación sin escritura, historial publicado,
propietarios activos/desconocidos, colisión de destino, tombstones y reader OGG
abierto mientras se reclama una revisión obsoleta. Estas caídas de proceso no
simulan pérdida de alimentación ni demuestran tolerancia a hardware defectuoso.

Las cinco suites del plugin pasan (28,51 s total). Regresión y recuperación app
pasan en Release (6,27 y 3,28 s) y con ASan (7,12 y 5,20 s), sin diagnóstico de
memoria; la regresión comprueba retirar la selección correcta y que una carga
cancelada no vuelva a conectarla. App/VST3 compilados, sin instalar binarios.
Los logs conservados de esta pasada son
[plugin](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Recovery-Plugin-2026-10-07.log>),
[app Release](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/Recovery-App-2026-10-07.log>)
y [app ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/Testing/Temporary/Recovery-App-2026-10-07.log>).

Tras integrar la recuperación se repitieron las nueve combinaciones de biblioteca:
la matriz pasó en 197,48 s. Con 10.000 entradas, primer scan tras rebuild
3.853,97/2.150,78/1.860,71 ms y scan con caché 1.697,48/1.668,84/1.542,62 ms
para 0/10/50 % ausentes. Todos los scans calientes conservaron cero lecturas JSON
y un hit por entrada. Dispatch: 80.946 intervalos, p99 3,5234 ms, p99,9 3,993 ms,
máximo 4,5425 ms. Se mantienen los límites del fixture descritos arriba: marcadores
de preview, caché del SO, operaciones de datos y dispatch, sin pintura de ListBox.
El [log actualizado](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/LibraryBenchmark-Recovery-2026-10-07.log>)
conserva las nueve filas; las cifras anteriores son históricas y no se sobrescriben.

**Integridad y hash — cierre de validación de almacenamiento del 7 de octubre.**
La captura comprueba o recupera la revisión anterior antes de reutilizar su cola:
la prueba altera PCM legible y confirma que el prefijo nuevo se mezcla con la
última cola íntegra. Se acepta el formato histórico con OGG mono y WAV estéreo;
las revisiones nuevas exigen canales coincidentes. La revisión se marca publicada
antes de notificar al observador del commit, de modo que una excepción posterior
no borre el audio publicado. La autorización de promoción queda persistida y
permite terminarla si el proceso cayó después del movimiento y el propietario
ya no puede consultarse. Se rechaza reutilizar en `.pending` un ID ya permanente.
En ese estado quedaba pendiente integrar esa situación con la asociación manual;
el ensayo de ID ya conservado desde la app descrito abajo cubre ahora ese caso.

SHA256 de JUCE solicita bloques de 64 bytes. La lectura directa provocaba una
llamada nativa por bloque; ahora un buffer de 64 KiB alimenta el hash sin cambiar
su resultado, conservando cancelación y rechazo de lecturas incompletas. En el
fixture de 32 MiB: 1.806,39 ms directo y 188,98 ms con buffer. Se lee el mismo
archivo, con caché del SO disponible y referencia directa medida primero;
son observaciones locales, no una garantía para otras unidades.

Las cinco suites del plugin pasan en 23,68 s; regresión/recuperación app en
Release: 6,04/3,30 s, y con ASan: 7,19/5,28 s, sin diagnóstico de memoria.
Logs conservados de este estado, anterior al siguiente cambio de configuración:
[plugin](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-Storage-2026-10-07.log>),
[app](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/Final-Storage-2026-10-07.log>)
y [ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/Testing/Temporary/Final-Storage-2026-10-07.log>).

También pasa el ensayo real de cuatro capturas simultáneas de 120 s/192 kHz:
180.000 callbacks, p99 0,0044 ms, p99,9 0,0196 ms y máximo 0,1100 ms;
finalización del guardado 9.263,42 ms después de capturar. Reserva de colas
64 MiB, pico de commit privado del proceso 91,86 MiB y working set máximo
71,18 MiB; memoria privada tras la retirada 5,36 MiB. WAV total 527,34 MiB y
OGG total 3,59 MiB. No hubo capturas descartadas y las reservas se liberaron.
El máximo retraso del productor del fixture fue 2,03 ms, medido por separado;
los tiempos de callback incluyen sólo el motor, no un DAW ni driver físico.
El [log](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-SustainedCapture-2026-10-07.log>)
conserva contadores, picos de proceso y 2.812 muestras de memoria. El límite
de regresión de 512 MiB sobre la base es propio del fixture, no una garantía
de memoria total del producto. El ensayo posterior al buffer de hash ahorra
trabajo de guardado frente a la pasada anterior de 27.173,4 ms, con los límites
de comparación local indicados; el scheduler también cambia el solapamiento
y el pico de memoria entre pasadas.

**Configuración y perfiles de prueba — comprobación adicional del 7 de octubre.**
Las instantáneas registran revisión, fecha y SHA256 del texto que suministra
los campos. No se reutilizan valores anteriores ante JSON inválido, ni se
confía sólo en fecha/tamaño para detectar ediciones externas. La API anterior
del motor conserva el destino fijado explícitamente; el procesador entrega
una instantánea completa para aplicar también la carpeta al preparar.

Cada banco reserva su destino y ajustes fuera de audio. El worker activa el
contexto de biblioteca correspondiente antes de procesar ese banco; las
identidades de cada biblioteca se conservan al volver. No redirige capturas
ya realizadas ni copia historial entre bibliotecas al cambiar la configuración.
La cola espera a un PCM anterior que aún drena antes de publicar un prefijo más
reciente. La revisión conserva settingsRevision/settingsFingerprint y parámetros
efectivos, también al recuperar su audio. La limpieza normal recuerda los
pendientes propios en las distintas raíces utilizadas.

`RememberConfiguration` bloquea una transacción real de metadata: crea dos
capturas anteriores en A, prepara B con 16 kHz/mono/30 s/calidad 9 y crea una
nueva. Verifica prefijos, formatos, versiones, favorito previo de B, IDs
distintos por biblioteca y vuelta a A. Prepare debe retornar en menos de
200 ms sin esperar al commit. Otra prueba crea pendientes en ambas raíces y
comprueba su eliminación al cerrar. La invalidación de configuración reutiliza
fecha/tamaño/revisión y cambia contenido. Este ensayo pasa en 0,67 s.

Los dos ejecutables de pruebas fijan un perfil dentro de su fixture antes de
crear configuración, loggers o workers. Ajustes, roots de descubrimiento,
estado de dispositivo y logs quedan aislados de los datos del usuario.
La ruta de producción sigue siendo `%APPDATA%/Remember`. Esto completa el
aislamiento del fixture, no el pipeline/CI pendiente del punto 20.

La regresión app con ese perfil pasa en Release (6,35 s), recuperación en
3,46 s; con ASan pasan en 6,87/5,66 s sin diagnóstico de memoria. App y VST3
recompilados; copia automática del VST3 desactivada. La aceptación de hosts,
dispositivos físicos, música real y los restantes puntos parciales de la tabla
sigue pendiente.

La pasada completa del plugin con estos cambios pasa sus siete suites en
158,15 s, incluido el estrés de 133,18 s. En esta pasada: 180.000 callbacks,
p99 0,0046 ms, p99,9 0,0200 ms, máximo 0,1751 ms; guardado finalizado
9.881,97 ms tras capturar. Pico de commit privado 91,74 MiB, working set
71,13 MiB y memoria privada posterior 5,15 MiB. Las cuatro capturas completas,
recibos y retirada de reservas pasan sin descartes. Se conservan los límites
del fixture y de interpretación de rendimiento descritos antes. SHA256 de
32 MiB vuelve a dar el mismo resultado: 1.927,43 ms directo / 210,96 ms con
buffer, con referencia medida primero y caché del SO disponible.

Logs de esta compilación y perfil aislado:
[plugin completo](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-Configuration-2026-10-07.log>),
[app Release](<E:/JUCE PLUGINS/Remember/App/build/Testing/Temporary/Final-Configuration-2026-10-07.log>)
y [app ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/Testing/Temporary/Final-Configuration-2026-10-07.log>).
`git diff --check` pasa. La matriz de 10.000 entradas citada antes corresponde
al estado anterior al aislamiento del perfil; no se repitió en esta pasada.

En el estado de esa pasada, el selector de output del editor aún consultaba y
guardaba desde el hilo de mensajes; la asociación manual y el ID ya conservado
desde la app seguían pendientes. Los cambios siguientes cubren esos casos.
Los BuildInfo de esta compilación
indican hash Git `unknown`; la procedencia/versionado y el pipeline de release
siguen pendientes en 20. No se han instalado ni publicado estos binarios.

**Asociación manual y ajustes del editor — validación adicional del 7 de octubre.**
El plugin permite seleccionar un archivo existente, conservar permanentemente
una grabación sin archivo y volver a detección automática. XML y metadata
conservan la decisión explícita. La UI muestra origen y categoría de evidencia;
«direct», «inferred», «explicit» y «unknown» no son probabilidades calibradas.
Una ruta manual sin verificar bloquea capturas nuevas; un título diferente por
sí solo no sustituye una selección manual. Una ruta directa del host puede
establecer un guardado por sustitución o un Save As. Sin esa ruta, cambiar ID y
contenido exige seleccionar el archivo de nuevo. El inventario conserva su TTL
de 30 s para candidatos nuevos y el rechazo conservador de homónimos.

`RememberAssociation` verifica conservar/renombrar/cerrar/restaurar una idea,
conservación de su cola al regrabar, selección de archivo después de grabar,
traslado por identidad y rechazo de una ruta reutilizada. Un Save As con dos
archivos del mismo nombre queda ambiguo y la selección manual resuelve el
destino sin alterar el original. También cubre colisiones, retry, acciones con
ID obsoleto y cierre después de un Keep fallido: el audio queda recuperable.
Reutiliza un ID que la app ya había conservado aunque el XML anterior no tenga
el modo manual. Asociar una idea conservada a un proyecto con preview reutiliza
esa entrada y mezcla su prefijo, preservando la cola y la idea original sin
crear otra asociación al mismo archivo.

Save As usa el pipeline de revisiones para publicar recibos con el ID nuevo;
no copia recibos del propietario anterior. La promoción explícita de pending
exige el token exacto del escritor, marca autorización antes de mover y no
reemplaza un destino ocupado. Tres nuevas salidas abruptas del escritor comprueban
marcado, movimiento y finalización. La suite de recuperación incluye ahora
19 interrupciones reales de proceso contando los escenarios anteriores;
no simula un corte eléctrico ni certifica almacenamiento físico defectuoso.

Una carrera de restauración permitía empezar a procesar una captura antes de
aplicar su contexto guardado. El worker comprueba ahora la generación aplicada
y publica la confirmación de conservación después de preparar el destino.
El render del editor real se guardó en
[association-editor.png](<E:/JUCE PLUGINS/Remember/Plugin/build/artifacts/association-editor.png>)
y se revisó: nombre, origen, botón y estado quedan dentro de sus bounds.
Es una captura de componente JUCE, no una interacción con un DAW o chooser real.

El editor carga y guarda output en un worker finito. Publica el resultado por
atomic; la UI lo consulta sin callbacks desde el worker. Cierre no espera a
disco y el servicio de retirada retiene job, módulo y handle de hilo hasta
finalización nativa. El handle se captura antes de dejar ejecutar incluso una
lectura rápida. Se termina una escritura ya aceptada aunque editor/procesador
se hayan cerrado. El feedback permanece entre timers; una selección fallida
conserva la configuración anterior y un retry exitoso elimina ese error.
La UI explica que la carpeta nueva se aplica al preparar/recargar el plugin.

`RememberConfiguration` mantiene una transacción real de configuración durante
la selección: dispatch sigue vivo, la selección retorna en 0,1642 ms y cierre
en 0,0582 ms en la pasada final. Tras cerrar se confirma la escritura, preservando
45 s/calidad 9/mono y un campo ajeno. También pasa la conservación del error,
retry y 20 cierres inmediatos durante lectura de ajustes. Son tiempos del
fixture Windows local, sin certificado de latencia en un host real.

App y VST3 compilados; el build final del plugin no emite las advertencias de
captura de lambda observadas en una pasada intermedia. Las ocho suites del plugin
pasan en 165,55 s: asociación 7,68 s, regresión 12,93 s, configuración 1,01 s,
recuperación 3,84 s, estrés 132,65 s, análisis 3,08 s, matriz 4,01 s y carga 0,32 s.
El estrés comprueba 180.000 callbacks: p99 0,0050 ms, p99,9 0,0218 ms y máximo
0,1248 ms; guardado terminado 9.328,41 ms después de capturar. Pico de commit
privado 111,95 MiB, working set 83,14 MiB y memoria privada tras retirada
5,36 MiB. Reserva de captura 64 MiB; cuatro archivos PCM completos, recibos,
ausencia de descartes y liberación de reservas pasan. Se conserva la distinción
entre tiempos del motor, scheduler del fixture y plazo completo de un DAW.
La matriz corta tiene 20.343 callbacks: p99 0,0033 ms, p99,9 0,0112 ms y máximo
0,0167 ms; exportación acelerada de 15 s/192 kHz termina en 1.080,43 ms.

Regresión/recuperación de app pasan en Release (6,18/4,88 s) y con ASan
(6,94/7,20 s), incluida promoción explícita interrumpida, sin diagnóstico de
memoria. Esas dos configuraciones se ejecutaron simultáneamente y sus duraciones
no se usan para comparar rendimiento. ASan sigue cubriendo los tests de app,
no este editor/VST3 dentro de un DAW.
Logs preservados:
[plugin completo](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-Association-Settings-2026-10-07.log>),
[app Release](<E:/JUCE PLUGINS/Remember/App/build/Final-Association-2026-10-07.log>)
y [app ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/Final-Association-2026-10-07.log>).
`git diff --check` pasa. No se instalaron ni publicaron binarios. La matriz de
10.000 entradas no se repitió en esta pasada; sus cifras anteriores no se
presentan como medición de esta compilación.

El objetivo completo sigue activo: quedan los puntos parciales de la tabla.
Entre el trabajo de código pendiente están pruebas espectrales/clipping y caché
de características del punto 15, distribución/procedencia/CI del 20, rutas de
error del 17 y aceptación completa de UI y grandes conjuntos de tags del 16/21.
En ese estado también quedaban por revisar las consultas de configuración del
procesador al construir y guardar/restaurar estado. El cambio siguiente cubre
esas rutas; preparación conserva la consulta síncrona de una instantánea actual.
Música real etiquetada, discos/dispositivos físicos y smoke tests de DAWs también
siguen pendientes. La implementación del punto 13 no cierra esas validaciones.

**Configuración en callbacks del procesador — validación adicional del 7 de octubre.**
Construcción encola la carga de output en el Core, junto con inicialización del
logger. Guardar estado copia el destino solicitado junto a identidad/asociación
bajo un lock breve y serializa ese snapshot: no relee config.json. Restauración
invalida primero la generación de audio y encola la lectura de ajustes actuales;
el destino de un XML antiguo no sustituye la configuración compartida.

Cada cambio explícito del destino incrementa una revisión bajo projectLock.
Una lectura iniciada antes de una selección/preparación más reciente no puede
sobrescribirla. I/O ocurre fuera de ese lock. Los bancos siguen fijando formato,
destino y fingerprint al preparar. El worker no confirma una idea restaurada
ni publica su audio antes de aplicar el contexto de restauración. Al cerrar,
Core/módulo/lectura permanecen retenidos hasta la finalización nativa.

Los fixtures usan el lector real de configuración temporal y retienen el
snapshot exacto que leyó. Mientras está retenido, cambian el archivo y llaman
a prepareToPlay. En construcción, la captura termina en la biblioteca nueva;
la lectura anterior no modifica el destino. En restauración, una idea de 2,4 s
conserva su UUID y cola tras cambiar a 16 kHz/mono y regrabar 1,2 s. Su metadata
registra exactamente el fingerprint y 45 s/calidad 9 de la preparación nueva,
sin crear una entrada en la biblioteca de la lectura obsoleta. Cierre durante
lectura retenida retorna y la retirada libera después todos los recursos.

En la pasada final: constructor 0,2073 ms, guardar estado 0,0732 ms, restaurar
0,0284 ms y cierre 0,0824 ms. Se comprueba además que el lector no corre en el
hilo de mensajes. Son retenciones controladas del fixture Windows; no tiempos
de una unidad física bajo carga ni garantía de callbacks en un DAW real.

Las siete suites cortas pasan en 36,97 s: asociación 8,59 s, regresión 14,09 s,
configuración 1,36 s, recuperación 3,93 s, análisis 3,11 s, matriz 5,54 s y carga
0,32 s. VST3 y tests recompilados; copia automática desactivada. El
[log completo](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-Processor-Settings-2026-10-07.log>)
conserva los escenarios y cifras. `git diff --check` pasa. No se repitió el estrés
de 120 s ni la matriz de biblioteca; el callback de audio y los pipelines de
resampling/análisis/codec no cambiaron en esta pasada. Las cifras previas de
esas pruebas conservan su fecha y alcance.

prepareToPlay sigue leyendo sincrónicamente una instantánea coherente y reciente,
como frontera de configuración; no se sustituyó por una caché que pudiera perder
ediciones externas. La lectura durante preparación y el logger usado desde
createEditor/prepare requieren revisión adicional si se pretende eliminar todo
I/O de esos puntos. La app y sus tests no cambiaron en esta pasada; sus resultados
Release/ASan anteriores siguen documentados con su alcance.

Antes de dar el objetivo por terminado: releer íntegramente el informe,
contrastar sus 22 puntos y las pruebas de aceptación contra el código actual,
compilar app/VST3/tests y registrar resultados y limitaciones reales.

**Calidad de audio y coste del pipeline — validación del 7 de octubre.**
La medición previa reprodujo dos defectos del punto 15. Un cuadrado de 997 Hz
con pico PCM de 0,99 generaba OGG estéreo q2 con pico decodificado de 1,69439.
Un seno de pico 1,5, ya atenuado antes del codec, seguía llegando a 1,08483.
Al convertir de 192 kHz a 22,05 kHz, un seno de 13,23 kHz dejaba un alias a
8,82 kHz de −5,06953 dB. El filtro de dos polos tampoco mantenía plana la banda
útil: a 48 kHz, 8,82 kHz perdía 2,11571 dB.

El codificador ahora decodifica el OGG completo antes de publicar. Si alguna
muestra supera ±1, reduce la ganancia y codifica otra vez desde el mismo WAV
float, con un máximo de tres intentos. Cada intento reinicia y trunca sólo su
archivo candidato privado, evitando concatenar streams OGG al reutilizarlo.
Lecturas fallidas, longitud/canales incorrectos o muestras no finitas impiden
el commit. La revisión previa permanece disponible. Decodificación y reintentos
comparten el permiso de trabajo pesado; siguen fuera del callback de audio.
Metadata registra `previewGain`, `previewDecodedPeak`, `previewGainPasses` y
`previewGainPolicy=decoded-sample-peak-guard-v1`; recuperación restaura los cuatro
campos de la misma revisión de audio y conserva favoritos/organización actuales.

| Caso estéreo de 1,2 s a 48 kHz | Pico OGG anterior | Pico OGG corregido | Ganancia final | Intentos |
|---|---:|---:|---:|---:|
| Seno 1,5 / q2 | 1,08483 | 0,987017 | 0,590198 | 2 |
| Cuadrado 0,99 / q2 | 1,69439 | 0,989282 | 0,578380 | 2 |
| Seno 1,5 / q4 | 1,03140 | 0,959848 | 0,620777 | 2 |
| Cuadrado 0,99 / q4 | 1,63208 | 0,978793 | 0,600463 | 2 |
| Seno 1,5 / q9 | 0,999061 | 0,999061 | 0,653333 | 1 |
| Cuadrado 0,99 / q9 | 1,10726 | 0,986950 | 0,885067 | 2 |

`RememberPreviewQuality` verifica doce casos mono/estéreo, las muestras completas
del PCM exactamente iguales a la captura, transparencia del input del DAW,
picos OGG finitos ≤1 y seis recuperaciones tras corromper deliberadamente el
OGG nuevo. Los picos de los doce casos quedan entre 0,959848 y 0,999061.
Este guard controla el pico de las muestras decodificadas; no mide true peak
entre muestras ni certifica la salida de un dispositivo que vuelva a remuestrear.
Las previews antiguas se conservan; una nueva captura publica el nuevo pipeline.

El resampler de JUCE de dos polos se ha sustituido por sinc centrado con ventana
Blackman. Cutoff: 95 % del menor Nyquist. Radio: 64 muestras multiplicadas por
max(1, ratio de entrada/salida). Tasas racionales habituales usan fases exactas;
otras interpolan fases de una tabla acotada. Coeficientes ≤8 MiB y caché de
entrada de 16.384 muestras más el soporte del kernel; ratio aceptado entre
1/64 y 64, tasas finitas entre 1 y 768.000 Hz. Preparación de coeficientes y
lectura comprueban cancelación. Endpoints se rellenan con cero; la muestra de
salida n corresponde a n·rateEntrada/rateSalida sin añadir retardo del filtro.
A igual frecuencia se copia exactamente. Archivo y memoria comparten conversor,
con canales independientes. La metadata nueva marca `analysisVersion=3`.

`RememberResampling` comprueba banda útil a 1/8,82 kHz y rechazo de tonos a
13,23/19/30 kHz al bajar a 22,05 kHz en las cinco tasas, con tolerancia de banda
0,02 dB y rechazo mínimo de 75 dB. El peor error de banda medido es
0,0000611228 dB y el peor alias −107,058 dB. Incluye las 25 combinaciones de
44,1/48/88,2/96/192 kHz, 25 impulsos mono centrados, muestras distintas por
bloque sin cambiar resultado, duplicación de mono, headroom, equivalencia exacta
archivo/memoria, EOF, fases interpoladas y cancelación. El peor error interior
frente a la señal calculada independientemente es 6,59892·10⁻⁷.
Estos tonos y puntos espectrales no sustituyen un corpus musical ni una
certificación de todas las frecuencias y dispositivos.

La medición auxiliar reutiliza las mismas señales y el binario Release actual:
convertir un segundo estéreo de 192 kHz tarda aproximadamente 15 ms frente a
2,5 ms con el filtro previo; de 44,1 kHz tarda 2,1 frente a 0,65 ms. Es el coste
de corregir la calidad, en workers. No se presenta como mejora de velocidad.
El fixture no está dentro de un DAW y dispone de caché del SO.
Los datos completos están en
[baseline](<E:/JUCE PLUGINS/Remember/Plugin/build/audit/quality/PreviewQuality-Baseline-2026-10-07.txt>),
[corrección de ganancia](<E:/JUCE PLUGINS/Remember/Plugin/build/audit/quality/PreviewQuality-AfterGain-2026-10-07.txt>)
y [ganancia y sinc](<E:/JUCE PLUGINS/Remember/Plugin/build/audit/quality/PreviewQuality-AfterSRC-2026-10-07.txt>).
El instrumento auxiliar mide; las dos nuevas suites persistentes son las que
aplican umbrales y fallan ante regresiones.

Las diez suites del plugin pasan en 188,52 s: calidad 2,39 s, remuestreo 7,32 s,
asociación 8,74 s, regresión 14,93 s, configuración 1,48 s, recuperación 4,26 s,
estrés 140,34 s, análisis 3,92 s, matriz 4,77 s y carga 0,32 s.
El estrés vuelve a capturar cuatro instancias durante 120 s a 192 kHz:
180.000 callbacks, p99 0,0057 ms, p99,9 0,0290 ms y máximo 0,3529 ms. Guardado
termina 16.979,1 ms después de capturar; reserva de captura 64 MiB, pico de
commit privado 92,77 MiB, working set 72,02 MiB y memoria privada tras retirada
5,60 MiB. WAV/OGG completos, recibos, cero descartes y retirada de reservas pasan.
La pasada anterior guardaba en 9.328,41 ms; esta nueva pasada incluye validación
OGG y el nuevo filtro. Son ejecuciones distintas, sin aislamiento de variación
del sistema: se informa del mayor coste observado y no de una comparación causal
controlada de cada fase. La exportación acelerada de 15 s/192 kHz termina en
1.156,16 ms. No certifica el presupuesto completo del audio dentro de un host.

App y VST3 compilados, instalación automática desactivada. App pasa sus tres
suites en Release (8,20/10,64/265,77 s) y con ASan (17,45/12,37/443,00 s),
incluida la matriz 100/1.000/10.000 con 0/10/50 % de rutas ausentes, sin diagnóstico
de memoria. App usa el cambio compartido de recuperación; no incorpora el
conversor del plugin en su callback de reproducción. Las dos configuraciones de
app y otras compilaciones se solaparon: sus tiempos, especialmente los de la
matriz, no se usan para concluir regresión o mejora de rendimiento. ASan cubre
app/tests, no VST3 en un DAW. Logs completos:
[plugin](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-AudioQuality-2026-10-07.log>),
[app Release](<E:/JUCE PLUGINS/Remember/App/build/PreviewGain-2026-10-07.log>) y
[app ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/PreviewGain-2026-10-07.log>).
`git diff --check` pasa. No se instalaron ni publicaron binarios.

El punto 15 sigue parcial: falta la caché de características por bloques para
reducir trabajo sobre una cola intacta, después de estas correcciones. El resto
de pendientes permanece en la tabla, incluidos distribución/procedencia/CI,
rutas de error y aceptación de UI, música real y dispositivos/DAWs. El objetivo
completo continúa activo; esta validación no cierra los 22 puntos del informe.

**Caché de características por ventanas — validación del 7 de octubre.**
El punto 15 ya incluye la caché propuesta, con una instancia por worker de
análisis. Guarda chroma normalizado por ventana de 8.192 muestras/hop 2.048 y
energía, magnitud y flux por ventana de 2.048/hop 256, a 22,05 kHz. Mantiene los
canales separados y vuelve a estimar tonalidad, tempo y confianza globales;
no reutiliza una etiqueta musical final. El flujo completo sin caché usa la
misma extracción/agregación de características, identificado por versión 4.

La cola se reutiliza únicamente cuando coincide el UUID de la revisión previa
publicada y su SHA256 de PCM verificado, además de frecuencia, canales y
longitud. Commit y recuperación devuelven el checksum que ya calcularon/verificaron
para el recibo; no se introduce otro hash completo para consultar la caché.
El encoder publica una confirmación mediante atomic release después de
commitAudio. El worker de análisis adquiere esa confirmación antes de leer el
checksum. Un análisis cancelado o una revisión no publicada no autoriza reuse.
Restauración en un Core nuevo comienza sin caché; un cambio de formato/duración,
checksum o revisión provoca cálculo completo.

La frontera reutilizable incluye el soporte futuro del sinc y las ventanas
solapadas. Flux exige que también su FFT anterior esté en la región intacta.
El lector conserva la fuente completa para el lookahead pero normaliza sólo
el prefijo requerido por las ventanas que deben recalcularse; el nuevo final
de ese buffer no se convierte en un endpoint artificial del filtro. Se verifica
la cabecera del PCM frente a formato/longitud esperados y se conserva la preview
anterior si no coincide. Las ventanas exactamente silenciosas omiten FFT de
cero, conservando el mismo resultado de características. Merge, OGG y checksums
siguen trabajando sobre la duración completa: esta mejora ahorra análisis.

Los vectores retenidos están limitados a 512 KiB por Core y 16 MiB compartidos.
Reserva global por CAS, liberación al sustituir/destruir y falta de presupuesto
con resultado completo sin retención. No retiene PCM, reader ni decoder.
La retirada del Core también libera sus características después de finalizar
los workers nativos. Los buffers temporales siguen dentro de los dos permisos
de trabajo pesado; la cifra de vectores no es un límite de memoria total del
proceso. Metadata/logs registran muestras normalizadas, ventanas calculadas y
reutilizadas y bytes retenidos. Recuperación restaura esos siete campos,
incluida versión, de una sola revisión.

RememberFeatureCache prueba prefijos musicales distintos con crossfade en las
cinco frecuencias y compara **cada** ventana de chroma y los tres valores del
flux, exactamente, contra extracción completa del WAV nuevo. Compara también
key, BPM, ambas confianzas, correlación/margen, alternativa, variación y
estimaciones inicial/final sin tolerancia numérica. Publicación pendiente,
checksum/UUID distintos, frecuencia/canales/duración, falta de características
BPM tras usar tempo del host, cabeceras incorrectas, cancelación y retry pasan.
El pipeline real confirma el checksum del commit y la recuperación contra el
WAV leído, utiliza la caché tras una captura parcial y vuelve al cálculo
completo al recuperar una revisión distinta o cambiar a mono/96 kHz.
La corrupción del PCM nuevo restaura todos los campos de análisis de la
revisión anterior y conserva una preview coherente.

| Frecuencia | Análisis con caché (ms) | Completo (ms) | Muestras normalizadas | Ventanas key nuevas/reutilizadas | Ventanas BPM nuevas/reutilizadas |
|---|---:|---:|---|---|---|
| 44,1 kHz | 18,63 | 168,68 | 34.816 / 264.448 | 14 / 112 | 106 / 920 |
| 48 kHz | 21,82 | 171,55 | 34.816 / 264.448 | 14 / 112 | 106 / 920 |
| 88,2 kHz | 23,43 | 213,01 | 34.816 / 264.448 | 14 / 112 | 106 / 920 |
| 96 kHz | 28,35 | 220,72 | 34.816 / 264.448 | 14 / 112 | 106 / 920 |
| 192 kHz | 45,50 | 301,38 | 34.816 / 264.448 | 14 / 112 | 106 / 920 |

Son pares de análisis consecutivos locales de una fuente de 12 s, con prefijo
reemplazado de 1,21347 s: 86,83 % menos muestras normalizadas. Cada caché retiene
30.672 bytes en ese caso. Incluyen lectura/normalización, extracción y estimación;
no codificación/merge/recibos ni tiempo completo de guardado. Cached se mide
primero y la caché del SO está disponible. No se afirma una aceleración equivalente
del guardado de la preview ni del callback de un DAW.

El fixture crea 58 instancias reales sobre PCM silencioso de 120 s, para llenar
el presupuesto. Pico de vectores 16.723.584 bytes (15,95 MiB); cuatro instancias
completan su análisis sin retener caché. El contador vuelve a su valor inicial
al destruirlas y permite nueva reserva. La pasada final tarda 6.812 ms para ese
fixture. No se confunde ese contador con working set/commit privado de Windows.

La validación estricta de cabecera detectó una inconsistencia de media muestra:
60.000 frames a 48 kHz deben normalizarse a 27.563 a 22,05 kHz, pero dividir por
el ratio ya redondeado producía 27.562. RememberCaptureMatrix reprodujo el error
con bloque 32. Lector y metadata ahora usan convertedLength, con la misma fórmula
de duración. RememberResampling verifica además 1,25 s y equivalencia exacta
archivo/memoria. La matriz completa vuelve a pasar.

Las once suites del plugin pasan en 207,97 s: calidad 2,74 s, remuestreo 7,40 s,
caché 20,17 s, asociación 8,39 s, regresión 13,95 s, configuración 1,44 s,
recuperación 4,00 s, estrés 141,56 s, análisis 3,09 s, matriz 4,86 s y carga
0,33 s. El estrés de cuatro capturas simultáneas de 120 s/192 kHz vuelve a
verificar WAV/OGG completos, recibos, cero descartes y retirada. Sus 180.000
callbacks tienen p99 0,0060 ms, p99,9 0,0243 ms y máximo 0,5726 ms; guardado
termina 18.015,8 ms después de capturar. Pico de commit privado 92,75 MiB,
working set 72,18 MiB y memoria privada tras retirada 5,27 MiB; rings de captura
64 MiB. Son primeras capturas completas, sin cola previa reutilizable: este
estrés valida el pipeline y su límite, sin medir un ahorro de caché parcial.
Las ejecuciones anteriores siguen siendo mediciones distintas con su fecha,
sin atribuir variación del scheduler a un cambio causal del callback.
La exportación acelerada de 15 s/192 kHz termina en 1.199,41 ms.

VST3/app compilados; copia automática desactivada. App pasa regresión y
recuperación en Release (7,65/20,47 s) y ASan (10,74/27,27 s), sin diagnóstico
de memoria. Se solaparon entre sí y con compilación, por lo que sus tiempos no
sirven como comparación de rendimiento. El benchmark de biblioteca no se
repitió: código del índice/reproductor y su algoritmo no cambiaron; sus cifras
anteriores mantienen fecha/alcance. ASan cubre la app/tests, no este VST3 en DAW.
Logs preservados:
[plugin completo](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-FeatureCache-2026-10-07.log>),
[app Release](<E:/JUCE PLUGINS/Remember/App/build/FeatureCache-2026-10-07.log>) y
[app ASan](<E:/JUCE PLUGINS/Remember/App/build-asan/FeatureCache-2026-10-07.log>).
Después de la suite completa se reforzó la comprobación de todos los campos
recuperados y se recompiló/revalidó RememberFeatureCache; no cambió producción.
Su log adicional se conserva en
[campos recuperados](<E:/JUCE PLUGINS/Remember/Plugin/build/Testing/Temporary/Final-FeatureCache-RecoveryFields-2026-10-07.log>).
git diff --check pasa. No se instalaron ni publicaron binarios.

El punto 15 está implementado y validado en fixtures Windows. Música real
etiquetada sigue pendiente en 14 y dispositivos/DAWs en 18/19. La tabla conserva
el resto del alcance: distribución/procedencia/CI del 20 y rutas de error/UI
siguen requiriendo trabajo. El objetivo global continúa activo.

**Distribución, procedencia y validación nativa — 7 de octubre.**

VERSION es la fuente de versión para CMake y el instalador. La información
compilada incluye commit completo, estado clean/dirty, build ID y SHA256 de los
archivos de fuente. App y VST3 conservan una línea de procedencia en sus binarios;
el pipeline comprueba esa línea, además de los JSON generados. Git se resuelve
por ruta absoluta incluso al construir sin PATH. El VST3 no se copia al sistema
por defecto. El fabricante ya tiene nombre/URL reales y los CIDs de componente
y controlador conservan sus valores anteriores.

Los scripts PowerShell ejecutan builds nuevos, las doce suites del plugin y las
tres de app, y pluginval sobre el bundle que se empaquetaría. Conservan resultados
JUnit, logs completos, imágenes de fixtures y hashes de cada archivo del payload.
No hay selección por orden de un wildcard. El publicador exige instalador y
manifiesto explícitos, source limpio sin cambios desde el build, evidencia CTest
completa, procedencia compilada, hashes y tags locales/remotos del mismo commit.
Una compilación -AllowDirty -SkipPackage nunca es publicable. El instalador sólo
consume el payload suministrado; incluye procedencia y conserva el AppId existente.

45 contratos de release pasan: edición del mismo tamaño, fuentes nuevas/borradas,
ignorados, versiones, tags de otro commit, pruebas ausentes/fallidas/skipped,
artefactos alterados, archivos de bundle no listados, build ID ajeno y salida no
cero de herramientas. La ejecución de procesos espera el ExitCode real; se
reprodujo y corrigió el caso de ejecutable GUI que no fija LASTEXITCODE en
PowerShell. Estos contratos usan un repositorio temporal, no el Git de Remember.

El VST3 actual pasó pluginval 1.0.4, fijado por SHA256, con strictness 5, editor,
estado, 25 combinaciones 44,1/48/88,2/96/192 kHz y 32/64/128/512/2048, y callbacks
de tamaños variables. La herramienta omite su opción de validator SDK externo;
no se presenta esto como certificación de Steinberg ni de DAWs reales.
[Log nativo](<E:/JUCE PLUGINS/Remember/out/native-vst3-20261007-fixed/pluginval-detail.txt>).
El perfil opt-in marcado redirige la DLL antes de construir el procesador; siete
casos comprueban que rutas relativas, marker incorrecto y destinos externos se
rechazan sin caer en el perfil normal. ASan de app/recuperación pasó 7,39/6,62 s
(14,03 s), con el cambio de inicialización del perfil, antes del ajuste posterior
del nombre de los fixtures. No se repitió aquí su benchmark de biblioteca.

El primer pipeline se detuvo ante un fallo real de recuperación. Con el mismo
binario en un cwd corto pasaron todas las interrupciones; en el cwd nuevo el
owner tenía 258 caracteres y su temporal sobrepasaba el límite Win32. Se mantienen
los UUID completos y todas las fases, acortando el prefijo de fixtures y usando
un cwd separado corto bajo out/t. Una repetición en el directorio profundo pasó
la suite completa de recuperación en 4,01 s. Esto corrige el entorno de prueba;
el soporte/diagnóstico de rutas largas en producto queda registrado en 17.
El pipeline no emitió manifiesto válido después de aquel fallo.

El workflow tiene jobs de ambos targets Release y de app ASan, dependencias y
actions fijadas, permisos de lectura y retención de diagnósticos; no publica.
El YAML se analizó con el parser oficial PyYAML. No se ha ejecutado en GitHub.
La siguiente ejecución completa se guarda en
out/release-validation-2026-10-07-final; sólo su resultado terminal y manifiesto
pueden confirmar las gates. Sigue pendiente el instalador real (ISCC no está
disponible en las rutas comprobadas), instalación/desinstalación y workflow remoto.
El objetivo global sigue activo y la tabla mantiene todos los pendientes.

**Empaquetado verificable y repositorio de publicación — 7 de octubre**

El pipeline completo anterior terminó correctamente con 12 suites del plugin,
3 de la app y pluginval 5 sobre el VST3 staged. Sus hechos, tiempos y hashes
permanecen en `out/release-validation-2026-10-07-final/VALIDACION.md` y su manifiesto.
Son un snapshot de esas fuentes; no se reetiquetan binarios antiguos con la
procedencia de los cambios posteriores.

Se ha compilado el instalador de ese payload con Inno Setup 6.7.3 portable,
sin instalar el compilador en Windows. innounp 2.67.11 recupera los cinco
archivos exactos de app, VST3, notas y procedencia. El script de validación
comprueba sus bytes, AppId estable, arquitectura x64 y versión numérica de
los recursos. No se ha ejecutado el instalador de Remember. La prueba detectó
el relleno de espacios de FileVersion de Inno; se comprueban los cuatro
componentes numéricos del recurso, tanto de archivo como de producto.

`dependencies.json` fija los archivos del compilador y los hashes de ambas
distribuciones. El bootstrap emplea el modo portable oficial; CI deja de
depender de la versión de Inno instalada en el runner. El publicador exige un
solo destino de fetch/push, lo compara con el manifiesto y pasa --repo a gh.
No se han creado tags, pushes ni releases. -AllowDirty puede generar un
instalador revisable, siempre con publishable=false; no existe override de
publicación para desarrollo. Las mismas validaciones de tests, bytes y
procedencia permanecen obligatorias.

74 contratos locales pasan, incluidos destinos Git diferentes/múltiples,
GH_REPO ajeno, runtime de compilador distinto, desarrollo sin publicar,
extracción corrupta/extra y metadata de setup incorrecta. El siguiente build
nuevo con empaquetado se ejecuta en `out/release-validation-2026-10-07-packaged`;
su informe terminal se guardará allí sin modificar las fuentes durante la
validación. El workflow remoto y la instalación/actualización/desinstalación
en un Windows aislado continúan pendientes; la extracción no sustituye esas
aceptaciones. Los otros pendientes de la tabla de 22 puntos se mantienen.

**Pipeline empaquetado y corrección de UI — 7 de octubre, validación terminal**

El build `20261007T201640Z-a64f0294` termina con código 0 en
`out/release-validation-2026-10-07-packaged`. SHA256 de fuentes:
`de4134be79c972595809edeed7426d115e52de4caf3a36a4ebf251169b17da2d`.
Las 12 suites del plugin pasan (209 s) y las 3 de la app pasan (305 s:
regresión 6,602 s, recuperación 4,626 s, benchmark 294,545 s). pluginval 5
valida el VST3 staged; Inno 6.7.3 compila y innounp 2.67.11 compara los cinco
archivos de instalación. El manifiesto terminal verifica 25 artefactos y el
publicador rechaza el candidato de desarrollo antes de consultar tags remotos.
Sus datos y hashes completos están en `out/release-validation-2026-10-07-packaged/VALIDACION.md`.
Es un snapshot anterior a los cambios de UI siguientes; no representa sus
fuentes ni incorpora su corrección. No se ha ejecutado setup de Remember.

Una nueva regresión selecciona realmente Sort by mediante teclado en el popup
de carpetas, destruye el sidebar y deja ejecutar el callback de JUCE. Antes de
corregir, ASan registra heap-use-after-free en chooseSortMode desde la lambda
que retenía &owner. Se conserva `App/build-asan/UI-Before-Full.log`. La lambda
captura ahora SafePointer y la misma interacción pasa. El código C1902 al
compilar ASan dentro del sandbox se resolvió al ejecutar el mismo build fuera
con permiso automático: no se alteraron los archivos de Visual Studio.

La prueba de UI crea 1.102 rutas y un inventario de 512 tags con variantes de
mayúsculas. Recorre el árbol, comprueba un solo conteo por entrada/ancestro,
selección tras reconstruir, dos scrolls y alcance de los últimos botones.
Usa el diálogo real para añadir un tag normalizado, elegir una sugerencia,
retirar un chip mediante callbacks diferidos y confirmar sin perder el tag de
630 caracteres. La publicación ahora incluye entradas, carpetas, tags y orden;
los tags se deduplican/ordenan en el worker. Se conservan controles y anchos de
texto al editar, en lugar de recrearlos. El texto pintado usa fuente fija y
elipsis, con tooltip/identidad completos. La revisión visual detectó un recorte
de tags cortos; se corrigió el redondeo de avances fraccionarios.

Release antes: apertura 78,6502 ms, edición 16,2537 ms. Último Release después:
apertura 60,4350 ms, edición 0,5604 ms. Son ensayos individuales del fixture,
no garantías de latencia ni porcentajes de CPU. En ASan, las dos suites de
regresión/recuperación pasan (9,50 y 6,21 s) después de corregir la interacción;
Release pasa las dos (6,86 y 4,77 s). Tras corregir únicamente el writer de PNG
y el ancho de texto, RememberAppRegression vuelve a pasar en 6,71 s y app/tests
compilan. La apertura sigue mereciendo seguimiento en conjuntos más grandes.

Las capturas PNG se sobrescriben correctamente: FileOutputStream de JUCE abre
al final de archivos existentes; la fixture ahora reposiciona/trunca para no
adjuntar otra imagen a la anterior. Se verificó una sola cabecera PNG por
archivo y se inspeccionaron los renders actuales de tags/carpetas. Una fixture
inicial ocultaba el diálogo, lo cual JUCE interpreta como cierre: se sustituyó
por alpha=0 manteniendo su vida útil, y se utiliza SafePointer para el contenido.
Esos dos fallos pertenecían a la prueba y no se confunden con el UAF del producto.

Persisten los pendientes originales de la tabla: índice ante errores/unidades
lentas, prepare/cierre durante capturas largas, loops y saturación ampliados,
corpus musical real, escena completa de bibliotecas grandes, errores de disco,
choosers, dispositivos/DAWs, CI remoto y aceptación de instalación. El objetivo
de 22 puntos permanece activo; estos resultados no lo dan por completado.

**Índice con notificaciones, escena completa y promoción — 8 de octubre**

El índice distingue una biblioteca vacía de un fallo de configuración o de
enumeración. Config vacía, inválida, relativa o inaccesible y raíz offline
conservan la última publicación de entradas, carpetas y tags; la lista muestra
un aviso con ruta completa en tooltip. Se probaron lectura con handle Windows
exclusivo, obstrucciones de .pending/.deleted, reparación/retry y cambio a una
biblioteca realmente vacía. Un array vacío de raíces desactiva el descubrimiento.

ReadDirectoryChangesW vigila config, previews y raíces, con debounce de 200 ms,
máximo de ráfaga de un segundo y polling de respaldo a 30 s. Config filtra su
nombre para evitar que el log genere refrescos. Hay como máximo 16 watchers con
buffers de 32 KiB; la cancelación nativa termina antes de liberar sus recursos.
Se probaron cambios externos sin Refresh con la app oculta (~807 ms en el último
Release), reemplazo/eliminación de raíz, una ráfaga de 900 archivos y 100 cierres
con peticiones pendientes. Handles antes/después: 192/192 Release y 225/225 ASan.
La ráfaga no demuestra por sí sola que se forzara overflow del buffer nativo.

La escena usa MainComponent, sidebar, lista, búsqueda y reproductor reales con
100/1.000/10.000 entradas. Comprueba última fila, 11 controles reciclados,
búsqueda, carpetas/favoritos, hotkey F, selección por ID tras reordenar y cierre.
Los renders 640x480 y 1024x768 se revisaron visualmente. Se habilitaron ventanas
de tooltip compartidas en la app y parentadas al editor del plugin, y se corrigió
el redondeo que truncaba nombres cortos. Los marcadores OGG de esta escena
validan existencia y UI, no decodificación; audio y dispositivo tienen fixtures
independientes. No sustituye el arranque del ejecutable ni choosers/DAWs reales.

La regresión de asociación ahora espera Done y comprueba manifest único, OGG y
PCM de 9.600 muestras a 8 kHz. Reprodujo un Pending persistente: la promoción
ya había escrito los campos, por lo que updateMetadata no tenía nada que guardar
y no cambiaba el estado. Una promoción confirmada actualiza Done directamente.

Resultados terminales: AppRegression final 220,602 s Release y 224,753 s ASan,
incluidas creación/limpieza de 10.000 fixtures; publicación/dispatch de 10.000
filas 22,10 ms y filtro de carpeta 5,84 ms en ese Release. Las tres suites app
anteriores a los últimos cambios de escena pasaron en 423,73 s; seis suites del
plugin pasaron en 61,73 s después de corregir Done. pluginval 1.0.4 strictness 5
validó el VST3 local. Son ensayos locales, algunos concurrentes, sin porcentajes
de mejora de CPU ni garantías físicas. Logs/JUnit y alcance se registran en
`out/validation-20261008-index-scene/VALIDACION.md`. El pipeline empaquetado del
7 de octubre sigue siendo un snapshot anterior, no un paquete de estos cambios.

Continúan los pendientes de los 22 puntos, incluidos prepareToPlay, errores de
manifiestos/candidatos, prepare/cierre durante capturas largas, loops cortos,
corpus musical real, edición/menús/choosers, rutas largas, disco/driver/DAW reales,
workflow remoto, pipeline final y aceptación de instalación.

**Errores por entrada, árbol de búsqueda y logs — 8 de octubre, continuación**

Se reprodujeron dos fallos adicionales antes de corregirlos. Un manifest
confirmado corrupto eliminaba su fila (`App/build/Entry-Before.log`); una escritura
diagnóstica retenida bloqueaba el caller de prepare (`Plugin/build/Diagnostic-Before.log`).
Los logs de preparación/editor se encolan ahora en el Core retenido, sin I/O en
esos callers. La regresión bloquea el writer, mantiene dispatch, cierra editor y
processor y comprueba que el evento aceptado termine antes de retirar el worker.
La lectura de ajustes de prepareToPlay sigue síncrona y continúa como pendiente.

Cada fila ilegible conserva los campos conocidos del mismo directorio y muestra
storageError/ruta/acción; una entrada nueva dañada muestra un placeholder.
Metadatos ilegibles por sharing/permisos no se reescriben como si estuvieran
corruptos. Faltan manifests o audio: la fila tampoco desaparece. Entradas sanas
siguen publicándose y restaurar los archivos limpia la advertencia. Se prueban
manifests dañados/ausentes, handle exclusivo tras invalidar mtime, preview
ausente, tombstone y reemplazo de directorio. Los placeholders y datos legacy
sin projectId conservan identidad por metadataFile, sin inventar ID desde el
nombre de carpeta.

CandidateIndex construye un inventario nuevo y sólo sustituye el anterior tras
enumerar todas las raíces. Error de raíz/subcarpeta impide una asociación única
por título; una ruta conocida verificable sigue siendo evidencia independiente.
Cancelar un retry no limpia el error anterior. Se siguen junctions con IDs de
directorio para cortar ciclos y deduplicar aliases. Las pruebas crean dos
junctions reales y una DACL que deniega sólo enumeración; restauran la DACL y
retiran los enlaces sin recorrer sus destinos. La raíz explícita, incluso vacía,
manda también en el plugin; las rutas habituales sólo son defaults opcionales
existentes cuando no hay raíces configuradas.

El plugin conserva ID/audio ante búsqueda fallida, muestra el problema y frena
capturas no verificables. Al restaurar acceso recupera la asociación; elegir un
archivo manual verificado permite grabar aunque otra raíz continúe offline.
Se comprueban revisión sin alterar, prefijo nuevo y cola anterior intacta. La
primera ejecución ampliada detectó defaults inexistentes que antes JUCE omitía
silenciosamente; se corrigió la distinción entre raíz explícita y guess opcional.
La fixture se acotó a sus propios proyectos: incluir también su carpeta outside
permitía relocalizar correctamente el original del caso de ruta reutilizada.

Resultados terminales: 12 suites plugin pasan en 234,97 s; 3 suites app pasan en
340,73 s (138,435/3,967/198,310 s). La última escena Release de 10.000 filas mide
24,12 ms de publicación/dispatch y 6,33 ms de filtro; conserva 11 controles.
El benchmark separado recoge p99/p99,9/máximo de dispatch 3,524/3,995/4,574 ms.
Ese conjunto app es anterior a las dos comprobaciones pequeñas finales de
cancelación/ID legacy; ambas se verificaron después con --library-index en
Release y ASan, salida 0. Cien cierres dejan 231→231 handles en los dos ensayos
finales; ASan storage recovery también pasa en 7,386 s. pluginval 5 vuelve a
validar el VST3 compilado de esta tanda. Alcances/logs exactos se conservan en
`out/validation-20261008-entry/VALIDACION.md`.

El sandbox denegaba el gestor PDB de ASan (C1902) y la creación de junctions
(Win32 5). Los mismos builds/tests pasaron fuera con aprobación automática;
no se modificó Visual Studio ni permisos/enlaces externos al fixture. Los
ensayos de sandbox/outside y con carga simultánea no son comparables como una
mejora porcentual. No se ejecutó el instalador ni se publicaron recursos.

Sigue activo el objetivo completo de 22 puntos. Retirar la lectura síncrona de
prepare debe preservar la primera captura desde cero, ajustar mono/duración/
calidad/destino desde una instantánea coherente y retener bancos antiguos hasta
drenarlos; una caché por sí sola no satisface esas condiciones. Permanecen los
otros pendientes de la tabla, incluidos capturas largas con prepare/cierre,
loops, corpus real, edición/menús/choosers, config dañada/rutas largas/disco lleno,
drivers/DAWs, CI remoto y pipeline/aceptación de distribución finales.

**Preparación asíncrona y concurrencia de ajustes — 8 de octubre, continuación**

prepareToPlay ya no lee el JSON en su caller. Cada banco retiene una publicación
de ajustes de un worker independiente; captura desde la muestra cero y espera
esa versión antes de codificar/publicar. Conserva duración, promedio mono,
calidad, revisión/fingerprint y destino, también cuando llega tras haber
capturado más de la duración configurada. Los bancos anteriores drenan en su
orden y a sus propias bibliotecas. saveState usa el último destino conocido
mientras espera; no adivina un destino ni espera I/O. El editor muestra la
lectura pendiente. El overload antiguo de RecordingEngine usado por fixtures
conserva su lectura síncrona y no lo llama el procesador real.

Una regresión de asociación detectó que aplazar restauración dejaba pasar una
acción keep posterior. applyCommands conserva ahora esa barrera de orden sin
esperar en UI/audio; una prueba retenida comprueba restauración, keep/rename,
identidad y prefijo. Config dañada conserva revisión/audio, indica ruta y permite
un nuevo prepare tras repararla. Cierre con lectura retenida libera banco/contexto
y mantiene vivo el job/DLL hasta terminar su hilo nativo.

Máximo 32 jobs de ajustes retenidos por módulo, con fallo/retry explícito al
saturar y reserva hasta completar la retirada nativa. El ensayo de 40 requests
admite 32 y rechaza ocho; todos los aceptados terminan y se puede reintentar.
La prueba ASan descubrió UAF al convertir simultáneamente a UTF-16 la ruta File
del singleton: JUCE modifica el buffer interno aunque la API sea const.
readSnapshot copia File antes del I/O; cada lector convierte su propio objeto.
El log rojo se conserva y la prueba de concurrencia pasa con la corrección.

Se habilitó ASan también en Plugin, SharedCode/tests/DLL de retirada, evitando
LTO sólo en esa variante y copiando runtime. El workflow añade plugin-asan con
seis nombres obligatorios; su gate de JUnit se probó contra la publicación final.
El job remoto sigue sin ejecutar. Los 74 contratos de distribución pasan en
fixtures temporales fuera del sandbox, que bloqueaba crear su .git; el .git
de Remember no se modifica. No se ejecutó setup ni se publicó nada.

Resultados terminales: 12 suites Release pasan en 262,63 s antes de la única
corrección final de copia de ruta; asociación/configuración pasan después en
15,43 s. El conjunto final ASan de seis suites pasa en 89,20 s, incluido el caso
de 32 lectores. Hubo un timeout de regresión durante compilación concurrente;
el reintento pasó en 45,16 s y la ejecución conjunta en 44,69 s para esa suite,
manteniendo su límite de 60 s. pluginval 5 vuelve a pasar sobre el VST3 final.
Alcance, métricas, fallos y logs están en
`out/validation-20261008-prepare/VALIDACION.md`.

La lectura retenida conserva frames 0/127/119999 y exactamente 15 s mono a 8 kHz
frente a otra edición del JSON; prepare/save/cierre miden 0,172/0,022/0,022 ms
en el último Release. Es timeline acelerado, no I/O físico de 17 s. El ensayo
sostenido sí usa 120 s de reloj y cuatro instancias a 192 kHz con prepareAsync:
p99/p99,9/máximo de 180.000 callbacks 0,007/0,027/2,3165 ms y pico privado/commit
97.497.088 bytes. Había compilación/validación ASan concurrente; no acredita un
porcentaje de CPU, cortes de DAW ni una mejora porcentual contra otra tanda.
El ring/temporal asíncrono retiene estéreo y publica mono de un canal: conserva
presupuestos de memoria pero puede usar más disco temporal que un banco cuyo
Snapshot mono ya es conocido. Esa diferencia queda documentada.

El objetivo completo continúa activo; la tabla mantiene los demás pendientes
de los 22 puntos. Esta tanda no vuelve a compilar/medir la app ni sustituye la
pipeline final y las aceptaciones físicas/DAWs/distribución.

**Preparación general y capturas largas — 8 de octubre, continuación**

La preparación de dos argumentos de RecordingEngine usa ahora la misma ruta
asíncrona, con la biblioteca compartida actual. Una prueba con JSON aislado
comprueba que sustituye el destino antiguo y conserva duración configurada,
mono, calidad y fingerprint. Las fixtures que eligen destino independiente
reciben Snapshot explícito. Si los ajustes ya llegaron al reservar, se usan sus
canales/duración; si esperan, el ring/temporal conserva estéreo hasta convertirlo.
README ya describe estas rutas y el caso de timeline acelerado.

La prueba nueva publica 120 s/192 kHz, vuelve a preparar tras 75 s de recaptura
y cambia a 96 kHz/otra biblioteca/mono con ajustes retenidos. El banco antiguo
termina sin esperar al nuevo y conserva su cola; ambos mantienen configuración
y recibos propios. Después cierra tras 90 s con descubrimiento retenido, sin
alterar la revisión confirmada. Retirada nativa libera todas las reservas.
Release y ASan alcanzan 32 MiB de reservas; prepare/cierre de la última pasada
Release miden 0,212/0,203 ms. Es timeline acelerado con archivos reales, no un
disco físicamente bloqueado ni una captura de 75/90 s de reloj.

ASan superó el umbral de commit de Release al retener memoria de su allocator.
Se conservan los dos logs rojos y el diagnóstico: 639.635.456 bytes privados
de pico, 36.524.481 bytes vivos instrumentados y cero al terminar. Microsoft
documenta esa retención. El guard de commit de Release sigue igual; ASan
comprueba el mismo margen de pico en asignaciones vivas y exige retornar a
baseline + 8 MiB tras retirar workers. No se modifica ASAN_OPTIONS ni cuarentena.
La última pasada instrumentada termina en 140,334 s, con 36.574.099 bytes vivos
de pico y cero después; no equivale a detección general de todas las fugas.

Resultados terminales: 12/12 Release en 278,64 s, incluidas las capturas sostenida
y larga dentro de RememberSustainedCapture (199,19 s; límite original 300 s).
Seis suites ASan pasan en 102,55 s; el caso largo instrumentado posterior también
pasa. pluginval 1.0.4 nivel 5 confirma el VST3 de esta tanda. El workflow añade
el caso largo obligatorio y conserva su log; las seis suites, el gate JUnit,
los bloques PowerShell y el workspace se verifican localmente. CI remoto pendiente.
Logs, alcance, fallos y métricas:
[validación de esta tanda](<E:/JUCE PLUGINS/Remember/out/validation-20261008-long/VALIDACION.md>).

La captura de reloj de cuatro instancias sigue siendo de 120 s; recoge 180.000
callbacks p99/p99,9/máximo 0,0066/0,0348/0,2148 ms y pico privado/commit de
97.390.592/97.398.784 bytes. El percentil mide sólo el motor y la memoria incluye
otros subsistemas. Los tiempos no prueban una mejora porcentual ni una garantía
de CPU/audio en DAWs. Las primeras suites Release coinciden con el final del
ensayo largo ASan; no se recompilan/validan fuentes de la app en esta tanda.

Continúan los demás pendientes de los 22 puntos: loops cortos/carga extrema,
configuración dañada desde Settings, rutas largas/disco lleno/permisos de
escritura, música real, edición/menús/choosers, arranque con biblioteca grande,
drivers/DAWs, workflow remoto y pipeline/instalación finales. El informe original
se ha vuelto a consultar; no se marca terminado el objetivo completo.

**Bucles cortos, límite de un segundo y rearme — 8 de octubre, continuación**

La nueva prueba reprodujo un descarte sin aviso al parar una muestra antes de
un segundo. finishCapture añade un contador atómico de fragmentos cortos y una
instrucción de replay; no añade espera, asignaciones ni I/O al callback. El
tooltip separa capturas cortas de las rechazadas por buffers ocupados. Exactamente
un segundo se acepta, y una captura válida limpia el aviso actual. El mensaje
de cola llena ya describe buffers ocupados, también durante descarte.

Cuatro casos con el procesador real a 44,1/192 kHz y bloques de 32/2048 prueban
el límite, ocho loops de 250 ms, dos loops aceptados y tercero rechazado con
commit de metadata retenido. Todos los intentos cortos quedan contabilizados;
su reparto entre cortas y rechazadas depende del spooler en este timeline
acelerado. Liberar cola no inicia captura a mitad de loop; volver al cero rearma
sin stop previo. Passthrough, prefijo/cola, duración/canales y recibos pasan.
El editor real muestra el aviso y el contador. RememberCaptureMatrix incluye
estos casos, también en el gate ASan de CI; --transport permite ejecución aislada.

Resultados terminales de esta modificación posterior: 5/5 suites Release en
43,25 s, 6/6 ASan en 99,59 s y pluginval nivel 5 sobre el VST3 actualizado.
Se conservan el log rojo previo, el primer caso corregido y los logs/JUnit finales:
[validación de transporte](<E:/JUCE PLUGINS/Remember/out/validation-20261008-transport/VALIDACION.md>).
La suite completa de 12/12 y los ensayos largos del apartado anterior preceden
al contador; no se presentan como una nueva pasada completa tras ese cambio.
El timeout de RememberCaptureMatrix sigue en 90 s; termina en 10,88/26,41 s
Release/ASan. Los 78.728 callbacks del procesador miden p99/p99,9/máximo de
0,0015/0,0073/0,0251 ms en Release; son fixtures acelerados, sin garantías físicas.

La implementación completa continúa. La tabla ya retira bucles cortos como
pendiente de fixture; mantiene extremos físicos de carga y los otros puntos:
config dañada desde Settings, rutas largas/disco lleno/permisos, música real,
interacción de edición/menús/choosers, arranque con biblioteca grande, drivers/
DAWs, CI remoto, telemetría y pipeline/aceptación de instalación finales.

**Settings dañados, recuperación explícita y raíces JSON — 8 de octubre, continuación**

Settings ya no presenta un JSON truncado como configuración inicial sin aviso.
Su instantánea incluye Result y el SHA256 de los bytes originales; un error de
lectura ofrece Reload y deshabilita guardar, y un archivo legible inválido
permite revisar opciones y Rebuild. La reconstrucción en worker conserva una
copia exacta junto al original, la vacía antes de reemplazar y rechaza cambios
concurrentes bajo el lock compartido. Conserva preferencias ajenas cuando hay
un objeto válido, además de 45 s/calidad 9/mono, y registra la ruta de copia.
No es CAS frente a escritores externos que ignoren la coordinación.

El nuevo fixture descubrió que isObject() de JUCE admite arrays. Configuración,
manifests, receipts y patches comprueban ahora DynamicObject en app/plugin:
se rechazan los arrays sin mutación nula, y la recuperación utiliza sólo audio
confirmado/verificado. Raíces truncadas/vacías/UTF-16/array, fuente demasiado
grande, opciones ajenas, instantáneas obsoletas, sharing nativo y cierre durante
una reconstrucción aceptada pasan. Avisos y copia quedan legibles en los
renders reales de Settings, revisados mediante pintado software.

App Release/ASan pasan 2/2 suites (170,45/202,57 s), plugin Release/ASan 6/6
(45,74/99,25 s) y el VST3 actual pasa pluginval 1.0.4/nivel 5 en perfil aislado.
No se aumenta el timeout ni el número de suites; los casos nuevos forman parte
de regresión/configuración/recuperación, exigidos por CI. Diagnóstico previo,
fuentes cubiertas, JUnit, logs y renders:
[validación de Settings](out/validation-20261008-settings/VALIDACION.md).

No se repiten benchmark completo de biblioteca ni estrés de 120 s en esta
tanda. Continúan pendientes disco lleno/permisos de escritura/rutas largas,
huérfanos ambiguos, choosers/edición simultánea, corpus real, arranque con
biblioteca grande, I/O/drivers/DAWs físicos, telemetría, CI remoto y pipeline
final con aceptación de instalación. El informe original sigue siendo el
alcance del objetivo; no se afirma completar sus 22 puntos.

**Coste inicial de Tags y controles virtuales — 8 de octubre, continuación**

Se separó el contenido del diálogo de su peer nativo: 10.000 tags actuales
creaban 10.512 controles y bloqueaban construcción 259,63 ms. Virtualizar
los chips redujo los controles a 55, pero mantuvo 261,81 ms porque dominaba
medir texto. Los anchos desconocidos de colecciones grandes pasan ahora a
queryAsync con snapshot/generación; la UI permite editar mientras muestra
Loading tags, y un resultado obsoleto o posterior al cierre no se publica.
Cachea anchos y conserva todas las identidades/bounds, creando sólo visibles.

Último ensayo separado Release: 10.000 actuales retornan del constructor en
3,27 ms, quedan listos en fondo en 260,80 ms y editan en 5,19 ms, con 55
controles. 10.000 sugerencias: 3,80 ms/270,57 ms/20 controles. La disponibilidad
total no se presenta como 3 ms ni como coste de abrir ventana/driver.
ASan verifica las mismas colecciones, edición/commit de 10.000 valores,
101 posiciones de scroll, acciones tras reciclar, resultados obsoletos y
cierre durante layout/acción pendiente. Renders inicio/final revisados;
separador Quick add corregido para no cruzar su título.

RememberAppRegression actual pasa 1/1 Release en 141,62 s y 1/1 ASan en 229,20 s.
El deadline CTest sigue en 300 s. La espera específica de font shaping ASan
pasa de 10 a 60 s tras medir más de 14 s en fondo, sin tocar las opciones ASan
ni las guardas de UI. App y tests compilan en ambos modos; el plugin/Shared no
cambia en esta tanda. Evidencias y límites:
[validación de Tags](out/validation-20261008-tags/VALIDACION.md).

La tabla retira la ampliación del coste inicial de tags como fixture pendiente;
mantiene aceptación de arranque real/bibliotecas grandes, interacción de
edición/menús/choosers, disco lleno/permisos/rutas largas/huérfanos ambiguos,
música real, drivers/DAWs/I/O físicos, telemetría, CI remoto y pipeline/instalación
finales. El objetivo completo sigue activo.

**Guardado JSON, permisos nativos y rutas largas — 8 de octubre, continuación**

La escritura anterior ya comprobaba writeText=false; se amplía la comprobación
del contenido y el diagnóstico. Windows crea un temporal nuevo, verifica cada
WriteFile en bloques de 64 KiB y FlushFileBuffers antes de publicar. ReplaceFileW
solicita una copia nativa del original para los estados de fallo parcial que
Microsoft documenta; la retira al guardar con éxito y conserva los archivos que
sobrevivan ante errores 1176/1177. No se han forzado esos estados nativos. El
respaldo histórico de metadata y recibos permanecen separados. Un archivo nuevo
se mueve sin sobrescribir uno que aparezca en paralelo; no hay delete-then-move.
Los errores guardan fase/ruta/código y acción de espacio/permisos/lectores/ruta.

Seis fallos inyectados después de escribir un temporal real verifican disco
lleno, short write y flush con target presente/ausente: bytes e identidad intactos,
sin publicación ni temporal propio residual. Permisos Windows DACL impiden
crear parent/temporal y reemplazar el respaldo, y se restauran completamente.
Read-only/sharing conservan el original; restaurar acceso/liberar lector permite
retry. El primer fixture de backup se corrigió porque negar DELETE al archivo
dejaba FILE_DELETE_CHILD del parent como alternativa; se conservan los logs rojos.

Un target de 272 caracteres, creado/leído/retirado con API extendida dentro del
fixture, falla en los cuatro ejecutables actuales con Win32 3. El writer explica
acortar la ruta y conserva bytes. Queda explicado/reproducido el límite del
proceso actual; no equivale a habilitar rutas arbitrarias en todos los DAWs.
Unicode exacto y JSON de varios bloques pasan por el writer real. El render
actual de Settings mantiene el mensaje ampliado encima del footer.

App pasa recuperación 1/1 Release/ASan (4,88/8,13 s) y regresión completa 1/1
(170,50/261,92 s). Plugin pasa seis suites Release/ASan (45,21/110,55 s) y el
VST3 actualizado pasa pluginval 1.0.4/nivel 5, perfil aislado. Compilan los productos
Release y app ASan, además de ambos tests ASan. Suites/timeouts no cambian;
las mediciones solapan otras pruebas y no prueban mejora porcentual. No se
repite benchmark completo de biblioteca ni estrés 120 s. README también aclara
que compilar deja la instalación automática del plugin desactivada.
Logs, JUnit, alcance, fallos previos y límites:
[validación de escritura](out/validation-20261008-atomic-json/VALIDACION.md).

Se volvió a consultar el punto 17 del informe original. La tabla actual retira
permisos nativos y explicación del límite de ruta como pendientes de fixture.
Siguen disco lleno físico/PCM/OGG, estados parciales nativos, huérfanos ambiguos,
choosers/edición simultánea, música real, arranque con biblioteca grande,
I/O/drivers/DAWs físicos, telemetría, CI remoto y pipeline/instalación finales.
El objetivo de los 22 puntos permanece activo.

**Huérfanos ambiguos y validación de dependencias — 8 de octubre, continuación**

El worker expone revisiones completas verificadas y un token del manifest;
la lista abre un selector real sin elección inicial, con duración/canales,
fecha, UUID y análisis. Permite cargar el OGG para escuchar, restaurar o
cancelar conservando archivos. Restauración revalida manifest, recibo y
propietario; protege una publicación concurrente y no cambia silenciosamente
una selección corrupta. Propietarios activos no se pueden recuperar y los
desconocidos requieren confirmación, también en Repair. Organización/análisis
y revisiones alternativas sobreviven. La fixture usa dos procesos reales
interrumpidos tras recibo, y UI real con fila reciclada, lector read-ahead,
cancelación y cierre antes/después de aceptar una edición con worker retenido.

El primer test ASan de la escena completa falló al destruir FileChooser.
MainComponent.obj era anterior a la ampliación de ProjectListComponent y su
registro MSBuild omitía las cabeceras. Se reconstruyeron app/tests desde cero
sin cambiar código; el registro nuevo incluye la cabecera de la lista entre
751 dependencias. Tocar sólo su timestamp recompila ambos consumidores con
contenido SHA idéntico. No se determina por qué el registro anterior quedó
incompleto. README utiliza --clean-first para aceptación ASan tras cambios de
layout; CI/pipeline ya usan directorios nuevos. El log rojo se conserva.

Las fuentes actuales pasan App Release 2/2 (170,18 s), App ASan reconstruida
2/2 (245,39 s), plugin Release 6/6 (44,58 s) y plugin ASan 6/6 (123,79 s).
El VST3 pasa pluginval 1.0.4/nivel 5 en perfil aislado. No cambian suites ni
timeouts. Se verifican nuevamente renders del selector y whitespace; no se
repiten benchmark completo, estrés 120 s ni pipeline final. Hashes de fuentes,
binarios, logs, JUnit y diagnóstico:
[validación de huérfanos](out/validation-20261008-orphan-choice/VALIDACION.md).

Se vuelve a leer el alcance y el punto 17 del informe original. La tabla retira
huérfanos ambiguos como fixture pendiente y mantiene selectores de archivos/
carpetas, más interacción de edición/menús, disco lleno físico/PCM/OGG,
estados parciales nativos de reemplazo, música real, arranque de biblioteca
grande, I/O/drivers/DAWs físicos, telemetría, CI remoto y pipeline/instalación
finales. El objetivo completo sigue activo.


**Corpus musical completo y edición de filas — 9 de octubre, continuación**

GiantSteps aporta 604 referencias de tonalidad y 664 de tempo v2, con 43 IDs
compartidos. Se adquieren los 1.225 originales del respaldo oficial con MD5
publicado y SHA256 individual, sin seleccionar por predicciones. Se fijan las
604 entradas de desarrollo y 621 de holdout antes del ajuste; un script reproduce
la división y rechaza sustituirla por otra. Un lote MP3 directo dejó de avanzar
mientras retenía un archivo; se preserva su evidencia, sin atribuir una causa no
verificada. El runner PCM usa float a frecuencia/canales originales, dos workers,
deadlines por child, checksums y resultados reanudables, sin dependencia FFmpeg
en producto y sin conservar todos los WAV temporales.

El detector analysisVersion=4 se evalúa sin cambios en un binario congelado:
1.225/1.225, cero errores, exit 0. Se rehashean todos los originales y verifican
cada entrada/resultado, referencias, herramientas, división y métricas por un
segundo cálculo. No quedan temporales PCM. BPM exacto 18,61% (123/661), mitad/doble
37,52%; tonalidad exacta 23,68% (143/604). La cobertura es 49,32/56,13%. Media,
mediana y p95 del análisis son 1.783,35/1.715,18/2.030,57 ms; wall time local con
trabajo de app solapado, no CPU en DAW. Contratos del evaluador y corpus sintético
pasan RememberAnalysis 1/1 Release/ASan; pilotos PCM equivalentes también pasan.

El resultado mantiene abierta aceptación musical. Sólo desarrollo se usa para
diagnosticar casos: 59 tempos a mitad, 28 a cuarta parte, 39 de 60 avisos de
variación con relación de octava entre segmentos; tonalidad muestra errores de
modo/relativas/quintas. El score alto sigue sin ser una probabilidad calibrada.
Se estudiarán períodos y chroma/perfiles sin forzar todos los tempos por octavas,
se ajustará sólo desarrollo y se reservará holdout para un candidato fijado.
Protocolo, hashes, resultados completos, auditoría y límites:
[validación musical](out/validation-20261009-musical-corpus/VALIDACION.md).

La app corrige el callback obsoleto de un rename cuando la misma identidad cambia
de ruta durante la edición. Preserva texto y adopta el callback del snapshot
actual; el menú compara también projectId antes de editar la fila inline. La
fixture falla con el callback antiguo. Cinco popups reales comprueban acciones
capturadas tras rebind, incluida otra identidad en la misma ruta. La lista real
reordena mientras un diálogo de tags permanece abierto; su commit conserva
favorito/tag concurrentes y no altera la nueva ocupante. Seleccionar un popup y
cerrar la lista antes de la finalización no abre otro diálogo.

App/tests compilan en ambos modos. Fixtures específicas y regresión completa
pasan: App Release 2/2 (173,62 s) y App ASan 2/2 (287,86 s), con suites/timeouts
intactos. No cambian layouts de clases ni código del plugin de audio en esta
tanda. No se repiten benchmark completo, estrés sostenido ni pluginval. Fuentes,
binarios, logs, JUnit y el fallo previo quedan registrados:
[validación de acciones de filas](out/validation-20261009-row-actions/VALIDACION.md).

Se vuelven a consultar los puntos 14/16/17/20/21/22 del análisis original. La
infraestructura de música etiquetada está completa, pero su precisión requiere
trabajo. Siguen selectores nativos/otras interacciones, I/O/disco lleno físico,
estados parciales nativos de reemplazo, drivers/DAWs reales, telemetría en hosts,
arranque de biblioteca grande, CI remoto y pipeline/instalación final. El objetivo
de los 22 puntos sigue activo; no se presenta esta tanda como cierre total.

**Detectores v5 y rendimiento — 9 de octubre, continuación**

Se exportan y auditan características de las 604 entradas de desarrollo desde
un binario v4 archivado; sus predicciones/PCM coinciden con el baseline. El
exporter y su reader acotan formato, dimensiones, tamaño y hashes. Calibración
de tonalidad usa tres folds internos y perfiles aprendidos fuera de cada fold;
tempo compara 48 variantes. Se elige una representación local por ventana que
conserva caché exacta; ruido/tono único, 48 tonalidades normales/desafinadas y
contratos de tempo filtran la selección. Los parámetros/perfiles se congelan
antes del lote nativo y holdout no participa en el ajuste.

analysisVersion=5 interpola picos, pondera fondo local y utiliza perfiles mayor/
menor de desarrollo. Tempo combina autocorrelación normalizada y evidencia
Fourier entre 40–240 BPM, comprueba intervalos regulares y soporte por segmentos.
Un cambio 120→240 se distingue de una ambigüedad estable de octava. Mantiene
prioridad del host, desconocido y los presupuestos/representación de caché.
El candidato nativo procesa 1.225/1.225, cero errores. La auditoría rehashea
originales/PCM, verifica fuentes, herramientas, resultados, referencias y métricas;
desarrollo coincide con el prototipo y holdout sólo se compara agregado.

Holdout: BPM correcto 208/335 (62,09%) frente a 54/335 (16,12%); tonalidad exacta
146/303 (48,18%) frente a 74/303 (24,42%). Completo: 418/661 (63,24%) y 305/604
(50,50%). La confianza sigue heurística: el score BPM ≥0,75 acierta 3/9 casos
reservados. No se cambia su umbral a partir de holdout; faltan calibración y
otros datos/géneros. Ese conjunto reservado ya utilizado no vuelve a estar
intacto para futuros candidatos.

Después se filtra una sola vez el mono, se reutilizan planes FFT por estimación
y se omite el recorrido de bins al reiniciar una ventana de tempo silenciosa.
Las curvas y el PCM conservan igualdad exacta en fixtures; dos WAV con ráfagas,
silencio y canales distintos tienen idénticas predicciones y bytes REMFEAT1
frente al binario congelado. Seis pares nativos alternados en PCM sintético de
12 s miden mediana estéreo 48 kHz 102,92→98,91 ms y mono 192 kHz
161,75→104,97 ms (3,89/35,11%). Son tiempos locales fuera de DAWs.

ResamplingTests preparaba mensajes detallados incluso al pasar millones de
comprobaciones. El mensaje ahora se forma sólo al fallar: ASan pasa en 2,18 s
frente al timeout de 90 s, sin cambiar matriz, calidad ni deadline. StressTests
retira el mismo trabajo y conserva todos sus escenarios/plazos. El gate ASan
combinado aún supera 300 s durante lifecycle tras publicar sus cuatro audios.
Una carrera adicional perdía un diagnóstico aceptado durante el último batch
bloqueado; el worker retenido procesa los comandos restantes al cerrar. Ocho
cierres con logger retenido y despacho UI pasan en ambos modos. El exporter de
pruebas también crea su parent nuevo antes de abrir features; su contrato lo
comprueba y el fallo anterior se conserva.

Las fuentes actuales pasan plugin Release 12/12 (227,91 s, incluido stress de
160,23 s), ASan 11/11 fuera de stress (290,26 s) y pluginval 1.0.4/nivel 5 del
VST3 actualizado. El pico real de caché es 16.723.584 bytes con cuatro retenciones
rechazadas en 58 instancias. CI añade resampling/caché/análisis a las nueve suites
ASan requeridas; sus 74 contratos de distribución pasan. El ciclo largo ASan
actual pasa por separado en 105,74 s: prepare/cierre 4,26/0,27 ms, máximo de
32 MiB reservado y asignaciones instrumentadas vivas de vuelta a cero. Esto
no da por superado el gate combinado de 300 s ni representa
ejecución remota de CI ni aceptación de un instalador.

Métodos, resultados, fuentes archivadas, logs rojos y límites:
[validación de calibración](out/validation-20261009-calibration/VALIDACION.md).
Se relee el análisis original completo y se mantienen pendientes confianza/
otros datos, gate combinado ASan, choosers nativos, arranque grande, I/O/disco
lleno físico, reemplazos nativos parciales, drivers/DAWs, telemetría en hosts,
CI remoto y pipeline/instalación final. El objetivo de los 22 puntos sigue activo.

**Checksums y fases — continuación del 9 de octubre.**

[La continuación de checksums y fases](out/validation-20261009-stress-phases/VALIDACION.md)
utiliza SHA-256 CNG nativo en ProjectIdentity, compartido por app/plugin, con
lecturas de 64 KiB, estado propio por llamada y fallback JUCE al fallar creación.
Conserva formato y digest exactos; cancelación, entrada ausente, crecimiento,
truncamiento, fronteras SHA/read y ocho hashes simultáneos están probados.
Los objetos nativos del hash se liberan también al fallar. No añade trabajo al callback.

El estrés ASan combinado pasa en 280,27 s sin cambiar sus 300 s, escenarios ni
comprobaciones. Sus commits de cuatro revisiones tardan 95,94–165,91 ms; sigue
medida la codificación OGG, que ocupa 13,47–55,35 s según canales en esa pasada.
Las once suites ASan restantes pasan en 267,94 s. Release completo pasa 12/12
en 216,42 s. La app pasa sus tres suites en Release (370,25 s) y ASan (574,13 s),
incluida matriz de 100/1.000/10.000 con rutas ausentes; estos dos modos se
ejecutaron en paralelo, por lo que sus tiempos no son un benchmark controlado.
El VST3 actualizado pasa pluginval 5. Las fuentes musicales no cambian y dos
PCM archivados conservan características/predicciones exactamente iguales.
Fuentes, imágenes y resultados están vinculados por SHA256 en la evidencia.
La aceptación de los demás requisitos del informe continúa pendiente.

**Arranque del contenido principal — validación del 9 de octubre.** La escena
anterior esperaba el índice antes de construir MainComponent. El nuevo fixture
reproduce una lista en blanco durante el primer scan de 10.000 entradas y la
inspección descubre una verificación que puede tomar una lista todavía vacía o
preparar un diálogo por ruta ausente. El índice termina ahora su estado loading
al publicar o fallar; calcula los proyectos no localizados en su misma
publicación. La lista presenta carga/vacía/error y recuento con acciones, conserva
búsqueda y no ejecuta esa segunda verificación. Una configuración dañada o una
carpeta configurada inaccesible no fuerza el diálogo de primer uso.

La matriz final cubre trece procesos nuevos por modo: 100/1.000/10.000 × 0/10/50 %,
vacía, configuración dañada, carpeta inaccesible y cierre durante 10.000/50 %.
Sólo comparte archivos preparados una vez por tamaño; cada proceso inicia perfil,
índice y cachés propios. Los proyectos ausentes se mueven fuera de las raíces de
búsqueda conservando identidad. Cuenta filas reales recursivamente, llega a la
última fila, prueba búsqueda durante carga y restaura proyectos sin perder previews.
No abre modales ni salida de audio. Mantiene watchdog 150 s, espera visible 90 s y
CTest 600 s; no amplía los 300 s de las suites existentes.

RememberAppColdStartup pasa en 185,03 s Release / 264,24 s ASan. Las otras tres
suites pasan en la invocación anterior: regresión 164,91/263,36 s, almacenamiento
8,72/8,85 s y benchmark 214,19/330,63 s. Esas primeras pasadas tienen el arranque
rojo por preparación del fixture; no son 4/4. Se conservan los fallos, el contador
inicial que daba cero, las fuentes/imágenes intermedias y las interrupciones de
pasadas reemplazadas. Sólo la CLI del arranque y su fixture cambian entre imágenes;
producción y el resto de AppTests son iguales. La evidencia usa JUnit separados,
sin fabricar uno conjunto. El módulo de distribución exige la nueva suite, y 88
contratos verifican también el rechazo de cada prueba requerida ausente.

Con 10.000 entradas, Release construye el contenido en 68,84–82,09 ms y publica
la biblioteca visible en 4,19–4,72 s; mantiene 10/11 componentes de filas. Dispatch
p99 16,03–16,05 ms y máximo 18,37–19,04 ms incluyen esperas solicitadas del fixture.
ASan publica en 17,38–18,43 s y tiene máximos de dispatch de 453–466 ms; no se
afirma respuesta inferior a 50 ms en ese modo. Cerrar antes de publicar tarda
0,19/4,79 ms y la retirada posterior 4,66/37,47 ms. Son observaciones bajo dos
modos ejecutándose en paralelo, sin comparación controlada ni garantía.

Se han revisado renders de carga, vacía, error y 5.000 proyectos no localizados.
Fuentes, cuatro imágenes, JSON de 26 casos, logs y JUnit se vinculan por SHA en
[validación de arranque](out/validation-20261009-startup/VALIDACION.md). Se mide
MainComponent y la primera publicación, con archivos potencialmente en caché
del sistema; quedan ventana/bandeja nativas, disco frío, selectores, hardware,
drivers/DAWs, confianza musical y otros datos, workflow remoto, pipeline final
e instalación/actualización/desinstalación. El objetivo general sigue abierto.
