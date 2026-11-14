# Investigación de la banda horizontal — 11 de noviembre de 2026

## Conclusión

No se ha identificado una causa reproducible en Remember. La evidencia orienta
hacia una diferencia entre la imagen capturada y su presentación en la pantalla,
pero no distingue entre procesamiento/controlador de pantalla y panel. Tampoco
permite descartar que abrir Remember actúe como desencadenante. No se ha probado
un fallo físico ni debe presentarse la retención de imagen como diagnóstico.

El usuario observó una banda clara de ancho completo en el centro de la pantalla
integrada, persistente tras ⌘Q, que desapareció espontáneamente. La foto HEIC
muestra la franja atravesando dos zonas de Recordatorios. El PNG posterior no la
muestra. Son capturas de momentos distintos, por lo que su comparación no prueba
que ambos estados físicos fueran exactamente iguales.

Análisis del PNG original 3024×1964, RGBA de 8 bits: en las filas 785, 864, 903,
942, 982, 1021, 1060 y 1139, a 60/70/80/90 % del ancho, todos los puntos del fondo
son RGB (28,28,28). No hay un salto de luminancia en esa muestra. La comprobación
usó decodificación PNG sin pérdida, sin modificar el archivo original.

## Revisión del código y sus límites

| Área | Resultado |
|---|---|
| Arranque (`Main.cpp`, `MainComponent.cpp`) | Tema propio del standalone, registro de logs y creación de AppShell; configuración/búsquedas asíncronas. No se encontró un ajuste global de pantalla. |
| Ventana (`AppShell.cpp`) | Marco nativo; restaura tamaño/maximizado guardados. La cruz oculta la ventana; salir destruye la ventana y espera el cierre de trabajadores. No crea una superficie horizontal de ancho de pantalla. |
| Reapertura (`MacDockReopen.mm`) | Añade el callback del delegado para llamar a `open()`. No dibuja ni controla brillo o salida de vídeo. |
| Tema y dibujo (`RememberLookAndFeel.h`, `MainComponent.cpp`) | Colores SDR, relleno del fondo y superficies limitadas a componentes. El modo claro/oscuro es interno; escucha la apariencia del sistema, sin cambiarla. |
| Tooltips, menús y diálogos | Tooltips y menús pueden ser ventanas transparentes/topmost. Su geometría depende del contenido y sus recursos se liberan por JUCE. Ningún código revisado crea una banda fija a pantalla completa. Una ventana propia ordinaria no explica por sí sola la persistencia tras terminar el proceso. |
| Renderizador JUCE 8.0.12 | CoreGraphics por defecto y capas AppKit con dibujo asíncrono. Existe una ruta opcional CAMetalLayer detrás de `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS`; no se encontró esa definición en la configuración del producto ni en el checkout. No hay fundamento para atribuir el efecto a esa ruta opcional. Esto no excluye composición/aceleración internas de macOS. |
| API de pantalla | No se encontraron llamadas propias a cambios de gamma, brillo, modo/resolución, HDR/EDR ni overlays globales en app/shared/plugin. La búsqueda textual es evidencia de alcance, no una prueba formal sobre todo el framework o el sistema operativo. |
| Integraciones nuevas | FSEvents opera sobre archivos; asociación AppKit lee ventanas del propio host; MediaPlayer publica metadatos/controles. No se encontró un mecanismo que deje una franja dibujada por estas integraciones. |
| Biblioteca/audio | El arranque consulta proyectos y metadatos en workers. El dispositivo de audio se abre a demanda. No hay relación demostrada entre esa carga y la banda. |

Se revisaron también los caminos de selección de previews, iconos de DAW,
restauración de geometría, primer inicio y cierre asíncrono. El cuadro «Buscando…»
visible en las imágenes no corresponde a un `ThreadWithProgressWindow` ni a ese
literal en el código de Remember revisado; no se ha identificado su aplicación
propietaria y no se le atribuye la banda.

## Registros y pruebas

El log personal registra salida a las 23:36:54.958; las imágenes son de alrededor
de 23:38 y 23:40. Esto es coherente con la persistencia comunicada, aunque el log
no acredita por sí solo la ausencia de cualquier otro proceso/copia del producto.
No incluye telemetría de GPU o luminancia que explique el incidente.

Revalidación nativa: **3/3** suites aprobadas:
`RememberMacPlatform`, `RememberAppFrontend`, `RememberAppColdStartup`.
Tiempo total 51,23 s; evidencia local `out/mac-display-investigation-tests.log`
y `out/mac-app/display-investigation.xml`. Cubren delegado de reapertura,
minimización, cierre, arranque y frontend, no defectos visibles exclusivamente
en el panel físico. La validación previa con ASan tampoco certifica ese panel.

Se observaron dos incidencias separadas: el log intenta recuperar `Exports` como
una grabación sin manifest y el icono de bandeja se configura sin la imagen
`templateImage` que exige el backend Mac de JUCE. Merecen corrección independiente;
no hay evidencia de que expliquen esta banda. No se han modificado por esta auditoría.

## Siguiente evidencia necesaria

Reproducción controlada desde pantalla normal: comprobar antes/durante/después de
abrir y salir de la misma build, con foto y captura tomadas juntas. Si reaparece,
comparar en una build de diagnóstico CoreGraphics habitual frente al renderizador
software de JUCE, registrando ventanas y tiempos; cambiar el renderer no es aún
una corrección justificada. Si aparece sin Remember o durante arranque del Mac,
ampliar la investigación al sistema/pantalla. No reiniciar ni cambiar frecuencia,
perfil de color o ajustes del usuario automáticamente.

No se han cambiado el renderizado de producción, ajustes de pantalla, proyectos,
audios ni permisos del sistema durante esta investigación. La revisión no prueba
una causa raíz; documenta resultados negativos y la evidencia que falta.
