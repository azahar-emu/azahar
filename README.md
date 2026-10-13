# Azahar MMJ Port

> **Fork no oficial y experimental de [Azahar](https://github.com/azahar-emu/azahar).**
> Lleva a Azahar los arreglos por juego que acumuló **Citra MMJ**, un fork de Citra pensado para Android.
>
> *English summary at the [bottom](#english-summary).*

> [!WARNING]
> **Estado: compila, pero todavía no está probado en juegos.**
> No es una versión oficial de Azahar, no publica binarios y no está afiliado ni respaldado por el equipo de
> Azahar, por el de Citra, por el de Lime3DS, por el autor de Citra MMJ ni por Nintendo.

## Qué buscamos

Durante años, [Citra MMJ](https://github.com/weihuoya/citra) (de *weihuoya*) fue acumulando conocimiento práctico
sobre juegos concretos de 3DS: qué títulos necesitan una multiplicación más precisa en los shaders, cuáles se
rompen con cierto recorte de geometría, cuáles se benefician de saltarse una copia de texturas lenta, etc.
Ese conocimiento vive sobre todo en listas de *title IDs* dentro de su código y de un archivo de configuración
(`config-games.ini`).

El problema es que MMJ se separó de Citra en 2020, no puede fusionarse con el código actual de Azahar y su último
commit es de noviembre de 2025. **Si nadie lo rescata, ese conocimiento se pierde.** Este proyecto intenta:

1. **Rescatar y documentar** qué problema resuelve cada hack de MMJ y en qué juegos.
2. **Traducir a la arquitectura actual de Azahar** los que siguen teniendo sentido, reutilizando su
   `HackManager` (`src/common/hacks/`), un hack por commit.
3. **Descartar, explicando por qué,** lo que Azahar ya resolvió mejor, lo que no se puede validar sin el juego
   o lo que MMJ nunca llegó a usar de verdad.
4. **Validar en dispositivos Android reales**, juego por juego, comparando contra Azahar oficial.

### Principios

- **Precisión primero.** Cada hack actúa únicamente sobre los títulos listados por ID. Para cualquier otro juego
  el comportamiento es idéntico al de Azahar.
- **Reversible.** Los hacks son independientes. Si un juego va peor que en Azahar oficial, se quita su ID de
  `src/common/hacks/hack_list.cpp` y listo.
- **Honestidad sobre el estado.** Se dice qué está verificado y qué no (ver abajo).

### Lo que *no* buscamos

- Reemplazar o competir con Azahar. Es un experimento sobre su base.
- Reescribir el motor ni mezclar mejoras de rendimiento generales.
- Enviar estos cambios a Azahar como pull request (ver [Transparencia sobre el uso de IA](#transparencia-sobre-el-uso-de-ia)).
- Facilitar la piratería. Ver [Aviso legal](#aviso-legal).

## Estado actual

Base: Azahar `master` en `bfa768727` (2026-10-12). Origen de los datos: Citra MMJ en `302b0c3` (2025-11-14).

| Hack de MMJ | Hack en Azahar | IDs | Juegos (ejemplos) |
|---|---|---:|---|
| `skip_texture_copy` | `SKIP_TEXTURE_COPY` | 19 | Mario Kart 7, Kid Icarus: Uprising, Metroid Prime: Federation Force |
| `disable_clip_coef` | `DISABLE_CLIP_COEF` | 9 | Star Fox 64, Picross 2, Art of Balance |
| `use_linear_filter` | `FORCE_LINEAR_MAG_FILTER` | 5 | Attack on Titan |
| `skip_slow_draw` | `SKIP_SLOW_DRAW` | 4 | Dragon Quest Monsters: Joker 3, New Atelier Rorona |
| `y2r_perform_hack` | `SKIP_REDUNDANT_Y2R_CONVERSION` | 2 | Danball Senki |
| lista de multiplicación precisa | `ACCURATE_MULTIPLICATION` (ampliada) | 57 | Zelda: Ocarina of Time 3D, Super Mario 3D Land |

**Qué está verificado y qué no:**

- ✅ Los 38 archivos fuente afectados compilan con la configuración real del proyecto, sin errores ni advertencias.
- ✅ Una prueba contra el `HackManager` real comprueba que cada título recibe (o no) su hack, sin IDs duplicados.
- ❌ **No se ha ejecutado ningún juego.** No hay ninguna garantía de que un hack mejore (o no empeore) un título.
- ❌ No se ha compilado el proyecto completo ni el APK de Android.

Detalle completo, notas de fidelidad frente a MMJ y la lista de lo que **no** se portó y por qué:
**[`docs/MMJ_PORT.md`](docs/MMJ_PORT.md)**.

## Compilar y probar

- Guía para Android Studio y tabla de qué mirar en cada juego: **[`docs/ANDROID_TEST.md`](docs/ANDROID_TEST.md)**.
- Instrucciones generales de compilación (escritorio): [wiki de Azahar](https://github.com/azahar-emu/azahar/wiki/Building-From-Source).
- Los cambios de este fork, aislados del resto: `git diff master...mmj-hacks-port -- src/`.
- README original de Azahar, conservado íntegro: [`docs/UPSTREAM_README.md`](docs/UPSTREAM_README.md).

> [!NOTE]
> El trabajo vive en la rama **`mmj-hacks-port`**. La rama `master` es un espejo del Azahar oficial.

## Transparencia sobre el uso de IA

El código y la documentación de este fork fueron escritos con **Claude (Anthropic)**, siguiendo la dirección y las
decisiones del propietario del repositorio. Los commits más recientes lo declaran con un trailer `Co-Authored-By`.

Esto tiene consecuencias que conviene decir en voz alta:

- Azahar publica una [política de uso de IA](AI-POLICY.md) que **prohíbe enviar como contribución código escrito
  total o sustancialmente por IA**. Por eso **este fork no se propone como pull request a Azahar**, y se pide que
  nadie lo haga en su nombre. Si a los mantenedores les interesa alguna idea, pueden reimplementarla a mano a partir
  de la documentación en [`docs/MMJ_PORT.md`](docs/MMJ_PORT.md).
- Una IA puede equivocarse. **Revisa el diff antes de confiar en él**, sobre todo en `video_core`.

## Créditos

Este proyecto no existiría sin el trabajo de otras personas. Casi todo lo valioso aquí es suyo:

- **[Azahar Emulator Project](https://github.com/azahar-emu/azahar)** y todas las personas que lo desarrollan: la
  base sobre la que se construye este fork, incluido el `HackManager` que hace posible el enfoque por título.
- **Citra Emulator Project** y sus colaboradores, creadores del emulador original del que descienden todos los
  demás, incluidos Azahar y MMJ.
- **Lime3DS** y sus colaboradores, cuyo trabajo continúa hoy dentro de Azahar.
- **[weihuoya](https://github.com/weihuoya)** (y **zhangwei**), autores de
  **[Citra MMJ](https://github.com/weihuoya/citra)**. Los hacks, los *title IDs* y las observaciones por juego que
  este fork traduce son fruto de **su** trabajo y de sus pruebas con juegos reales. Aquí solo se adaptan a una
  base nueva.
- Los autores de las bibliotecas externas que incluye Azahar (Dynarmic, entre otras), con sus propias licencias.

Si eres autor de alguno de estos proyectos y quieres que se corrija o retire algo de aquí, abre un issue.

## Licencia

Azahar, Citra y Citra MMJ se distribuyen bajo licencias GPL. Cada archivo conserva su aviso de copyright y su
licencia original (la mayoría **GPLv2 o posterior**; algunos archivos más recientes, **GPLv3 o posterior**).
Consulta las cabeceras de cada archivo, [`LICENSE.txt`](LICENSE.txt) y [`misc/licenses/`](misc/licenses/).
Los cambios de este fork se publican bajo los mismos términos.

## Reportar problemas

- **Problemas de este fork** (un hack que rompe un juego, un error de compilación): abre un issue **aquí**.
- **No** reportes en el repositorio de Azahar fallos observados con compilaciones de este fork: no son suyos
  y les harías perder tiempo.
- Si un problema también ocurre en Azahar oficial, repórtalo allí siguiendo sus guías.

## Aviso legal

Este proyecto es un emulador y no incluye juegos, claves ni firmware de Nintendo, ni ayuda a obtenerlos.
Úsalo únicamente con copias de juegos que hayas obtenido legalmente de tu propia consola.
Nintendo y Nintendo 3DS son marcas de Nintendo.

---

## English summary

**Unofficial, experimental fork of [Azahar](https://github.com/azahar-emu/azahar)** (a Nintendo 3DS emulator) that
ports the per-game compatibility hacks accumulated in **[Citra MMJ](https://github.com/weihuoya/citra)** (an
Android-focused Citra fork by *weihuoya*, last updated Nov 2025) onto Azahar's current `HackManager`.

- **Goal:** preserve MMJ's per-title knowledge before it is lost, one hack per commit, with each hack active
  *only* for its listed title IDs (all other games behave exactly like upstream Azahar) and individually revertible.
- **Status:** the 38 affected source files compile cleanly and a data test against the real `HackManager` passes.
  **No game has been run yet.** No binaries are published. Not affiliated with Azahar, Citra, Lime3DS, Citra MMJ's
  author, or Nintendo.
- **AI disclosure:** the code was written with Claude (Anthropic) under the repository owner's direction. Azahar's
  [AI policy](AI-POLICY.md) forbids submitting substantially AI-written code upstream, so **this fork is not
  intended as a pull request to Azahar**.
- **Credits:** Azahar Emulator Project, Citra Emulator Project, Lime3DS, and above all **weihuoya / zhangwei
  (Citra MMJ)**, whose per-game research this fork adapts.
- **Details:** [`docs/MMJ_PORT.md`](docs/MMJ_PORT.md), [`docs/ANDROID_TEST.md`](docs/ANDROID_TEST.md),
  original README in [`docs/UPSTREAM_README.md`](docs/UPSTREAM_README.md).
