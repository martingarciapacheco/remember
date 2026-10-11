# Pausa y reanudación de Remember

Fecha: **11 de octubre de 2026**. Decisión expresa del usuario en la conversación
lateral del proyecto. Este documento fija el alcance nuevo y sustituye el plan de
seguir cerrando exhaustivamente los 22 puntos de la revisión.

## Decisión del usuario

- Pausar ahora, conservar las fuentes y compilaciones actuales y dejar un punto
  claro para retomar. Reanudar únicamente cuando el usuario lo solicite.
- Al retomar, dedicar **unas dos horas** a correcciones y pruebas esenciales.
- Aplazar calibración de BPM/tonalidad, confianza musical, nuevos corpus y otros
  géneros. No lanzar campañas extensas de estrés, escalabilidad, CI remoto,
  instalación o certificación de DAWs como parte de este cierre acotado.
- **Intentar primero leer el BPM del proyecto/transporte del DAW.** Si el host
  no entrega un BPM válido, se permite la estimación por audio existente como
  respaldo, con procedencia clara. Si tampoco hay una estimación válida, mostrar
  desconocido. No atribuir una estimación o un valor antiguo al proyecto actual.
- Aclaración posterior del usuario: la estimación cubre pocos casos de uso y
  basta con su funcionamiento aproximado actual. No mejorarla ni calibrarla
  como tarea prioritaria; esta decisión sustituye el requisito anterior de
  eliminar el fallback.
- Revisar si el host/formato expone la escala del proyecto. Que el DAW la muestre
  en su interfaz no garantiza que el plugin pueda recibirla. No confundir escala,
  acorde y compás. Evitar soluciones basadas en títulos o lectura de proyectos.
- Congelar el detector de tonalidad existente: no calibrarlo ni modificar sus
  perfiles. Cualquier integración de escala del host debe tener procedencia clara
  y sólo entrar en el cierre si hay una API accesible y un cambio pequeño.

## Qué está preservado y comprobado

Directorio: `E:/JUCE PLUGINS/Remember`. Windows, JUCE 8.0.12, Visual Studio.
El árbol de trabajo contiene numerosos cambios y borrados anteriores. No hacer
reset, limpieza general, reinstalación ni copia de binarios sobre la instalación
del usuario. La pausa no exige commit ni publicación.

La mayor parte del trabajo de los 22 puntos ya tiene implementación y pruebas.
El historial detallado sigue en `IMPLEMENTACION_MEJORAS.md`; el diagnóstico
original queda en `ANALISIS_FUNCIONAMIENTO_RENDIMIENTO.md`. Los resultados
históricos no equivalen a una aceptación completa de la versión actual.

Último trabajo de app: `AppShell.cpp/.h` comparte la ventana/bandeja de producción
entre la app y las pruebas; `Main.cpp` mantiene el punto de entrada y delega el
ciclo de vida. Se corrigieron y reprodujeron dos pérdidas de posición:

1. Cerrar antes de completar la lectura de los límites guardados sustituía éstos
   por el tamaño inicial. La geometría inicial de Windows no se considera una
   edición del usuario; una lectura pendiente/fallida conserva el estado previo.
2. Mover, ocultar y salir guardaba la posición anterior. El evento `moved()` vuelve
   a llamar al método base de JUCE para actualizar el último estado visible.

La lectura estricta de límites usa `ProjectScanner::readWindowState` en worker.
Se conservan los movimientos explícitos durante una lectura tardía. Los eventos
nativos y los callbacks de menú prueban cierre seguro y retirada de recursos.

Evidencia comprobada al preparar esta nota:

| Comprobación | Resultado confirmado |
|---|---|
| App Release, batería completa actual | **6/6**, 548,08 s; `build-all.xml`, salida 0 |
| Ventana de producción | Siete casos por suite: cierre temprano, movimiento temprano, restauración, movimiento durante lectura, JSON dañado, bloqueo nativo de lectura y mover/ocultar/salir |
| Bandeja de producción | Dos casos; registro real en Explorer, clic, doble clic, Open/Exit y callback seleccionado tras destruir el dueño |
| Repetición de bandeja | Tres pasadas consecutivas en Release y tres en ASan, aprobadas |
| Contratos de distribución | **89 aprobados**, `release-contracts-accepted.log` |
| App ASan, batería completa actual | **En curso/parcial al consultar**; no había JUnit final ni archivo de salida. No darla por aprobada |
| Plugin/Shared | No modificados por esta tanda de ventana; mantener la evidencia anterior sin presentarla como una nueva ejecución |

