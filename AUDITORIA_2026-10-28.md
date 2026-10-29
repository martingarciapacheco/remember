# Auditoría de funcionamiento y rendimiento — 28 de octubre de 2026

## Punto de partida y método

Estado previo guardado **antes de auditar** en `36d8b6d` (rama `main`). Todo
el trabajo de esta revisión pertenece a `audit/app-correctness-performance-2026-10-28`.
Los informes de octubre anteriores son antecedentes, no evidencia del estado actual.

Fase 1: inspección de fuentes, contratos y pruebas existentes, compilación Release
de app/VST3/tests y ejecución inicial de CTest. Se revisan índice y notificaciones,
ediciones/organización/undo, selección de audio, reproducción/read-ahead/waveforms,
audición/Statistics, configuración/localización, shell/cierre y renderizado offline;
también sus fronteras compartidas de almacenamiento y plugin. Las suites existentes
aportan cobertura adicional de captura, identidad, recuperación y concurrencia.
Esta revisión no equivale a certificar todos los dispositivos o DAW reales.

Fase 2: resolver los IDs siguientes en orden, con regresiones que ejerciten el
fallo y evidencia final. Los primeros 12 se registraron en `7d77835`, antes de
cambiar implementación. A13–A15 se añadieron durante la verificación, antes de
corregir cada uno. La tabla siguiente consolida el inventario y su cierre.
P1 = pérdida de decisiones/archivos o ejecución incorrecta; P2 = robustez/rendimiento.
Todos los hallazgos iniciales están confirmados por el camino de código indicado;
Las reproducciones y resultados de cierre figuran debajo.

## Inventario y comprobación de cierre

| ID | Prioridad | Fallo / mejora y evidencia inicial | Criterio de cierre | Estado |
|---|---|---|---|---|
| A01 | P1 | `PreviewSelection::disable` conserva procedencia `render`; `select(..., false)` puede volver a activarla pese a la decisión explícita de volver a captura. | Desactivar cualquier selección cuenta como decisión manual; un nuevo render no la cambia. | Corregido y verificado |
| A02 | P1 | La asignación automática y `inheritRender` verifican projectId pero no vuelven a comprobar la asociación de proyecto dentro de la transacción; una entrada reasociada puede recibir el render anterior. La comparación de IDs vacíos también acepta identidad desconocida. | Rechazar asociación obsoleta/identidad desconocida bajo lock; conservar elecciones y otros campos concurrentes. | Corregido y verificado |
| A03 | P1 | `outputStamp` sólo incluye ID/tamaño/mtime; una edición in situ de igual longitud y fecha restaurada elude la confirmación de sobrescritura. | Comprobar contenido además de identidad y conservar cambios concurrentes. | Corregido y verificado |
| A04 | P1 | `render` publica formatos uno a uno; un fallo o cancelación tras el primero conserva una tanda parcial y limpia el staging restante. | Deshacer una publicación incompleta; conservar originales y audio recuperable si falla el deshacer. | Corregido y verificado |
| A05 | P2 | `collectProjects` usa el iterador JUCE sin resultado de errores de enumeración; una subcarpeta ilegible puede devolver éxito y un inventario parcial. | Fallar explícitamente y descartar inventario parcial ante errores; mantener recursión explícita y evitar enlaces. | Corregido y verificado |
| A06 | P1 | `stop` sólo cambia `batch`/señal; las finalizaciones de inicio/refresh/execute y la confirmación inicial aún pueden instalar planes, continuar trabajo o marcar Completed tras parar. | Cancelar impide continuaciones/confirmaciones obsoletas; permite replanificar y cerrar sin bloquear UI. | Corregido y verificado |
| A07 | P2 | Error del worker se atiende antes de la finalización específica: un fallo guardando historial deja fila Rendering; error de refresh deja el plan anterior arrancable. | Estado de fila/plan coherente ante fallo, cancelación y reintento; feedback bilingüe. | Corregido y verificado |
| A08 | P2 | `readProjectTiming` lee eventos byte a byte directamente de `FileInputStream` y no comprueba cancelación en el bucle interno del playlist ni limita eventos. | Búfer acotado, cancelación dentro del playlist, presupuesto de eventos y estimaciones equivalentes. | Corregido y verificado |
| A09 | P2 | `planBatch` deduplica rutas con búsquedas lineales de StringArray, repetidas por cada salida: coste cuadrático en tandas grandes. | Conjuntos de rutas normalizadas; mantener orden y reporte de colisiones exacto; medir tanda grande. | Corregido y verificado |
| A10 | P2 | `PreviewExport::prepare` calcula dos SHA de archivo completos sin consultar cancelación durante la lectura; ahora puede recibir renders externos de horas. | Hash por bloques cancelable, errores de lectura explícitos y mismos nombres/reutilización de exportaciones. | Corregido y verificado |
| A11 | P2 | `readEntryMetadata` y `LibraryStore::readJson` cargan el archivo entero sin límite, a diferencia de configuración. Esto incluye recibos/backups leídos por recuperación; su manifest principal ya tenía límite. | Límite explícito y compartido antes/durante lectura y antes de guardar; no reparar/sobrescribir archivos enormes; preservar entradas sanas. | Corregido y verificado |
| A12 | P2 | Si falla `WaveformEnvelope::read`, el player sólo cambia descripción; con normalización `play` sigue esperando un waveform que nunca llegará. | Salir de espera, mostrar error accionable y permitir reproducción al desactivar normalización; probar carga/cancelación siguiente. | Corregido y verificado |
| A13 | P1 | Hallazgo adicional al verificar A12: el waveform acepta frecuencias de muestreo finitas sin máximo, aunque read-ahead rechaza más de 768 kHz. Una cabecera WAV dañada puede pedir miles de millones de Hz y una ventana de medición de varios GiB. | Rechazar esa cabecera antes de reservar memoria, aplicar el mismo máximo que reproducción y comprobar el caso real. | Corregido y verificado |
| A14 | P2 | La regresión ASan reproduce `Root drop did not preserve insertion position`: el índice puede publicar el nombre nuevo entre rename, cambio de orden y cambio a orden manual de una misma operación. | Publicar sólo observaciones completas de ediciones de carpetas; probar el índice reteniendo la fase de metadatos y revalidar el gesto/drop y undo. | Corregido y verificado |
| A15 | P2 | La escena ASan de 10.000 entradas reproduce un retraso de publicación después de una edición: Refresh/edits sólo despiertan al índice al terminar su escaneo obsoleto. El manifiesto ya tiene la fecha nueva, pero la vista conserva el orden anterior durante otro recorrido completo. | Cancelar y reiniciar observaciones obsoletas ante pedidos explícitos, conservar la publicación válida y aprobar la misma escena/selección dentro de sus límites originales. | Corregido y verificado |

