# Contexto de Remember para continuar en otro ordenador

Preparado el **14 de noviembre de 2026**. Es una memoria de proyecto portable:
conserva decisiones verificables y orienta la lectura del repositorio. No es una
exportación de la memoria interna del modelo ni de todas las conversaciones.
El estado que cambia entre sesiones está en [ESTADO_TRABAJO.md](ESTADO_TRABAJO.md).

## Cómo recuperar el contexto

1. Leer [AGENTS.md](AGENTS.md), este archivo y `ESTADO_TRABAJO.md`.
2. Comprobar el Git y el entorno reales; después leer las secciones pertinentes
   del [README](README.md) y los documentos especializados de la tabla siguiente.
3. Identificar la petición actual del usuario y continuar desde ella. Un informe
   antiguo o una propuesta no autoriza a reanudar automáticamente todo su trabajo.
4. Al terminar una tanda, actualizar el estado con cambios, pruebas realmente
   ejecutadas, limitaciones y siguiente paso. Registrar las decisiones duraderas
   en este archivo o en su guía especializada, evitando duplicar grandes informes.

Las instrucciones nuevas del usuario prevalecen sobre notas históricas.
Cuando dos documentos describan estados distintos, contrastar fechas, commit y
código antes de elegir. En particular, la pausa de octubre no describe por sí
sola el estado de noviembre ni bloquea encargos posteriores del usuario.

## Qué es el producto

Remember combina un **plugin VST3 transparente en el master del DAW** y una
**app independiente para escuchar y organizar previews de proyectos musicales**.
Código C++17 con JUCE 8, CMake y componentes nativos. Hay Windows x64 y macOS
universal. No es una web ni debe sustituirse su interfaz por una implementación web.

El plugin captura normalmente desde el cero del transporte, hasta 60 segundos
por defecto. Existe captura desde cualquier posición como opción. Una captura
menor de un segundo se descarta; una recaptura más corta conserva la cola previa.
La captura y el descubrimiento siguen funcionando con el editor cerrado.
No cambiar el audio que atraviesa el plugin.

La app organiza grabaciones, etiquetas, favoritos, carpetas/carpetas inteligentes,
notas, estados y papelera; reproduce audio, exporta WAV y permite abrir o reasociar
proyectos. También incluye Used Sounds, Statistics y renderizado offline, cuyos
límites son independientes. El README describe el comportamiento actual.

## Requisitos y decisiones confirmadas

- **Dos plataformas en cada release de instaladores.** Windows x64 y macOS
  universal `arm64` + `x86_64`, mismo código comprometido y mismo `VERSION`.
  Publicar sólo tras builds nativas, regresiones, pluginval y verificación de
  contenido de ambos paquetes. Usar `installer/build-installers.ps1` y
  `.github/workflows/release-installers.yml`; no publicar sólo Windows.
- **Diseño Apple en app y plugin.** Leer `DESIGN.md`; ampliar los tokens,
  controles, colores y foco de `Shared/RememberLookAndFeel.h`. Aplicar la skill
  `apple-design` cuando esté instalada, adaptada a JUCE. Conservar controles
  nativos de ventana, atajos, asincronía y listas/etiquetas recicladas. Revisar
  snapshots nativos a tamaños mínimo y normal y compilar app y VST3.
- **Tipografía y temas.** Elección expresa: Inter integrada en Windows y San
  Francisco del sistema en macOS. Opciones System/Light/Dark; conservar la paleta
  semántica y el gris muy oscuro del tema Dark. No incrustar fuentes SF de Apple
  en Windows. La guía especializada contiene tamaños y reglas de interacción.
- **Inglés y español de España.** Toda modificación debe conservar ambos idiomas,
  selector compartido y actualización de interfaces abiertas. El español usa tú
  y conserva preview, sample, playlist, plugin, preset y notación musical.
  No traducir nombres/rutas/notas/etiquetas del usuario ni identificadores internos.
- **Compatibilidad por función y DAW.** Leer y actualizar `DAW_COMPATIBILITY.md`
  al cambiar soporte. Statistics funcionando no implica Used Sounds funcionando,
  ni detectar/asociar una extensión implica poder leer todo su proyecto.
- **Ordenador de destino y DAW.** El usuario indicó MacBook Pro con chip M5 Pro
  y FL Studio como DAW principal para la revisión de compatibilidad Mac.
