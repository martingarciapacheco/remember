# Estado de trabajo y punto de reanudación

Actualizado el **14 de noviembre de 2026**. Sustituir las secciones de estado al
cerrar una tanda de trabajo; no convertir este archivo en un log infinito.
Las decisiones duraderas están en [CONTEXTO_PROYECTO.md](CONTEXTO_PROYECTO.md).

## Petición actual

El usuario ha trasladado el proyecto a otro ordenador y observa que allí falta
contexto sobre Remember y la forma de trabajar. Esta tanda prepara una memoria
portable y un mensaje de arranque; no cambia el comportamiento de app/plugin.

## Snapshot del repositorio de origen

### Continuación tras sincronizar con el trabajo del Mac

El usuario pidió descargar GitHub y continuar en su rama reciente del Mac antes
de hacer commit; después autorizó sincronizar los documentos. Se ejecutaron fetch,
checkout con seguimiento y pull fast-forward. Rama activa:
`codex/macos-feature-parity`, base `93461c355df0d4bf10d7b1b663281a1da72a4e14`
(`Improve macOS integration and recover Windows project paths`). Los tres documentos
locales se conservaron y se adaptaron a esa rama. Este commit de documentación se
destina a la misma rama, sin merge a main ni release.

Para continuar, leer primero las secciones **Unreleased development** y
**Local plugin installation** de `MAC_COMPATIBILITY.md`, el desarrollo sin publicar
de `DAW_COMPATIBILITY.md` y `DISPLAY_INVESTIGATION.md` si se toca pantalla.
La rama ya implementa mejoras Mac que no existían en beta6: FSEvents, asociación
automática parcial, renderizado FL nativo parcial, MediaPlayer, Dock y rutas
Windows importadas. Los límites beta6 de abajo son históricos.

La guía registra app 17/17, plugin 14/14 y ASan focalizado 3/3 en el Mac, más
pluginval nivel 5 y correcciones posteriores con validación dirigida. No se han
repetido aquí. Sigue pendiente aceptación de sesiones reales y Windows/Intel.
La configuración tuvo un fallo intermitente y pasó al repetir aisladamente;
no dar su causa por resuelta. Los logs `out/` del Mac no vienen con Git.

Se prepara commit/push únicamente de `AGENTS.md`, `CONTEXTO_PROYECTO.md` y
este archivo. Comprobar el resultado del push en Git antes de asumir que el otro
equipo ya recibió los documentos. El ZIP anterior es un snapshot previo a esta
actualización: usar los documentos de esta rama en GitHub para el traslado actual.

### Snapshot inicial, anterior al fetch de la rama Mac

- Ruta de origen: `E:/JUCE PLUGINS/Remember` (no exigir esta ruta en el Mac).
- Repositorio: `https://github.com/martingarciapacheco/remember.git`.
- Rama observada: `main`.
- HEAD observado: `56a2c3f6ae60c34637c0a35410efe0a825fb052d`.
- Asunto: `docs: record beta6 native validation and release evidence [skip ci]`.
- `VERSION`: `1.0.0-beta6`.
- Al empezar esta tanda, árbol limpio y referencia local `origin/main` coincidente.
  No se ha actualizado/verificado el remoto en esta tanda: no usar esta nota como
  prueba de que el Mac o GitHub continúan exactamente en ese commit.
- Cambios de esta tanda: lectura inicial y continuidad en `AGENTS.md`, y creación
  de `CONTEXTO_PROYECTO.md` y este estado. Se entregan como archivos locales;
  en aquel momento aún no se había hecho commit ni push ni publicación.

## Release y validación anteriores

La beta6 publicada se construyó desde
`8755ea32fb999e10df348f6c4fd6699066d7bf77`, diferente del HEAD documental anterior.
No confundir commit de instaladores, commit de documentación y checkout del Mac.

La evidencia ya registrada en `MAC_COMPATIBILITY.md` y `DAW_COMPATIBILITY.md`
indica: builds nativas de ambos productos/plataformas, pluginval nivel 5 y
verificación del contenido extraído; Mac arm64/macOS 15.8.2 app 15/15 y plugin
14/14; Windows app 16/16 y plugin 16/16. También hay validación Windows ASan y
vida útil nativa Mac. Son resultados históricos, no pruebas ejecutadas ahora.

Referencias archivadas en el registro:

- [Release beta6](https://github.com/martingarciapacheco/remember/releases/tag/v1.0.0-beta6).
- [Workflow conjunto](https://github.com/martingarciapacheco/remember/actions/runs/42857004283).
- Evidencia local original: `out/Remember-1.0.0-beta6-validation.zip`, si sigue
  disponible en el origen. `out/` está ignorado por Git; clonar no lo descarga.
  No afirmar que se ha incluido ese ZIP en el paquete de contexto.
- Primer timeout intermitente de Undo conservado en la evidencia; la repetición
  completa aprobó sin cambiar fuentes ni relajar límites. Su causa no se demostró.

## Pendientes reales registrados, sin iniciarlos automáticamente

- Aceptación física en M5 Pro/FL Studio: instalación/actualización/desinstalación,
  escaneo VST3, captura, guardar/restaurar, varias instancias y cierre/reapertura.
- CoreAudio, dispositivos, selectores, discos externos, permisos macOS, arrastre,
  escala de pantalla y textos EN/ES sobre el equipo del usuario.
- Ejecución Intel/Rosetta y otras versiones macOS no queda certificada por la
  build universal ni por pruebas ejecutadas en arm64.
- AU/AAX, firma Developer ID/notarización, render offline Mac/Ableton y nuevos
  lectores Used Sounds/Statistics siguen sujetos a sus registros de pendientes.
  No son todos tareas encargadas para la siguiente sesión.

## Entorno para desarrollar en Mac

- CMake >= 3.22, C++17 y herramientas Xcode apropiadas al generador elegido.
- JUCE fijado en `installer/dependencies.json`: 8.0.12, commit
  `29396c22c93392d6738e021b83196283d6e4d850`.
- Por defecto Mac busca JUCE en `out/tools/JUCE`; usar `-DJUCE_PATH=...` para otra ruta.
- CMake tiene entradas separadas `App/` y `Plugin/`; no existe CMake raíz.
- No reutilizar CMakeCache/builds Windows. Crear directorios de build Mac nuevos.
- Activar `REMEMBER_BUILD_TESTS=ON` para regresiones; mantener
  `REMEMBER_COPY_PLUGIN_AFTER_BUILD=OFF` para no sustituir el plugin instalado.
- Los productos pueden ser universales mientras los ejecutables de tests se
  compilan para la arquitectura nativa. Eso es intencionado.

Ejemplo de configuración/build **para ejecutar en el Mac**, después de disponer
del checkout JUCE fijado. Estos comandos no se han ejecutado en esta tanda:

```bash
cmake -S App -B out/mac-app -G Xcode \
  -DJUCE_PATH="$PWD/out/tools/JUCE" \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DREMEMBER_BUILD_TESTS=ON
cmake --build out/mac-app --config Release \
  --target RememberApp RememberAppTests --parallel 3

cmake -S Plugin -B out/mac-plugin -G Xcode \
  -DJUCE_PATH="$PWD/out/tools/JUCE" \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DREMEMBER_BUILD_TESTS=ON -DREMEMBER_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build out/mac-plugin --config Release \
  --target RememberPlugin_VST3 RememberTests --parallel 3
```

Enumerar primero las pruebas disponibles mediante
`ctest --test-dir out/mac-app -C Release -N` y su equivalente de plugin.
Seleccionar las pertinentes a la petición. La regresión completa usa
`ctest --test-dir <build-dir> -C Release --output-on-failure`.
Cambios frontend exigen builds app/VST3 y snapshots de ambos, EN/ES, tamaños
mínimo y normal. Las releases conservan todas las comprobaciones obligatorias.

Para instaladores usar siempre el workflow conjunto. Desde Mac también puede
despacharse con `gh workflow run release-installers.yml --ref main -f publish=false`
si `gh` está instalado y autenticado, después de commit/push del código a validar.
No despacharlo sólo por cargar este contexto.

## Validación de esta entrega

Sólo documentación. Verificados los enlaces Markdown relativos de los tres
documentos de continuidad, las rutas clave de código/build y la ausencia de
marcadores de decodificación inválida; `git diff --check` correcto. El paquete
de contexto incluye manifiesto SHA-256 para comprobar sus archivos. No se han
ejecutado builds ni regresiones de app/VST3 en esta tanda.

## Siguiente paso concreto en el Mac

Guardar y preservar cualquier cambio local del Mac. Hacer fetch de origin,
continuar en `codex/macos-feature-parity` y actualizar con pull fast-forward;
si hay divergencia o conflictos, conservar el trabajo local y resolver sin reset
ni sobrescritura indiscriminada. Usar estos documentos actualizados desde Git,
en lugar del ZIP inicial. Verificar builds y binarios instalados antes de cambiar
la instalación; no restaurar configuración Windows sobre el perfil Mac.

Comparar los tres documentos de continuidad con cualquier versión ya existente
allí e incorporarlos sin pisar cambios nuevos. Abrir la carpeta real de Remember
como proyecto de Codex y comenzar un chat con este mensaje:

> Continúa con Remember en este ordenador. Antes de modificar nada, lee AGENTS.md,
> CONTEXTO_PROYECTO.md y ESTADO_TRABAJO.md; consulta los documentos especializados
> que indiquen. Verifica el commit, la rama, los cambios locales, las herramientas
> y la skill apple-design disponibles aquí. Dime brevemente qué reglas has cargado,
> qué diferencias hay respecto al snapshot y qué falta para continuar. Conserva
> Windows/macOS, inglés/español y el diseño compartido. No reinicies auditorías
> históricas ni publiques por el mero hecho de leer pendientes. La tarea que quiero
> hacer ahora es: [escribir aquí la tarea concreta].

Esa respuesta sirve como comprobación de que se cargó el contexto. Si no reconoce
las reglas, revisar que se abrió el checkout correcto, el `AGENTS.md` activo y
posibles overrides globales/de directorio. Empezar un chat nuevo tras incorporar
las instrucciones evita depender del contexto ya cargado por una sesión anterior.