## Validación inicial

- Compilaciones app, VST3, RememberAppTests y RememberTests Release correctas:
  `out/audit-baseline-{app,plugin}-build.log`.
- CTest app inicial: 11/15; los fallos incluyen creación de junction (Win32 5),
  workflow y shell/tray dentro del sandbox. La repetición nativa aislada pasó
  (`out/audit-baseline-app-native.log`): eran restricciones de ese entorno.
- CTest plugin 16/16 (352,01 s) y reejecución app nativa 4/4 (219,05 s), incluidos shell/tray y regresión global: aprobados.
- No se ha cambiado implementación durante la fase 1. No se instala, publica ni
  modifica ningún proyecto de usuario como parte de estas comprobaciones.

## Registro de la fase 2

Se completó cada ID con cambio, regresión y resultado observado. La aceptación
final incluye builds app/VST3, CTest completo, comprobaciones de memoria pertinentes
y snapshots JUCE mínimos/normales en inglés y español para las interfaces afectadas.

- A01–A04: regresiones focalizadas aprobadas en `out/audit-a01-a05-tests.log`.
  El nuevo test A03 fallaba antes de la corrección con edición in situ de igual
  longitud y fecha restaurada. A04 fuerza con un handle nativo la imposibilidad
  de sustituir el segundo formato y cancela después del primero: ambos originales
  sobreviven, sin publicación parcial. La restauración protege ediciones externas;
  si no se puede restaurar, conserva originales/renders en staging y explica la ruta.
- A05: `--library-index` aprobado con denegación real de enumeración y restauración
  de DACL (`out/audit-a05-library.log`), además de las suites offline/selección.
- A06–A07: implementados tokens de operación, cancelación de finalizaciones,
  invalidación de planes y tratamiento específico de errores de historial/fila.
  Se añaden pruebas de worker retenido, confirmación tardía y fallo real al guardar
  historial después de generar un WAV mediante proceso simulado.
- A08–A09: búfer de 64 KiB, máximo de dos millones de eventos y cancelación dentro
  de playlists; conjuntos hash para rutas de salidas conservando su orden.
  La prueba anterior tardaba 831,167 ms en leer 250.001 eventos y fallaba al
  cancelar un único evento con 10.000 clips (`out/audit-a06-a09-tests.log`).
