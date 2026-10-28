# Registro de compatibilidad por DAW

Este archivo es la referencia permanente para planificar y registrar implementaciones
por DAW. `AGENTS.md` exige leerlo y mantenerlo al cambiar funciones específicas de un DAW.
Una función genérica del plugin no implica que podamos leer el proyecto guardado de ese DAW.

Estados: **Implementado** = existe código; **Parcial** = tiene límites indicados;
**Pendiente** = falta implementación; **Genérico** = no necesita un lector específico.
La validación se registra por separado; no se deduce del estado.

| Funcionalidad | FL Studio `.flp` | Ableton Live `.als` | Otros DAW |
|---|---|---|---|
| Grabación de previews mediante el VST3 | Genérico | Genérico | Genérico, requiere host VST3 compatible |
| Biblioteca, carpetas, tags, favoritos, filtros, recuperación y reproducción de previews | Genérico | Genérico | Genérico |
| Arrastre de preview completa como WAV desde el reproductor principal | Genérico; implementado | Genérico; implementado | WAV PCM de 24 bits; validar el arrastre nativo en cada host |
| Asociación de proyecto y apertura en el DAW (doble clic con confirmación) | Implementado | Implementado | Resolución por extensión: REAPER, Cubase/Nuendo, Studio One, Pro Tools y Bitwig; validar integración por host |
| Detección de backups para resolver el proyecto principal | Implementado | Implementado | REAPER, Cubase/Nuendo y Studio One: reglas específicas; ampliar pruebas por DAW |
| Used Sounds: inventario de samples | Implementado | Pendiente | Pendiente |
| Used Sounds: MIDI, playlist y audición básica | Parcial: notas, sin efectos/automatización compleja | Pendiente | Pendiente |
| Used Sounds: exportar MIDI `.mid` | Implementado | Pendiente | Pendiente |
| Used Sounds: instrumentos y preset guardado `.fst` | Parcial: preset explícito cuando existe; conserva estado opaco | Pendiente | Pendiente |
| Used Sounds: inventario de efectos | Pendiente | Pendiente | Pendiente |
| Statistics: top 1000 samples por ruta | Implementado; ver límites y validación | Implementado; ver límites y validación | Pendiente |
| Statistics: top 50 instrumentos | Implementado; ver límites y validación | Implementado; VST2/VST3/AU, nativos y Max for Live | Pendiente |
| Statistics: top 50 efectos | Implementado; ver límites y validación | Implementado; VST2/VST3/AU, nativos y Max for Live | Pendiente |
| Statistics: búsqueda, drag de copias y reproductor de samples independiente | Genérico sobre el inventario | Genérico sobre el inventario | Requiere lector de inventario |
| Offline rendering desde la app | Parcial: ejecutor CLI y ventana; publicación con rollback por proyecto, ver límites | Pendiente | Pendiente |
| Select preview: audio externo por grabación, desactivación y fallback a la captura | Genérico; decisiones manuales protegidas y asociación revalidada | Genérico; decisiones manuales protegidas y asociación revalidada | Formatos que pueda decodificar la app; no añade lectores de proyectos |

## Offline rendering (en investigación)

Auditoría del 28 de octubre: la confirmación de sobrescritura incluye el contenido
del archivo, además de identidad, tamaño y fecha. Los formatos de un proyecto
conservan originales y staging hasta terminar la publicación; ante fallo o
cancelación se restauran los ya sustituidos. Si otra aplicación modifica un
archivo publicado o Windows impide restaurarlo, se conserva el staging con
originales y renders y se indica su ruta para recuperación. Esto no es una
transacción atómica del sistema de archivos frente a un apagado del equipo;
la recuperación automática tras caída durante esa publicación queda pendiente.
El escaneo rechaza inventarios parciales ante errores de lectura. Las lecturas de
duración usan búfer, límite de eventos y cancelación dentro de playlists grandes.
Cancelar una ventana invalida también las confirmaciones/finalizaciones antiguas.

Desactivar una preview heredada es una decisión manual protegida incluso ante
renders posteriores (también se respetan selecciones desactivadas por versiones
anteriores). La asignación automática revalida la asociación dentro de la
transacción y no acepta identidad desconocida. No cambia el soporte de lectores
de Used Sounds, Statistics ni la captura VST3. Evidencia y cierre de regresiones:
`AUDITORIA_2026-10-28.md`; validación final de esta revisión en curso.

