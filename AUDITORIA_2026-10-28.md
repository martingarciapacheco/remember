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
| A01 | P1 | `PreviewSelection::disable` conserva procedencia `render`; `select(..., false)` puede volver a activarla pese a la decisión explícita de volver a captura. | Desactivar cualquier selección cuenta como decisión manual; un nuevo render no la cambia. | Pendiente |
| A02 | P1 | La asignación automática y `inheritRender` verifican projectId pero no vuelven a comprobar la asociación de proyecto dentro de la transacción; una entrada reasociada puede recibir el render anterior. La comparación de IDs vacíos también acepta identidad desconocida. | Rechazar asociación obsoleta/identidad desconocida bajo lock; conservar elecciones y otros campos concurrentes. | Pendiente |
| A03 | P1 | `outputStamp` sólo incluye ID/tamaño/mtime; una edición in situ de igual longitud y fecha restaurada elude la confirmación de sobrescritura. | Comprobar contenido además de identidad y conservar cambios concurrentes. | Pendiente |
| A04 | P1 | `render` publica formatos uno a uno; un fallo o cancelación tras el primero conserva una tanda parcial y limpia el staging restante. | Deshacer una publicación incompleta; conservar originales y audio recuperable si falla el deshacer. | Pendiente |
| A05 | P2 | `collectProjects` usa el iterador JUCE sin resultado de errores de enumeración; una subcarpeta ilegible puede devolver éxito y un inventario parcial. | Fallar explícitamente y descartar inventario parcial ante errores; mantener recursión explícita y evitar enlaces. | Pendiente |
| A06 | P1 | `stop` sólo cambia `batch`/señal; las finalizaciones de inicio/refresh/execute y la confirmación inicial aún pueden instalar planes, continuar trabajo o marcar Completed tras parar. | Cancelar impide continuaciones/confirmaciones obsoletas; permite replanificar y cerrar sin bloquear UI. | Pendiente |
| A07 | P2 | Error del worker se atiende antes de la finalización específica: un fallo guardando historial deja fila Rendering; error de refresh deja el plan anterior arrancable. | Estado de fila/plan coherente ante fallo, cancelación y reintento; feedback bilingüe. | Pendiente |
| A08 | P2 | `readProjectTiming` lee eventos byte a byte directamente de `FileInputStream` y no comprueba cancelación en el bucle interno del playlist ni limita eventos. | Búfer acotado, cancelación dentro del playlist, presupuesto de eventos y estimaciones equivalentes. | Pendiente |
| A09 | P2 | `planBatch` deduplica rutas con búsquedas lineales de StringArray, repetidas por cada salida: coste cuadrático en tandas grandes. | Conjuntos de rutas normalizadas; mantener orden y reporte de colisiones exacto; medir tanda grande. | Pendiente |
| A10 | P2 | `PreviewExport::prepare` calcula dos SHA de archivo completos sin consultar cancelación durante la lectura; ahora puede recibir renders externos de horas. | Hash por bloques cancelable, errores de lectura explícitos y mismos nombres/reutilización de exportaciones. | Pendiente |
| A11 | P2 | `readEntryMetadata` carga el archivo entero sin límite, a diferencia de configuración; recuperación y `LibraryStore::readJson` también pueden volver a leerlo sin límite. | Límite explícito y compartido antes/durante lectura; no reparar/sobrescribir archivos enormes; preservar entradas sanas. | Pendiente |
| A12 | P2 | Si falla `WaveformEnvelope::read`, el player sólo cambia descripción; con normalización `play` sigue esperando un waveform que nunca llegará. | Salir de espera, mostrar error accionable y permitir reproducción al desactivar normalización; probar carga/cancelación siguiente. | Pendiente |

## Validación inicial

- Compilaciones app, VST3, RememberAppTests y RememberTests Release correctas:
  `out/audit-baseline-{app,plugin}-build.log`.
- CTest app inicial: 11/15; los fallos incluyen creación de junction (Win32 5),
  workflow y shell/tray dentro del sandbox. Reejecución nativa aislada en curso;
  no se clasifican todavía como defectos del producto.
- CTest plugin y reejecución app nativa: en curso al cerrar este inventario.
- No se ha cambiado implementación durante la fase 1. No se instala, publica ni
  modifica ningún proyecto de usuario como parte de estas comprobaciones.

## Registro de la fase 2

Se completará cada ID con cambio, regresión y resultado observado. La aceptación
final incluye builds app/VST3, CTest completo, comprobaciones de memoria pertinentes
y snapshots JUCE mínimos/normales en inglés y español para las interfaces afectadas.