- A10: exportación reutiliza el SHA nativo por bloques de 64 KiB ya compartido en
  `ProjectIdentity::fingerprint`; no se cambia el hash/nombre de los WAV. Una
  cancelación durante hash debe ocurrir antes de crear el directorio de exportación.
- A11: lector JSON compartido de 8 MiB, lectura completa comprobada y límite de
  escritura. El escáner conserva filas y no intenta reparar por error de tamaño.
- A12: fallo de waveform termina la espera, muestra instrucciones localizadas y
  permite reproducir desactivando normalización. Prueba con decoder ya abierto,
  worker de waveform retenido y cabecera dañada antes de su segunda apertura.
  No se usa un dispositivo físico para comprobar este camino.

Los 15 puntos quedan cerrados por sus comprobaciones específicas y por la
aceptación global documentada al final, además de la inspección del código.

A13 se registró durante la revisión de cierre de A12, antes de corregirlo.
A14 se añadió al reproducirse una publicación intermedia bajo ASan. El inventario
final incluye 15 hallazgos; los adicionales se registraron antes de corregirlos.

- A13 rechaza frecuencias superiores a 768 kHz antes de crear buffers/medidores y
  evita el desbordamiento al calcular bins. La regresión usa una cabecera PCM
  independiente que JUCE acepta con 2.147.483.647 Hz; el waveform la rechaza sin
  reservar la ventana. `RememberWaveforms` pasa con ASan.
- A14 usa generación y contador de ediciones de carpetas, con scopes anidados,
  para descartar observaciones tomadas durante crear/renombrar/mover/borrar/undo.
  Una prueba retiene el movimiento entre rename y orden final, pide Refresh y
  comprueba que permanece la publicación anterior. Después verifica orden, gesto
  nativo y deshacer. `out/audit-a14-asan-sidebar.log` y las tres suites del plugin
  `out/audit-a14-plugin-tests.log` pasan.

Durante validación, Release y ASan llegaron a competir por el bloqueo global de
renderizado de FL. La validación de aceptación se ejecuta en secuencia; no se
elimina ese bloqueo del producto. El primer fixture de frecuencia escribía un
offset fijo sobre un WAV con chunks adicionales: se sustituyó por una cabecera
PCM independiente, comprobando explícitamente que el lector acepta esa frecuencia.

La regresión ASan alcanzó 300 s preparando la escena de 10.000 entradas. Los logs
mostraban recorridos de 8–9 s del índice mientras todavía se creaban archivos.
Se conservan los mismos 100/1.000/10.000 registros, IDs, bytes, biblioteca creciente,
aserciones y límites: los fixtures se preparan fuera de su vigilancia y se publican
con movimientos de directorio; los marcadores/FLP de planificación usan escritura
simple, sin temporales de sustitución innecesarios. Cuatro tareas de E/S acotadas
preparan/publican esos fixtures y todas terminan antes de medir la UI. No se amplía
el timeout ni se eliminan aserciones. La limpieza de cada carpeta temporal propia
usa el recorrido nativo de `std::filesystem`, con comprobación de propietario,
nombre y ausencia de enlace en la raíz. La regresión ASan final empleó 256,03 s
en pruebas y 22,11 s en limpieza, resultado `ok`: 278,73 s en total, dentro de
los 300 s originales.

- A11 incluye también los lectores heredados de preferencias en `loadConfig`,
  que ahora delegan en el lector JSON limitado en vez de cargar el archivo entero.
- A15: la escena completa llegó a las 10.000 entradas dentro de los 300 s, pero
  reprodujo el retraso de la actualización de fecha/selección a continuación
  (`out/audit-acceptance-asan4-regression.log`). El índice ahora comprueba peticiones
  nuevas durante la lectura y descarta/reinicia el trabajo obsoleto; no publica una
  biblioteca vacía al cancelar ni conserva el backoff de una reparación cancelada.
  La escena de 10.000 entradas, el cambio de fecha, la selección por identidad y
  el cierre pasan con Release y ASan, manteniendo sus límites originales.

## Aceptación final — 29 de octubre de 2026