La ventana nativa ya está conectada al ejecutor secuencial, las estimaciones y la
confirmación de cada colisión. Persiste las opciones de Remember y las asociaciones
de renders, incluidas grabaciones indexadas posteriormente. Los ajustes de FL se
leen sin escribir en el registro. El usuario aceptó expresamente el 23 de octubre
de 2026 reutilizar los ajustes guardados y mostrarlos en modo de solo lectura en
la primera versión; la edición desde Remember queda aplazada. MIDI, M4A, publicación
de stems y validación de todos los controles siguen pendientes; el alcance no se
considera terminado. Si FL genera audios adicionales, se conserva el staging y se
detiene la publicación para revisión. La referencia inicial de tiempo es 1× audio;
las mediciones anteriores la sustituyen por proyecto con identidad coincidente.

Sonda real multiformato sobre copia de Empty: WAV/MP3/OGG/FLAC legibles, 48 kHz,
estéreo, WAV y FLAC de 24 bits; 7,106 s por la tanda
(`out/offline-multiformat-native-probe.log`). No valida canciones reales, todos
los ajustes de calidad/efectos ni equivalencia del registro con cada control CLI.
Builds finales Release de app y VST3 aprobadas (`out/offline-final-build.log`,
`out/offline-final-vst3-build.log`). Pruebas focalizadas de offline rendering,
previews seleccionadas y frontend bilingüe: 3/3
(`out/offline-final-focused-tests.log`). Snapshots nativos a 640×480 y 840×720,
incluido el panel desplazado de ajustes, inspeccionados en inglés y español.
Regresión/workflow/shell de app: 3/3 (`out/offline-window-app-regression.log`).
Regresión/workflow capture/config/frontend del plugin: 4/4
(`out/offline-window-plugin-tests.log`). No se han instalado los binarios.

Select preview guarda una referencia al audio externo, sin sustituir la captura
del plugin. El menú de tres puntos permite elegirlo, cambiarlo o desmarcarlo;
la fila muestra un indicador con instrucciones en su tooltip. Una elección manual,
incluso desactivada, queda protegida frente a asignaciones automáticas de renders.
Lectura/reproducción, waveform y Drag usan el audio seleccionado completo;
si no se puede abrir, usan la captura. Used Sounds y la audición de recuperación
mantienen su fuente explícita. Validación focalizada: `RememberSelectedPreviews`,
`RememberOfflineRendering`, `RememberWaveforms`, `RememberUsedSounds` y
`RememberAppFrontend`, 5/5 (`out/selected-preview-ui-tests.log`). Incluye pruebas
de identidad, ediciones concurrentes, fallback por archivo ausente/corrupto,
reproductor real y snapshots de filas recicladas en inglés/español a 384/768 px.
Builds Release de app y VST3 aprobados (`out/selected-preview-app-build.log`,
`out/selected-preview-vst3-build.log`). Regresión completa/workflow/shell de app:
3/3 (`out/selected-preview-regression-native.log`), ejecutadas fuera del sandbox
tras su rechazo de junctions/fixtures nativas. Regresión/captura/configuración
del plugin: 3/3 (`out/selected-preview-plugin-regression.log`); frontend del plugin:
1/1 (`out/selected-preview-plugin-tests.log`). No se instala ni publica la build.