- **Git.** En varios encargos de auditoría y releases el usuario pidió commit y
  merge directo en `main`, sin PR. Es una preferencia observada y debe respetarse
  en esos encargos; no convierte cualquier tarea en permiso para publicar una
  release, sobrescribir archivos o subir datos personales. Respetar también las
  peticiones concretas de rama nueva, como ocurrió con auditorías y frontend.
- **Auditorías.** En la auditoría del 28 de octubre pidió guardar primero el
  estado previo, trabajar en rama nueva y separar inventario de fallos de sus
  correcciones ordenadas. Conservar una lista de cierre y evidencia real cuando
  se solicite ese procedimiento; no reabrir automáticamente los hallazgos cerrados.
- **BPM y tonalidad.** La decisión recogida en `PAUSA_Y_REANUDACION.md` es dar
  prioridad al BPM válido del host y conservar la estimación existente como
  respaldo, con procedencia clara. No iniciar recalibración de BPM/tonalidad ni
  campañas nuevas de corpus como prioridad sin otro encargo. Desconocido no es
  120 BPM; un valor antiguo no se atribuye al proyecto actual. La escala visible
  en un DAW no demuestra que VST3 la exponga.

### Renderizado offline y previews elegidas

Las decisiones completas están en [OFFLINE_RENDERING.md](OFFLINE_RENDERING.md).
Usar comandos oficiales del DAW, sin automatización de su UI ni escrituras de
ajustes no documentadas. El usuario aceptó una primera versión que reutiliza
los ajustes guardados de FL y los muestra en **sólo lectura**. Su edición queda
aplazada. Valores desconocidos deben seguir apareciendo como desconocidos.

Pedir confirmación en el producto antes de sobrescribir resultados y antes de
asignar renders completos a previews en lote. Si FL está abierto, pedir guardar
y cerrar antes del lote. Los destinos personalizados son planos y sus colisiones
se revisan. La selección manual de audio y la vuelta explícita a la captura deben
resistir posteriores asignaciones automáticas. Conservar la captura original.

La prueba real confirmó WAV/MP3/OGG/FLAC, 48 kHz estéreo y 24 bits en WAV/FLAC.
No demostró que cada ajuste guardado de interpolación, HQ, polifonía, dithering,
efectos o colas produzca el mismo resultado por CLI que mediante render manual.
No convertir una lectura de ajustes o una plantilla vacía en esa garantía.

## Mapa del código

| Área | Puntos de entrada y responsabilidades |
|---|---|
| Plugin | `Plugin/Source/PluginProcessor.*`, `PluginEditor.*`: integración VST3/editor. `RecordingEngine.*` y `RecordingCore.*`: captura, transporte, asociación y publicación. `PreviewAnalysis.*`, `BpmDetector.*`, `KeyDetector.*`: análisis. |
| App y biblioteca | `App/Source/Main.*`, `AppShell.*`, `MainComponent.*`: arranque/ventana/ciclo de vida. `ProjectScanner.*`, `LibraryWatch.*`, `ProjectListComponent.*`, `FoldersSidebarComponent.*`: índice, listas y organización. |
| Reproducción | `App/Source/WaveformPlayerComponent.*`, `ReadAheadSource.*`, `WaveformEnvelope.*`, `PreviewSelection.*`, `PreviewExport.h`: carga, reproducción, waveform y exportación. |
| Funciones DAW | `App/Source/UsedSounds.*`, `ProjectStatistics.*`, `OfflineRendering.*` y sus diálogos. `Shared/FlStudioPaths.h`: rutas de FL. |
| Persistencia | `Shared/LibraryStore.*`, `AtomicJsonWriter.h`, `ProjectIdentity.*`, `PreviewAudio.*`, `PreviewCompaction.*`: identidades, transacciones, paquetes y recuperación. |
| Concurrencia y cierre | `Shared/BackgroundRetirement.*`, `RememberInterProcessLock.h`: vida útil, retirada y exclusión. Mantener recursos/código vivos hasta finalizar los hilos nativos. |
| Diseño e idiomas | `Shared/RememberLookAndFeel.h`, `RememberTypography.h`, `RememberDialogs.h`, `RememberLanguage.*`, `LanguageSettings.h`, `LanguageSync.h`, `SpanishSpain.h`, `SpanishDiagnostics.h`. |
| Build y distribución | CMake separado en `App/` y `Plugin/`; `Shared/RememberJuce.cmake`, `RememberVersion.cmake`; `installer/`, `.github/workflows/`, `Tests/`. |

## Fronteras que conviene preservar

- Mantener I/O, decodificación, FFT, escaneo, hashes y guardados fuera de callbacks
  de audio y pintura. Reutilizar workers cancelables y límites de memoria.