Todo lo anterior está en `out/validation-20261011-shell/`. El primer intento de
bandeja tuvo un fallo intermitente de reapertura; se conserva en
`native-release.xml` y su log. El fixture final espera entrada física neutral y
registra visibilidad, peer, dispositivo y modales. Las repeticiones posteriores
pasan; la causa del primer fallo no quedó demostrada por el log inicial.

El snapshot `accepted-source/snapshot.json` archiva **136 archivos** y cuatro
ejecutables, además del runtime ASan y las fuentes de JUCE relevantes. Se verificó
que las fuentes actuales coincidían con ese snapshot antes de escribir esta nota.
No ejecutar de nuevo `archive-current.py`: crea un directorio nuevo y no está
diseñado para reemplazar el archivo existente.

Binarios conservados de la app:

| Binario | SHA-256 |
|---|---|
| `App/build/RememberApp_artefacts/Release/Remember.exe` | `42025f05542f719bfe2606bd1f3dee8ef9ba5ac2e3791e8631599da5c0a3782b` |
| `App/build/RememberAppTests_artefacts/Release/RememberAppTests.exe` | `ce7a9e629e94358c63567e031d8352d7c160dd32ee08efa87683643f13697555` |
| `App/build-asan/RememberApp_artefacts/Release/Remember.exe` | `8c302070d58a794239b3d24aaee0f29a12226302882e8e1524e6ded23aed6bd6` |
| `App/build-asan/RememberAppTests_artefacts/Release/RememberAppTests.exe` | `a11512d45f02138f1def6a7a1fc7bd9c9882f33bfa0e41fd8ab09a45300a28e3` |

Los dos modos compilaron correctamente. No se ha instalado ni publicado esta
tanda. Los ensayos verifican código local y perfiles aislados, no ausencia
absoluta de fallos en hardware/DAWs reales ni el arranque completo del ejecutable.

## Estado de la pausa y procesos

La orden de pausa del usuario es explícita. Esta conversación lateral no dispone
de un objetivo persistente propio. El intento autorizado de enviar al chat
principal la pausa devolvió un error de la app; **no confirma el cambio del
objetivo principal a `paused`**. El chat principal aparecía inactivo al consultar,
pero su comando de CTest podía seguir ejecutándose en segundo plano.

Chat principal: `01a1125d-37c9-7a72-b5c2-66ecc91c2d31`, título
«Analiza rendimiento del plugin y la», host `local`. Última sesión de ejecución
conocida del chat principal: **43282**, CTest Release seguido de ASan.
Release terminó con salida 0; ASan seguía en la regresión al leer su log.

Para hacer efectiva la pausa del objetivo hay que detener/pausar ese chat o
transmitirle la instrucción desde una sesión que permita hacerlo. Detener sólo
sus procesos de prueba identificados; nunca procesos instalados de Remember,
DAWs ni compilaciones ajenas. Guardar cualquier ejecución interrumpida como
parcial. Si existe después `out/validation-20261011-shell/PAUSA_PROCESOS.md`, leerlo
antes de asumir el estado final. No reiniciar comprobaciones al cerrar esta nota.

## Prioridad del DAW y respaldo de BPM

**Conservar el fallback de detección por audio existente.** Al retomar, comprobar
que un BPM válido del DAW tiene prioridad. No rehacer ni afinar el estimador:
su precisión aproximada actual es suficiente para este alcance.

- `Plugin/Source/RecordingCore.cpp` lee `PositionInfo::getBpm()` al capturar y
  conserva mínimo/máximo para señalar variaciones del transporte.
- En la publicación selecciona actualmente `slot.bpm > 0 ? slot.bpm :
  bpmEstimate.bpm` y `bpmSource` como `host`/`estimated`.
- `Plugin/Source/PreviewAnalysis.cpp` calcula características y estima BPM cuando
  `Source::hostBpm` es falso. Mantener esa ruta como respaldo cuando no hay tempo
  válido del host; con BPM del DAW, evitar estimación innecesaria de tempo.