Primer objetivo: FL Studio en Windows. Contrato, decisiones del usuario y límites
documentados en `OFFLINE_RENDERING.md`. Solo comandos oficiales, sin automatizar
ventanas ni modificar ajustes de FL mediante vías no documentadas. La CLI permite
elegir archivos, formatos y destino; no documenta controles para todos los ajustes
del diálogo de exportación. La exploración de subcarpetas debe ser explícita en
Remember. Sobrescribir un render existente requiere confirmación del usuario.
La primera base de planificación de archivos/comandos precede al ejecutor y la
ventana actuales. Prueba nativa `RememberOfflineRendering`: 1/1 aprobada el
23 de octubre de 2026 (`out/offline-rendering-plan-tests.log`); comprueba recursión
explícita, cancelación, rutas Unicode/con espacios, formatos y detección de colisiones
sin sobrescritura. No certifica ningún render real ni los ajustes usados por FL.
Prueba CLI aislada posterior: FL Studio 2026 renderizó una copia de su plantilla
Empty mediante `/Ewav /R <flp> /O<destino>`; WAV PCM estéreo de 24 bits, 48 kHz,
cabecera y tamaño consistentes, proceso sin elevación y salida posterior de FL.
Artefactos en `out/offline-rendering-cli-probe/`. Esto valida ese comando/plantilla,
no todos los ajustes, formatos ni proyectos. App y VST3 Release compilados:
`out/offline-rendering-app-build.log`, `out/offline-rendering-vst3-build.log`.
El lector C++ recupera los 22 valores de exportación previstos en
`HKCU\Software\Image-Line\FL Studio 26\General\WAVRenderForm`, solo en lectura:
`out/offline-rendering-fl-settings.log`. No escribe ajustes de FL ni demuestra
por sí solo qué valores respeta cada modo de renderizado CLI.
El motor C++ también ha renderizado la plantilla Empty mediante staging y
publicación verificada: 6,61554 s, WAV PCM 24 bits/48 kHz estéreo
(`out/offline-render-executor-native-probe.log`). Hay planificación de tandas y
persistencia de opciones, panel de solo lectura, estimaciones y asociación de
resultados. El destino personalizado es plano, con confirmación de
colisiones; FL abierto bloquea el inicio hasta guardar y cerrarlo, por decisión
del usuario. Settings incluye el DAW offline por defecto, guardado con OK;
su validación visual en inglés y español figura arriba. No se certifica aún M4A: el validador
actual necesita un decodificador de Remember.
No cambia el
soporte de Used Sounds, Statistics o captura mediante VST3.

## Apertura desde la lista de previews

El doble clic en una fila solicita confirmación antes de abrir el proyecto
asociado. El diálogo identifica nombre, DAW y ruta, y conserva la identidad de la
grabación aunque se recicle la fila. Cancelar no abre el DAW. Si falta el proyecto,
se indica que hay que localizarlo primero. Usa el mismo resolvedor y lanzamiento
asíncrono ya existente; no añade lectores ni amplía la matriz de soporte por DAW.
La validación nativa por host sigue siendo la indicada en la matriz.

La opción Capture from any position muestra en app y VST3 un aviso asíncrono
sobre posibles artefactos y colas de sonido incorrectas. No cambia el motor de
captura ni los formatos admitidos por los hosts.

Validación del 22 de octubre de 2026: frontend de app aprobado, con gesto de doble
clic, nombre/DAW/ruta capturados y cancelación sin lanzamiento; menú contextual y
confirmación de condiciones de smart folders; aviso de captura en Settings.
`out/seven-changes-app-themed-tests2.log`. Captura y frontend del VST3: 2/2,
`out/seven-changes-plugin-themed-tests.log`. Regresión completa de app, workflow
y shell nativo: 3/3, `out/seven-changes-app-native-regression.log`. Used Sounds,
Statistics y recuperación mantienen sus suites aprobadas en
`out/seven-changes-app-final-ui.log`. Estas pruebas no abren un DAW real ni
amplían la certificación por host de la matriz.

Revalidación tras separar las condiciones de smart folders de los filtros
temporales: app y VST3 compilados, frontend de app y workflow de filtros aprobados,
captura/frontend del VST3 2/2 y regresión completa/workflow/shell Windows 3/3.
Logs: `out/smart-scope-app-build.log`, `out/smart-scope-plugin-build.log`,
`out/smart-scope-frontend.log`, `out/smart-scope-workflow.log`,
`out/smart-scope-plugin-tests.log` y `out/smart-scope-full-regression.log`.

## Arrastre de previews

El botón Drag a la izquierda de Assign path exporta toda la preview a WAV PCM
de 24 bits, con su frecuencia de muestreo y canales originales, sin aplicar volumen,
normalización ni metrónomo del reproductor. Funciona con OGG/WAV antiguos y con
contenedores fragmentados `.rmp`; estos últimos se decodifican sin otra compresión
con pérdida. Preparación en worker, arrastre de copia y sin reproducción automática.
Los WAV se conservan en `Exports` dentro de la carpeta configurada de previews.
Los nombres incluyen el hash del audio fuente: se reutilizan copias sin cambios y
se conservan las anteriores al cambiar la preview. No hay limpieza automática,
porque un DAW puede seguir usando la ruta arrastrada.