| Comprobación | Resultado | Evidencia local |
|---|---|---|
| App y RememberAppTests Release | Compilados | `out/audit-a15-app-build.log`, `out/audit-cleanup-app-build.log` |
| VST3 y RememberTests Release | Compilados | `out/audit-final-plugin-build2.log`, `out/audit-a15-plugin-tests-build.log` |
| CTest completo de app, incluidos benchmarks, shell y bandeja nativos | **17/17**, sin omisiones; 691,56 s | `out/audit-acceptance-release-tests.log`, `out/audit-acceptance-release.xml` |
| CTest completo de plugin, incluida captura sostenida | **16/16**, sin omisiones; 449,85 s | `out/audit-final-plugin-tests.log`, `out/audit-final-plugin.xml` |
| Revalidación de asociación/regresión/configuración del plugin después de A15 | **3/3**; 53,49 s | `out/audit-a15-plugin-tests.log`, `out/audit-a15-plugin.xml` |
| Regresión global de app con ASan, reconstrucción limpia previa | **1/1**; 278,73 s; sin errores de memoria | `out/audit-acceptance-asan5-regression.log`, `out/audit-acceptance-asan5-regression.xml` |
| ASan: waveforms, offline rendering, selected previews, undo, frontend y almacenamiento/recuperación | **6/6**, sin omisiones; 111,39 s | `out/audit-final-asan-focused-tests.log`, `out/audit-final-asan-focused.xml` |
| Bundle VST3 con pluginval 1.0.4, nivel 5 y perfil aislado | **SUCCESS**; cinco frecuencias × cinco tamaños de bloque | `out/audit-native-vst3/validation.json`, `out/audit-native-vst3/pluginval-detail.txt` |

La aceptación Release conserva los 17 tests y la regresión ASan conserva su
timeout de 300 s. Los fallos anteriores de fixtures/solapamiento y la reproducción
de A14/A15 se conservan en sus logs; las evidencias finales indicadas arriba los
sustituyen para el estado actual. La regresión Release global termina en 157,10 s.

La lectura del fixture de 250.001 eventos pasó de 831,167 ms antes del búfer a
3,667–3,679 ms en la aceptación Release. La planificación de 3.001 proyectos y
6.002 salidas conserva las dos colisiones previstas y tarda 407–420 ms en esa
ejecución. Son mediciones sintéticas locales; no representan una aceleración
equivalente de toda la app ni del renderizado dentro de un DAW.

### Cobertura por hallazgo

- **A01–A02:** `RememberSelectedPreviews` y `RememberOfflineRendering`: elección
  desactivada actual/antigua, identidad y asociación concurrente, proyecto sustituido
  durante el render, campos independientes y fallback/exportación de audio.
- **A03–A05, A08–A09:** `RememberOfflineRendering` y regresión global: edición con
  igual tamaño/fecha, fallo nativo en el segundo formato, cancelación entre formatos,
  fallo de rollback con recuperación conservada, acceso denegado, cancelación de
  playlist, límite exacto de dos millones de eventos y tanda grande con colisiones.
- **A06–A07:** `RememberAppFrontend`: worker retenido, confirmación tardía,
  actualización fallida sin plan anterior arrancable e historial fallido con WAV
  ya generado, conservando el archivo y mostrando Failed.
- **A10–A13:** waveforms, selección y recuperación: cancelación durante hash antes
  de crear salidas, reutilización de WAV, límite inclusivo de 8 MiB y rechazo de
  escrituras grandes sin sustituir el original, fila conocida conservada, fallo
  real de segunda apertura y frecuencia inválida rechazada antes de reservar buffers.
- **A14–A15:** regresión global, sidebar y undo: publicación retenida durante el
  movimiento y orden final, lectura obsoleta reiniciada, escena real de 100/1.000/
  10.000 entradas, búsqueda/favoritos/última fila, selección por ID tras reordenar,
  componentes reciclados y cierre sin esperar al índice.

### Idiomas, interfaz y límites

Los textos nuevos usan el sistema de localización y tienen traducción española.
Se validó el cambio de idioma en los controles afectados y se inspeccionaron PNG
de componentes JUCE en inglés/español: offline a 640×480 y 840×720; aviso del
reproductor a 640 y 1024 píxeles de ancho y modo compacto; app a 640×480 y 1024×768;
plugin a su tamaño normal y escala ampliada. Se mantienen colores/tipos semánticos,
controles nativos, trabajo asíncrono, atajos y reciclado de filas/tags.

Artefactos: `out/t/f57e9c5d9e45/artifacts/`,
`out/t/c96cc7e84f53/artifacts/` y `out/t/14d9dae8cfcc/artifacts/`.
Los snapshots verifican pintura/layout de componentes, no todos los controladores
gráficos. Las pruebas de audio usan los fixtures/driver simulado y el validador
indicados; no certifican todos los DAW/dispositivos reales. El rollback cubre errores
y cancelación durante la publicación; varios archivos no forman una transacción
atómica frente a un corte de energía. No se cambia el alcance de lectores DAW ni
se instala o publica ninguna build.

No quedan IDs abiertos en este inventario. El registro de compatibilidad conserva
las limitaciones funcionales previas del producto.