- Revisar los caminos de restauración/reutilización de metadatos de
  `RecordingCore.cpp`. Una nueva captura sin BPM del host no debe heredar el BPM
  de una preview previa como si procediera del proyecto actual.
- La app debe distinguir BPM del host de estimaciones heredadas. No presentar
  datos `estimated` o sin procedencia como BPM confirmado del DAW. Conservar los
  archivos históricos; evitar migraciones masivas o borrado de grabaciones.
- Conservar el BPM real del transporte, incluidos cambios de tempo y valores
  fuera de los rangos usados por el detector. Validar valores ausentes, cero,
  negativos o no finitos. No usar 120 ni otro valor predeterminado como fallback.

## Plan de reanudación: aproximadamente 120 minutos

| Tiempo | Trabajo |
|---|---|
| 0–10 min | Leer esta nota, estado de procesos y sólo los logs necesarios. Verificar cambios desde el snapshot y localizar fallos confirmados. No repetir suites ya aprobadas sin un cambio relevante. |
| 10–25 min | Comprobar prioridad del BPM del DAW y procedencia de metadatos con las pruebas existentes. Corregir sólo un fallo confirmado; conservar el estimador de respaldo sin calibrarlo ni rediseñarlo. |
| 25–35 min | Comprobar si existe una vía pequeña y documentada para escala del host. Si requiere extensión específica, investigación de DAWs o cambios amplios, anotarla como aplazada. Priorizar la estabilidad. |
| 35–90 min | Corregir sólo defectos confirmados de funcionamiento/rendimiento pendientes y compilar los componentes afectados sin copia automática a la instalación. El tiempo liberado de BPM se destina a estabilidad; adaptar contratos sólo cuando cambie el comportamiento. |
| 90–110 min | Ejecutar pruebas esenciales de captura/publicación, configuración, reproductor y ventana/bandeja; usar ASan de forma dirigida para vida útil. Si una prueba falla, corregir y repetir únicamente lo afectado. |
| 110–120 min | Registrar resultados reales, hashes/binaries de los cambios, limitaciones y punto de parada. No iniciar nuevas campañas. Informar de cualquier fallo o tarea que quede fuera del límite. |

Pruebas esenciales seleccionadas, usando fixtures existentes cuando sea posible:

- BPM presente, ausente/inválido y cambiado durante captura: prioridad y
  procedencia `host` cuando el DAW lo proporciona; respaldo `estimated` cuando
  no lo proporciona, o desconocido si no hay resultado válido. Comprobar que
  con tempo válido del host no se necesita estimar BPM por audio. Basta una
  comprobación básica del respaldo; no medir precisión por géneros o corpus.
- Nueva captura sobre entrada antigua con BPM estimado/host: no atribuir tempo
  anterior al DAW cuando éste no lo proporciona ahora.
- Capturar y publicar una preview conservando audio, identidad y organización;
  passthrough y carga básica del VST3 (`RememberRegression`, `RememberHostLoading`
  o contratos equivalentes adaptados al nuevo comportamiento).
- Cambio/cierre durante carga del reproductor, preferentemente mediante el caso
  enfocado existente. No abrir dispositivos físicos en las pruebas automáticas.
- Configuración/persistencia básica. `RememberAppShell` y `RememberAppTray` sólo
  si el cambio toca la app; bandeja únicamente con Explorer disponible.

No borrar ni desactivar permanentemente tests para reducir duración. Seleccionar
las pruebas por nombre/caso. Conservar expectativas de prioridad del host y
respaldo existentes; adaptarlas sólo si un arreglo exige un cambio real. No
silenciar fallos ni dar suites no ejecutadas por aprobadas. Si no cabe terminar
con seguridad en dos horas, dejar
el cambio pendiente/aislado y explicar exactamente qué falta.

## Pendientes explícitamente fuera del cierre

Calibración musical, nuevos géneros, disco frío, fallo de disco físico/driver,
matriz exhaustiva de DAWs/instancias, selectores nativos completos, CI remoto,
pipeline final de release e instalación/actualización/desinstalación. Mantener
estas limitaciones documentadas. No marcar los 22 puntos como aceptación total
por haber reducido el alcance.