Se eligió WAV para ampliar compatibilidad: [Ableton admite OGG Vorbis y WAV](https://help.ableton.com/hc/en-us/articles/211427589-Supported-Audio-File-Formats),
[Cubase admite OGG](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/pool/pool_media_importing_c.html),
pero [Avid no enumera OGG entre los formatos compatibles con Pro Tools y sí WAV PCM](https://kb.avid.com/pkb/articles/en_US/Knowledge/Pro-Tools-Audio-File-Type-and-Session-Support).
Esto documenta compatibilidad de formato, no una certificación del gesto de
arrastre en cada versión de FL Studio, Live, REAPER, Cubase, Studio One, Bitwig,
Logic o Pro Tools. La prueba `RememberAppTests --waveforms` valida el contrato de
copia, archivos exportados, canales/duración, reutilización y `.rmp` offline.
Queda pendiente la aceptación del arrastre dentro de cada host real.

Validación del 20 de octubre de 2026:
- App y VST3 Release, incluidos ejecutables de pruebas: compilados correctamente.
  App: `out/waveforms-app-build.log`; prueba final: `out/waveforms-final-tests-build.log`.
- Regresión completa de app, nueva suite Waveforms y frontend: 3/3 aprobadas,
  `out/waveforms-app-tests.log`. Incluye Used Sounds, Statistics, ajustes,
  recuperación, acciones de filas y reproducción.
- Fixture corregido para sustituir el audio al generar un tono nuevo, comprobando
  que el WAV nuevo cambia de contenido y conserva el anterior: aprobado,
  `out/waveforms-final-tests.log`.
- Frontend VST3: aprobado, `out/waveforms-plugin-frontend.log`.
- Snapshots JUCE revisados: app a 640×480 y 1024×768, reproductores principal y
  compacto en claro/oscuro con L/R separados y colores, menú contextual y plugin.
  `out/t/f57e9c5d9e45/artifacts/waveform-*.png`,
  `out/t/f57e9c5d9e45/artifacts/apple-app-waveform*.png` y
  `out/t/14d9dae8cfcc/artifacts/apple-plugin*.png`.

## Contrato de Statistics

- Disponible en la app, junto a Settings; tres pestañas: Samples, Instruments y Effects.
- Toda la biblioteca activa; todos los sonidos cargados en los proyectos guardados,
  aunque no estén en el playlist. No analiza cambios aún no guardados en el DAW.
- Cada ruta de sample y cada plugin suma como máximo una vez por proyecto.
  Varias grabaciones asociadas a la misma ruta de proyecto no multiplican el contador.
- 1000 samples, 50 instrumentos y 50 efectos; búsqueda por nombre dentro del ranking.
- Ocultar entradas guarda identidades por categoría entre sesiones. El top visible se
  rellena desde el inventario completo de metadatos; la búsqueda solo filtra ese top.
  El ojo muestra entradas visibles y ocultas juntas dentro del top original, con el
  mismo máximo de 1000 samples o 50 plugins por pestaña. No añade filas por encima
  del límite. Unhide entry recupera las entradas ocultas incluidas en ese top.
- La identidad de samples usa su ruta resuelta, con reglas de mayúsculas de la plataforma.
  No agrupa por nombre ni por contenido del audio. Samples ausentes siguen contando.
- Nativos y externos. Plugins externos con el mismo nombre se agrupan entre FL/Ableton;
  los nativos tienen un espacio de nombres por DAW. No se adivinan equivalencias entre
  nombres diferentes de VST2/VST3/AU.
- Proyectos no asociados, ilegibles y de formato no soportado se muestran en cobertura;
  no se presentan como proyectos sin sonidos. Las clasificaciones desconocidas se avisan.
- Cálculo a demanda y Refresh, en un worker. Sin caché de audio, estados de plugins ni
  cambios en los proyectos. Reproductor independiente con el comportamiento de Used Sounds.

## Lectores y límites

### FL Studio

Statistics lee eventos de FL Studio 10 o posterior, incluyendo la bandera de tres bytes
de FL 25/26. Separa instrumentos del Channel Rack y efectos del mezclador, identifica
Fruity Wrapper por sus metadatos de nombre y omite automatizaciones. Salta estados opacos
sin cargarlos. Los contenedores como Patcher pueden ocultar plugins internos; queda
pendiente leer sus subplugins. Los samples encapsulados dentro de plugins externos
requieren lectores específicos del plugin.

### Ableton Live

Statistics lee XML `.als` sin comprimir o gzip, de forma incremental. Lee referencias
de samples de clips y dispositivos, también dentro de racks; omite referencias históricas
de SourceContext y presets. VST2, VST3 y AU se clasifican mediante metadatos guardados.
Los nativos conocidos usan su tipo XML; los tipos desconocidos se omiten con aviso.
Max for Live utiliza el nombre del archivo `.amxd` del dispositivo, con las variantes
`MxDPatchRef` de Live 11 y `MxPatchRef` de Live 12. No usa nombres de pistas ni de presets
para inventar la identidad del instrumento/efecto. Los flags VST2 se interpretan según
Live (bit cero para instrumentos), no como los flags del SDK VST2. La clasificación
guardada y BranchDeviceId se leen sin depender del orden de los elementos.
No se inspeccionan buffers opacos de plugins ni samples internos de Kontakt u otros VST.
Pendientes: aliases antiguos de macOS, formatos antiguos con rutas fragmentadas,
nuevos tipos nativos no registrados y validación específica de Live 10.
Los racks se recorren como contenedores, sin contarlos como plugins adicionales.

Ambos lectores limitan el archivo a 256 MiB; Ableton permite hasta 512 MiB de XML
descomprimido, profundidad 256, tags de 64 KiB y dos millones de elementos/eventos.
Un límite excedido genera un error explícito de cobertura.

## Validación

Evidencia del 20 de octubre de 2026:

- Biblioteca real: 47 grabaciones corresponden a 43 rutas de proyecto FL Studio distintas.
  Lectura de los 43 proyectos sin errores ni plugins sin clasificar; 436 samples,
  29 instrumentos y top 50 efectos. Log: `out/statistics-real-library.log`.
- Auditoría adicional, solo en lectura, de 87 proyectos Ableton de las carpetas
  configuradas: cero errores y cero plugins sin clasificar. Incluye Live 11.0.11–11.3.35
  y Live 12.0.1–12.4.6, VST2/VST3/AU, racks, DrumCell y Max for Live.
  Log: `out/statistics-real-ableton-final.log`. Estos archivos de prueba no se añaden
  automáticamente a la biblioteca: la pantalla sigue contando solo sus grabaciones.
- `RememberAppTests --statistics`: separación de categorías, deduplicación por proyecto,
  agrupación por ruta entre DAW, búsqueda, límites 1000/50/50 aplicados después de ordenar,
  archivos ausentes, cancelación, XML truncado, copia del original mediante drag,
  reproducción independiente y lista reciclada con 1000 entradas.
- Estado final: builds Release de app, VST3 y ejecutables de pruebas correctos.
  Logs: `out/statistics-final-ui-build.log`, `out/statistics-plugin-current-build.log`.
- Suite final de Statistics y frontend de app: correctas, incluida la representación
  de biblioteca no disponible con contador «Unavailable». Logs:
  `out/statistics-current-tests.log`, `out/statistics-current-frontend.log`.
  Frontend del VST3 correcto: `out/statistics-plugin-current-frontend.log`.
- La regresión completa previa pasó, incluidos Used Sounds, carpetas, acciones de filas,
  ajustes, recuperación y reproducción: `out/statistics-app-regression.log`.
  En la revalidación del workspace actual, otros cambios añadieron `testContextUndo`:
  la suite completa termina con `FAIL: Context undo timed out`.
  Se reproduce ejecutando únicamente `--undo`, sin ejecutar Statistics. Es una incidencia
  separada de la implementación de Statistics y queda pendiente de resolver en el trabajo
  de deshacer contextual. Logs: `out/statistics-current-regression.log` y
  `out/statistics-isolated-undo-check.log`. Esa evidencia corresponde a la build previa.
  La revalidación de las mejoras de Statistics sí completa la regresión global
  (incluido deshacer contextual): `out/statistics-improvements-full-regression.log`.
  Las pruebas aisladas de Statistics, Used Sounds, frontend de app y frontend de
  VST3 también pasan; logs `out/statistics-improvements-*.log`. Incluye persistencia
  tras reabrir/cerrar, categorías separadas, menús nativos y relleno de los tres tops.
  Verificación adicional de Hide/Unhide mediante menús nativos de las tres pestañas:
  `out/statistics-context-menu-tests.log`; snapshots `statistics-*-context-menu.png`.
  Límite estricto con el ojo activado y búsqueda dentro del top en las tres
  categorías, más restauración del top rellenado al desactivarlo:
  `out/statistics-eye-limit-tests.log` (aprobado).
- Snapshots de componentes JUCE revisados en claro y oscuro, a 640×480 y 1024×768,
  además de la ventana principal y el plugin a tamaño normal. Cubren nombres largos,
  samples ausentes, búsqueda sin resultados, biblioteca vacía/no disponible y controles deshabilitados.
  Ubicaciones: `artifacts/statistics-*.png`, `out/t/stats/artifacts/` y
  `out/t/14d9dae8cfcc/artifacts/apple-plugin.png`.

Esta evidencia comprueba lectura offline y componentes nativos; no certifica una sesión
de reproducción/captura dentro de cada versión de cada host.

## Estimación de memoria de Statistics

El informe conserva nombre, identidad, ruta, contador y disponibilidad. No guarda PCM,
notas ni estados opacos de plugins. Los componentes de fila se reciclan según la zona
visible; no se crean 1000 reproductores ni 1000 controles completos.

| Escenario | Estimación de metadatos retenidos |
|---|---:|
| Biblioteca real FL: 436 samples + 29 instrumentos + 50 efectos | 176.895 bytes, unos 173 KiB |
| Auditoría ALS: 1000 samples + 24 instrumentos + 50 efectos | 474.097 bytes, unos 463 KiB |
| Fixture anterior: top limitado a 1000 nombres/rutas largos + 50 instrumentos + 50 efectos | 536.494–560.494 bytes, unos 524–547 KiB |
| Fixture actual: inventario de 1100 rutas largas + 65 instrumentos + 65 efectos, para rellenar entradas ocultas | 634.614 bytes, unos 620 KiB |

La diferencia del fixture depende de la longitud de la carpeta temporal de pruebas.
`Report::metadataBytes()` incluye estructuras, capacidad de vectores y un margen por
asignación de strings; es una estimación conservadora, no una medición exacta del heap.
Los ejemplos medidos quedan por debajo de 1 MiB, pero ahora el informe conserva
**todo el inventario de metadatos** para rellenar el top al ocultar entradas. Su coste
crece con los elementos únicos, no solo con las 1100 filas máximas habituales.
Como aproximación conservadora con rutas largas: unos 0,5–0,6 KiB por elemento,
5–6 MiB para 10.000 y 50–60 MiB para 100.000. Las rutas excepcionalmente largas
aumentan esa cifra. Los IDs ocultos añaden strings y nodos de un set por categoría.
El filtro visible solo guarda índices; las filas siguen reciclándose.

El cálculo temporal necesita además un mapa de todos los elementos únicos para obtener
un top exacto; su memoria crece con la biblioteca y se libera al publicar el informe.
Durante la publicación coinciden temporalmente el mapa y el vector ordenado.
Los lectores procesan un proyecto cada vez, con buffers de 64 KiB y tags acotados;
no cargan el proyecto completo ni descomprimen el XML entero en memoria.

Al escuchar un sample se añade el reproductor compartido: aproximadamente 0,8 MiB de
read-ahead a 48 kHz estéreo, hasta unos 3 MiB con frecuencias altas, y hasta 1 MiB de
picos y momentos espectrales de waveform, además del decodificador/dispositivo. Ese coste pertenece a una sola
audición y no se multiplica por el número de entradas.

## Propuesta para una cuarta pestaña

**Library overview**: distribución de BPM y tonalidades, y número de proyectos por
estado (Idea / In progress / Finished / Archived). Puede aprovechar la metadata ya
indexada de la biblioteca, con muy poco almacenamiento adicional y sin otro lector
de proyectos. Es una propuesta pendiente de elección; no está implementada.

## Cola para nuevos DAW

Para REAPER `.rpp`, Cubase `.cpr`, Nuendo `.npr`, Studio One `.song`, Pro Tools `.ptx`
y Bitwig `.bwproject`: inventario de samples, identidad de plugins, separación
instrumento/efecto, paths relativos/ausentes, duplicados, límites, cancelación y fixtures.
Después: ampliar Used Sounds (playlist, notas, tempo, exportación y audición).
No marcar soporte a partir de la mera asociación del archivo o de la grabación del VST3.

## Referencias de formato

- [PyFLP: eventos de mezclador y slots](https://github.com/demberto/PyFLP/blob/master/pyflp/mixer.py).
- [Ejemplos ALS de descriptores VST/AU y cadenas](https://github.com/luizen/als-tools/blob/main/TEMP.txt).
- [Ejemplo de metadatos VST3 en ALS](https://github.com/claytonwashington/hit/blob/main/VST_SYNC_SPEC.md).
