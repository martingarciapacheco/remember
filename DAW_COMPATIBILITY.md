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
| Asociación de proyecto y apertura en el DAW | Implementado | Implementado | Resolución por extensión: REAPER, Cubase/Nuendo, Studio One, Pro Tools y Bitwig; validar integración por host |
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