- Validar identidad de grabación/proyecto y resultados tardíos: una fila reciclada
  o un menú/selector abierto no debe editar al nuevo ocupante de la fila.
- Los cambios concurrentes de app e instancias del plugin deben conservar campos
  ajenos mediante el protocolo compartido. No sustituirlo por guardados ingenuos.
- Mantener audio confirmado y organización ante errores. No limpiar revisiones,
  propietarios desconocidos o capturas pendientes sin seguir recuperación.
- Las previews nuevas usan paquetes `.rmp` autocontenidos con fragmentos OGG.
  Seguir leyendo WAV/OGG antiguos. Instalar app y plugin compatibles juntos:
  beta5 antigua no lee el nuevo formato. No migrar ni borrar audio masivamente.
- No abrir dispositivos físicos innecesariamente al arrancar/navegar: la creación
  del audio manager/CoreMIDI fue un bloqueo real del arranque Mac ya corregido.
- Mantener el plugin independiente del LookAndFeel global del DAW y conservar
  cancelación, SafePointer/identidades y retirada segura al cerrar o descargar.

## Límites actuales que no se deben olvidar

**Actualización de desarrollo recuperada de GitHub el 14 de noviembre:** la rama
`codex/macos-feature-parity`, commit `93461c3`, es el punto de continuidad elegido
por el usuario. Añade FSEvents, asociación automática Mac parcial mediante AppKit,
render FL nativo parcial, controles multimedia, reapertura del Dock y recuperación
de rutas Windows importadas. **La tabla siguiente describe la beta6 publicada**;
para desarrollar en esta rama prevalecen las secciones de desarrollo de
`MAC_COMPATIBILITY.md` y `DAW_COMPATIBILITY.md`. No volver a eliminar esas mejoras
por interpretar las limitaciones históricas como requisitos actuales.

La asociación necesita aceptación en hosts reales. La auditoría de rutas encontró
14 entradas enlazadas, 34 recuperables y una sin coincidencia (`JJ`), sin modificar
manifiestos del usuario. La instalación local del VST3 del 13 de noviembre está
registrada en la guía Mac, con backup; verificar qué binario carga el DAW antes de
reinstalar. Xcode 27.2 obligó a usar deployment target local 12.0; la release sigue
teniendo objetivo 11.0 pendiente de validación con toolchain compatible. No se ha
publicado instalador de esta rama ni se ha revalidado Windows en su trabajo Mac.

| Función | Cobertura registrada en beta6 |
|---|---|
| App + VST3 | Windows x64 y macOS universal; Mac mínimo 11. Sin AU para Logic/GarageBand ni AAX para Pro Tools. |
| Asociación | Automática por ventanas del host en Windows, con límites y ambigüedades. Manual/Keep en Mac. |
| Used Sounds | Proyectos FL Studio guardados; samples, MIDI y generadores. No leer estados opacos como si se conociesen. |
| Statistics | Lectores FL Studio y Ableton Live guardados. No equivale a soporte de Used Sounds para Ableton. |
| Renderizado offline | FL Studio en Windows. No implementado en Mac ni para Ableton. Sin stems, MID o M4A. |
| Rutas entre sistemas | Una ruta `E:\...` no se transforma automáticamente en una ruta Mac válida. No adivinar equivalencias. |
| Firma Mac | Binarios con firma ad-hoc; paquete sin Developer ID ni notarización. |
| Pruebas | Los runners y snapshots no certifican el Mac físico, todos los DAW, Intel/Rosetta ni todos los macOS admitidos. |

## Documentos de referencia

| Archivo | Cuándo leerlo |
|---|---|
| `README.md`, `BETA_NOTES.md` | Funciones actuales, instalación, atajos, builds, regresiones y límites de la beta. |
| `DESIGN.md` | Antes de tocar frontend; diseño, interacción, medidas y validación. |
| `LOCALIZATION.md` | Traducciones, terminología, persistencia y pruebas EN/ES. |
| `DAW_COMPATIBILITY.md` | Antes de modificar lectores/funciones DAW; matriz, evidencia y pendientes. |
| `MAC_COMPATIBILITY.md` | Portabilidad, firma, publicación conjunta, evidencia beta6 y aceptación física pendiente. |
| `DISPLAY_INVESTIGATION.md` | Investigación de pantalla del Mac; consultar sus conclusiones y pendientes antes de retomar ese trabajo. |
| `OFFLINE_RENDERING.md` | Decisiones y límites de renders/selección de previews. Sus secciones históricas no anulan la implementación posterior. |
| `Shared/ProjectBackups.md` | Detección/exclusión de backups de proyectos. |
| `AUDITORIA_2026-10-28.md` | Inventario y cierre de 15 hallazgos. No volver a tratarlos como abiertos sin reproducción actual. |
| `PAUSA_Y_REANUDACION.md` | Alcance histórico de la pausa de octubre y decisiones sobre análisis musical. No es el estado actual del checkout. |
| `IMPLEMENTACION_MEJORAS.md`, `ANALISIS_FUNCIONAMIENTO_RENDIMIENTO.md`, `FUNCIONALIDADES.md` | Antecedentes técnicos. Contrastar con README, documentos nuevos y código. |

