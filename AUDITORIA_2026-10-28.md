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
fallo y evidencia final. Esta tabla se registra antes de cambiar implementación.
P1 = pérdida de decisiones/archivos o ejecución incorrecta; P2 = robustez/rendimiento.
Todos los hallazgos iniciales están confirmados por el camino de código indicado;
las reproducciones y resultados se incorporarán al completar cada corrección.

## Inventario de la fase 1

| ID | Prioridad | Fallo / mejora y evidencia inicial | Criterio de cierre | Estado |
|---|---|---|---|---|
| A01 | P1 | `PreviewSelection::disable` conserva procedencia `render`; `select(..., false)` puede volver a activarla pese a la decisión explícita de volver a captura. | Desactivar cualquier selección cuenta como decisión manual; un nuevo render no la cambia. | Corregido; regresión focalizada aprobada |
| A02 | P1 | La asignación automática y `inheritRender` verifican projectId pero no vuelven a comprobar la asociación de proyecto dentro de la transacción; una entrada reasociada puede recibir el render anterior. La comparación de IDs vacíos también acepta identidad desconocida. | Rechazar asociación obsoleta/identidad desconocida bajo lock; conservar elecciones y otros campos concurrentes. | Corregido; regresión focalizada aprobada |
| A03 | P1 | `outputStamp` sólo incluye ID/tamaño/mtime; una edición in situ de igual longitud y fecha restaurada elude la confirmación de sobrescritura. | Comprobar contenido además de identidad y conservar cambios concurrentes. | Corregido; regresión focalizada aprobada |
| A04 | P1 | `render` publica formatos uno a uno; un fallo o cancelación tras el primero conserva una tanda parcial y limpia el staging restante. | Deshacer una publicación incompleta; conservar originales y audio recuperable si falla el deshacer. | Corregido; regresión focalizada aprobada |
| A05 | P2 | `collectProjects` usa el iterador JUCE sin resultado de errores de enumeración; una subcarpeta ilegible puede devolver éxito y un inventario parcial. | Fallar explícitamente y descartar inventario parcial ante errores; mantener recursión explícita y evitar enlaces. | Pendiente |
| A06 | P1 | `stop` sólo cambia `batch`/señal; las finalizaciones de inicio/refresh/execute y la confirmación inicial aún pueden instalar planes, continuar trabajo o marcar Completed tras parar. | Cancelar impide continuaciones/confirmaciones obsoletas; permite replanificar y cerrar sin bloquear UI. | Pendiente |
| A07 | P2 | Error del worker se atiende antes de la finalización específica: un fallo guardando historial deja fila Rendering; error de refresh deja el plan anterior arrancable. | Estado de fila/plan coherente ante fallo, cancelación y reintento; feedback bilingüe. | Pendiente |
| A08 | P2 | `readProjectTiming` lee eventos byte a byte directamente de `FileInputStream` y no comprueba cancelación en el bucle interno del playlist ni limita eventos. | Búfer acotado, cancelación dentro del playlist, presupuesto de eventos y estimaciones equivalentes. | Pendiente |
| A09 | P2 | `planBatch` deduplica rutas con búsquedas lineales de StringArray, repetidas por cada salida: coste cuadrático en tandas grandes. | Conjuntos de rutas normalizadas; mantener orden y reporte de colisiones exacto; medir tanda grande. | Pendiente |
| A10 | P2 | `PreviewExport::prepare` calcula dos SHA de archivo completos sin consultar cancelación durante la lectura; ahora puede recibir renders externos de horas. | Hash por bloques cancelable, errores de lectura explícitos y mismos nombres/reutilización de exportaciones. | Pendiente |
| A11 | P2 | `readEntryMetadata` y `LibraryStore::readJson` cargan el archivo entero sin límite, a diferencia de configuración. Esto incluye recibos/backups leídos por recuperación; su manifest principal ya tenía límite. | Límite explícito y compartido antes/durante lectura y antes de guardar; no reparar/sobrescribir archivos enormes; preservar entradas sanas. | Pendiente |
| A12 | P2 | Si falla `WaveformEnvelope::read`, el player sólo cambia descripción; con normalización `play` sigue esperando un waveform que nunca llegará. | Salir de espera, mostrar error accionable y permitir reproducción al desactivar normalización; probar carga/cancelación siguiente. | Pendiente |
| A13 | P1 | Hallazgo adicional al verificar A12: el waveform acepta frecuencias de muestreo finitas sin máximo, aunque read-ahead rechaza más de 768 kHz. Una cabecera WAV dañada puede pedir miles de millones de Hz y una ventana de medición de varios GiB. | Rechazar esa cabecera antes de reservar memoria, aplicar el mismo máximo que reproducción y comprobar el caso real. | Pendiente |
| A14 | P2 | La regresión ASan reproduce `Root drop did not preserve insertion position`: el índice puede publicar el nombre nuevo entre rename, cambio de orden y cambio a orden manual de una misma operación. | Publicar sólo observaciones completas de ediciones de carpetas; probar el índice reteniendo la fase de metadatos y revalidar el gesto/drop y undo. | Pendiente |

## Validación inicial

- Compilaciones app, VST3, RememberAppTests y RememberTests Release correctas:
  `out/audit-baseline-{app,plugin}-build.log`.
- CTest app inicial: 11/15; los fallos incluyen creación de junction (Win32 5),
  workflow y shell/tray dentro del sandbox. Reejecución nativa aislada en curso;
  no se clasifican todavía como defectos del producto.
- CTest plugin 16/16 (352,01 s) y reejecución app nativa 4/4 (219,05 s), incluidos shell/tray y regresión global: aprobados.
- No se ha cambiado implementación durante la fase 1. No se instala, publica ni
  modifica ningún proyecto de usuario como parte de estas comprobaciones.

## Registro de la fase 2

Se completará cada ID con cambio, regresión y resultado observado. La aceptación
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

Los últimos puntos aún requieren terminar compilación y regresiones finales;
no se consideran cerrados sólo por estar implementados.

A13 se registra durante la revisión de cierre de A12, antes de corregirlo;
amplía el inventario a 13 hallazgos y se atiende a continuación.
