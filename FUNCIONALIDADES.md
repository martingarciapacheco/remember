# Funcionalidades elegidas

Rama: `feature/remember-workflow-controls`.

Esta numeración corresponde a la propuesta de funcionalidades del chat, no a los
IDs de la auditoría de rendimiento en `IMPLEMENTACION_MEJORAS.md`.

| ID | Alcance acordado | Implementación |
|---|---|---|
| 01 | Opción en plugin y Settings | Capture from any position en el plugin activa el modo manual solo en ese proyecto y se guarda en su estado del DAW. Capture next play arma desde el cursor actual o la próxima reproducción. Settings establece únicamente el valor por defecto para nuevas instancias; no modifica instancias existentes ni proyectos guardados. Los estados antiguos sin esta opción conservan captura automática al inicio. |
| 02 | Apuntado, aplazado | Sección de preview persistente por proyecto, loops y duración por compases. Sin implementación nueva. |
| 03 | Opción en plugin y Settings | Lock preview updates es compartido por todas las instancias. Project details permite además bloquear una preview individual. |
| 04 | Opción en plugin y Settings | Replace whole preview elimina la cola antigua; desactivado mantiene la política existente de sustituir el prefijo y conservar el resto. La opción se fija al empezar cada captura. |
| 05 | Excluido | Sin navegador de historial/versiones. Las revisiones internas de integridad existentes se conservan para recuperación. |
| 06 | Apuntado, aplazado | Flujo adicional de ideas independientes sin guardar el proyecto. No se amplía la asociación/conservación que ya existía en la rama de partida. |
| 07 | Implementar | Filtros combinables por BPM, tonalidad, DAW, etiquetas, estado, fechas, favoritos y carpeta; búsqueda por varias palabras/notas. Ordenación por fecha, nombre y BPM. |
| 08 | Implementar | Colecciones inteligentes persistentes: seleccionar una colección aplica sus filtros; + permite guardar o eliminar colecciones. |
| 09 | Implementar | Ctrl/Shift seleccionan varias entradas. Actions aplica altas/bajas de etiquetas, carpetas, favoritos y estado a la selección; borrado/restauración en lote. |
| 10 | Implementar | Project details: Idea, In progress, Finished, Archived y notas. |
| 11 | Implementar | Correcciones manuales de BPM y tonalidad separadas de los resultados de análisis; sobreviven a la recaptura y se muestran en app/plugin. Vaciar la corrección devuelve al análisis automático. |
| 12 | Implementar como reproductor Spotify | Play/pause, previous, next, shuffle con bolsa sin repeticiones, historial real para previous, avance al terminar y repetición off/all/one. Previous reinicia tras tres segundos; con menos vuelve a la preview anterior. Ctrl+izquierda/derecha; autoplay al seleccionar configurable. |
| 13 | Implementar como reproductor Spotify | Volumen persistente, mute que conserva volumen, selector de salida existente y normalización opcional por preview a -14 LUFS durante reproducción. |
| 14 | Papelera de 30 días | Borrar mueve audio y metadata a .trash; vista Trash permite restaurar. La indexación elimina contenido caducado a partir de 30 días y conserva tombstones contra escritores obsoletos. Ctrl+Z/Actions deshace etiquetas y carpetas mediante deltas inversos, agrupados por acción y conservando cambios concurrentes ajenos (32 acciones/16 MiB, sesión actual). |
| 15 | Implementar | Vista Missing projects: reúne proyectos no localizados; seleccionar una preview mantiene escucha y Assign path; Settings permite ampliar raíces y Refresh reintenta resolución. |
| 16 | Excluido | Sin nuevas acciones de exportación/arrastre de audio externo. |

Settings guarda el valor por defecto de 01 y las opciones compartidas 03/04 en
config.json desde workers. El modo manual del plugin se guarda en el estado del
proyecto del DAW: ni las consultas de Settings, ni reabrir el editor, ni una nueva
preparación de audio sustituyen esa elección. 03/04 siguen compartidas y las
instancias activas las adoptan en aproximadamente un segundo; el plugin que las
edita las aplica al confirmar, sin modificar el valor por defecto de 01.
El bloqueo se vuelve a comprobar antes de publicar audio y dentro de la
transacción de una entrada bloqueada. Duración/calidad/mono mantienen el contrato
de preparación y de capturas en cola de la implementación anterior.

La normalización mide el OGG decodificado en segundo plano con K-weighting y
gates absoluto/relativo BS.1770. Usa el objetivo normal de Spotify (-14 LUFS) y
limita ganancia para dejar 1 dB de margen de pico de muestra decodificada. No es
una certificación true-peak ni implementa el limitador del modo Loud de Spotify.
El audio guardado y la señal del DAW no se alteran por los controles de escucha.

Referencias: [Spotify: normalización](https://support.spotify.com/us/artists/article/loudness-normalization/),
[ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I/en).

## Comprobación

Las suites nuevas son `RememberWorkflowCapture` y `RememberAppWorkflow`. Se
añaden a las regresiones de captura, configuración, almacenamiento, interfaz y
reproducción existentes.

Validación del 12 de octubre de 2026:

- Compilación Release de la app, VST3 y ejecutables de pruebas: correcta.
- App Release de la implementación inicial: 8/8 suites correctas, incluidos biblioteca de 10.000
  entradas, arranque en frío, shell y bandeja nativa de Windows.
- Tras separar el modo manual por proyecto: 4/4 suites de app correctas
  (regresión general, workflow, frontend y recuperación de almacenamiento).
- Plugin Release: 14/14 suites finales correctas, incluida captura sostenida,
  sincronización app/plugin, interfaz nativa, asociación y carga en host.
- ASan de la implementación inicial: 4/4 suites de app (Regression, Workflow, Frontend y StorageRecovery),
  sin errores del sanitizer. La escena de 10.000 entradas conserva selección por
  identidad al reordenar, controles reciclados y cierre seguro con los plazos originales.
- Pluginval 1.0.4, strictness 5: correcto en 44,1/48/88,2/96/192 kHz y bloques
  32/64/128/512/2048. El VST3 final conserva el hash SHA-256
  `3BB0620CDCC42D803C86E71DA6EE1C60A6C28CF84CC4C44154020218CD6D0CF3` validado
  después de separar el modo manual por proyecto del valor por defecto de Settings.
- Snapshots nativos revisados: app 640×480 y 1024×768, plugin 440×496,
  Settings y formularios con scroll (incluido Project details a 480×360).
  Se conserva el sistema visual compartido y los controles nativos de ventana.

Los logs, JUnit y snapshots aislados se guardan en `out/`; las pruebas no usan
la biblioteca ni los settings personales. La normalización y sus límites se
describen arriba; esta validación no sustituye una sesión manual en cada DAW.

Resultados de la corrección por proyecto: `out/project-manual-app-junit.xml` y
`out/project-manual-plugin-junit.xml`. La suite de captura comprueba el valor por
defecto de nuevas instancias, la conservación de instancias existentes, el switch
local sin escritura del valor por defecto, reapertura con valores true/false,
estados antiguos sin atributo y nueva preparación de audio.
ASan inicial: `out/workflow-app-asan-final-junit.xml`. Pluginval:
`out/validation-project-manual-native/validation.json`.