## Procedencia de las decisiones recuperadas

Se revisaron los documentos anteriores, el historial Git y mensajes de estas
conversaciones accesibles en el ordenador de origen. Los IDs sirven como índice
si la app puede consultarlos; el Mac no necesita esos chats para leer esta memoria.

| Conversación | ID | Contexto recuperado |
|---|---|---|
| Prepara migración completa a Mac | `01a1c762-fa4b-7422-b8d9-faf6b3cb8b57` | Estado Git comprobado entonces, build Mac, datos externos al repo. |
| CONTINUAR Añadir instalador para macOS | `01a1baf3-f531-7362-baa6-eafe8cd3def5` | M5 Pro, FL Studio, pareja de instaladores, beta6 y publicación sin PR. |
| Prepare beta installer and README | `01a14a9a-f851-7ff3-b6bd-88a3ad47fb67` | Encargos repetidos de README/release y merge directo. |
| Auditoría de la app del 28 de octubre | `01a18434-9262-74a1-989b-be9ed03e442e` | Dos fases, rama de auditoría y posterior integración directa. |
| Añadir previews de proyectos | `01a158fa-a14c-7280-9345-19893a30d592` | Ajustes FL en sólo lectura aceptados y límites de las pruebas CLI. |
| FRONTEND | `01a12c23-5f02-7542-8c8d-7db8c772110f` | Temas, elección Inter/San Francisco y validación. |
| MEJORAS RENDIMIENTO | `01a1125d-37c9-7a72-b5c2-66ecc91c2d31` | Antecedentes de arranque; detalle histórico en los informes. |

No se han exportado chats completos, razonamientos internos, credenciales ni
información de otros proyectos. Las conversaciones largas se consultaron por
sus turnos recientes pertinentes; puede haber decisiones antiguas no recuperadas.

## Traslado y mantenimiento de esta memoria

Conservar `AGENTS.md`, este archivo y `ESTADO_TRABAJO.md` junto al resto de la
documentación en Git. Si hay cambios nuevos en el Mac, comparar antes de copiar
estos documentos para no sustituir decisiones posteriores. Una copia del paquete
es un snapshot: la documentación mantenida del repositorio es la referencia futura.

En el nuevo ordenador, instalar o copiar **la carpeta completa de la skill
personal `apple-design`**, con sus recursos relativos. En el origen está en
`C:/Users/marti/.codex/skills/apple-design/`; esa ruta no es portable. Los plugins
y sus cachés se gestionan mediante Codex en el equipo nuevo. Reconfigurar las
conexiones y herramientas allí; no es necesario copiar todo `.codex` para usar
esta memoria. Si existen preferencias globales propias en `~/.codex/AGENTS.md`,
revisarlas y trasladarlas por separado.

El paquete local `out/Remember-contexto-2026-11-14.zip` incluye una copia de esa
skill en `skills/apple-design/`: colocar esa carpeta en `~/.codex/skills/` del
Mac, conservando cualquier versión más nueva que ya esté instalada. Incluye
también las guías Markdown del proyecto y un LEEME con pasos de incorporación.

La biblioteca de previews, sus metadatos, proyectos DAW y samples son **datos
externos a Git**. Respaldarlos completos, con Remember/DAW cerrados, y configurar
las rutas Mac. Guardar `%APPDATA%/Remember` como referencia; no restaurar a ciegas
sus rutas Windows sobre `~/Library/Application Support/Remember`. No incluir
tokens ni credenciales en esta documentación o en un commit.

La documentación oficial explica que Codex carga `AGENTS.md` como instrucciones
de proyecto al iniciar. Un archivo de contexto adicional requiere una referencia
explícita como la añadida aquí al AGENTS del repositorio:
[Custom instructions with AGENTS.md](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
