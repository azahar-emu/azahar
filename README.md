# Azahar MMJ Port

> **Fork no oficial y experimental de [Azahar](https://github.com/azahar-emu/azahar).**
> Lleva a Azahar los arreglos por juego que acumuló **Citra MMJ**, un fork de Citra pensado para Android.
>
> *English summary at the [bottom](#english-summary).*

> [!WARNING]
> **Estado: compila, pero todavía no está probado en juegos.**
> No es una versión oficial de Azahar, no publica binarios y no está afiliado ni respaldado por el equipo de
> Azahar, por el de Citra, por el de Lime3DS, por los autores de Citra MMJ ni por Nintendo.

> [!NOTE]
> **Dónde está cada cosa.** La rama `master` es el código de Azahar más este README. Los cambios de código y toda
> la documentación están en la rama **[`mmj-hacks-port`](https://github.com/axelconde7w7-gif/azahar-mmj/tree/mmj-hacks-port)**.

## Qué buscamos

Durante años, [Citra MMJ](https://github.com/weihuoya/citra) fue acumulando conocimiento práctico sobre juegos concretos de 3DS: qué títulos
necesitan una multiplicación más precisa en los shaders, cuáles se rompen con cierto recorte de geometría, cuáles
se benefician de saltarse una copia de texturas lenta, etc. Ese conocimiento vive sobre todo en listas de
*title IDs* dentro de su código y de un archivo de configuración (`config-games.ini`).

El problema es que MMJ dejó de incorporar cambios de Citra en abril de 2020, no puede fusionarse con el código
actual de Azahar y su último commit es de noviembre de 2025. **Si nadie lo rescata, ese conocimiento se pierde.**
Este proyecto intenta:

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

Base: Azahar `master` en `bfa768727` (2026-10-12). Origen de los datos: Citra MMJ en [`302b0c3`](https://github.com/weihuoya/citra/commit/302b0c3bc4312bc96489c11584a97664894b343f) (2025-11-14).

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
**[`docs/MMJ_PORT.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/MMJ_PORT.md)**.

## Procedencia: de dónde sale cada dato

GitHub solo permite que un fork tenga **un** repositorio padre (aquí, Azahar), así que la relación con MMJ no
aparece en la cabecera del repositorio. Se documenta con enlaces permanentes a los commits originales de MMJ:

| Dato | Primer commit de MMJ donde aparece | Autor | Fecha |
|---|---|---|---|
| `skip_slow_draw` | [`e85a43c`](https://github.com/weihuoya/citra/commit/e85a43cf92e446b4ac4af99a22b53655ea12b92e) | weihuoya | 2020-07-06 |
| `disable_clip_coef` | [`0467d74`](https://github.com/weihuoya/citra/commit/0467d74465a73aba30b8ab712243f9f14b7bcb2d) | weihuoya | 2020-09-21 |
| `y2r_perform_hack` | [`965594b`](https://github.com/weihuoya/citra/commit/965594b140bf6c5c3f5caf6a674d5d3a8a44f5a7) | weihuoya | 2020-12-25 |
| `use_linear_filter` | [`21c231f`](https://github.com/weihuoya/citra/commit/21c231fbc2e1d1523a250330b2543b3c96c0875f) | weihuoya | 2021-01-18 |
| `skip_texture_copy` | [`3c93fb4`](https://github.com/weihuoya/citra/commit/3c93fb489497dd49b58afa5187e735609b149a53) | weihuoya | 2022-01-20 |
| listas completas de IDs (`config-games.ini`) | creado en [`79564b5`](https://github.com/weihuoya/citra/commit/79564b5f34fec812c6750b7165099023f83086a7); [versión usada aquí](https://github.com/weihuoya/citra/blob/302b0c3bc4312bc96489c11584a97664894b343f/src/android/app/src/main/assets/config/config-games.ini) | weihuoya / zhangwei | 2022-04-19 / 2025-11-14 |

Los mensajes de commit de MMJ suelen ser muy breves (`fx`, `cache`), así que cada commit puede agrupar otros
cambios: la tabla indica dónde aparece **por primera vez** el nombre del ajuste, no que ese commit sea solo ese hack.
Para consultar el historial completo en local: `git remote add mmj https://github.com/weihuoya/citra.git`.

## Compilar y probar

- Guía para Android Studio y tabla de qué mirar en cada juego: **[`docs/ANDROID_TEST.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/ANDROID_TEST.md)**.
- Instrucciones generales de compilación (escritorio): [wiki de Azahar](https://github.com/azahar-emu/azahar/wiki/Building-From-Source).
- Los cambios de código de este fork, aislados del resto: `git diff master...mmj-hacks-port -- src/`.
- README original de Azahar, conservado íntegro: [`docs/UPSTREAM_README.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/UPSTREAM_README.md).

## Transparencia sobre el uso de IA

El código y la documentación de este fork fueron escritos con **Claude (Anthropic)**, siguiendo la dirección y las
decisiones del propietario del repositorio. Los commits más recientes lo declaran con un trailer `Co-Authored-By`.

Esto tiene consecuencias que conviene decir en voz alta:

- Azahar publica una [política de uso de IA](AI-POLICY.md) que **prohíbe enviar como contribución código escrito
  total o sustancialmente por IA**. Por eso **este fork no se propone como pull request a Azahar**, y se pide que
  nadie lo haga en su nombre. Si a los mantenedores les interesa alguna idea, pueden reimplementarla a mano a partir
  de la documentación en [`docs/MMJ_PORT.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/MMJ_PORT.md).
- Una IA puede equivocarse. **Revisa el diff antes de confiar en él**, sobre todo en `video_core`.

## Créditos

Este proyecto no existiría sin el trabajo de otras personas. Casi todo lo valioso aquí es suyo:

- **[Azahar Emulator Project](https://github.com/azahar-emu/azahar)** y todas las personas que lo desarrollan: la
  base sobre la que se construye este fork, incluido el `HackManager` que hace posible el enfoque por título.
- **Citra Emulator Project** y sus colaboradores, creadores del emulador original del que descienden Azahar,
  Lime3DS y Citra MMJ.
- **Lime3DS** y sus colaboradores. Azahar ha tomado su relevo: conserva el ID de Play Store de Lime3DS e incluye
  migración de datos desde Lime3DS y Citra.
- **[weihuoya](https://github.com/weihuoya/citra)**, autor de **[Citra MMJ](https://github.com/weihuoya/citra)** entre 2019 y 2022 (94 commits propios desde abril de 2020). Los
  hacks, los *title IDs* y las observaciones por juego que este fork traduce son fruto de **su** trabajo y de sus
  pruebas con juegos reales. Commits firmados como *weihuoya* forman además parte de la historia de Citra y Azahar.
- **zhangwei**, que mantuvo Citra MMJ de 2023 a 2025 (23 commits, incluido el último) y cuya versión del archivo
  `config-games.ini` es la que se usa aquí como fuente de datos.
- Los autores de las bibliotecas externas que incluye Azahar (Dynarmic, entre otras), con sus propias licencias.

Aquí solo se adaptan sus ideas a una base nueva. Si eres autor de alguno de estos proyectos y quieres que se
corrija o retire algo, abre un issue en este repositorio.

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
ports the per-game compatibility hacks accumulated in **[Citra MMJ](https://github.com/weihuoya/citra)** (an Android-focused Citra fork,
maintained by *weihuoya* until 2022 and by *zhangwei* from 2023 to 2025) onto Azahar's current `HackManager`.

- **Goal:** preserve MMJ's per-title knowledge before it is lost, one hack per commit, with each hack active
  *only* for its listed title IDs (all other games behave exactly like upstream Azahar) and individually revertible.
- **Status:** the 38 affected source files compile cleanly and a data test against the real `HackManager` passes.
  **No game has been run yet.** No binaries are published. Not affiliated with Azahar, Citra, Lime3DS, Citra MMJ's
  authors, or Nintendo.
- **AI disclosure:** the code was written with Claude (Anthropic) under the repository owner's direction. Azahar's
  [AI policy](AI-POLICY.md) forbids submitting substantially AI-written code upstream, so **this fork is not
  intended as a pull request to Azahar**.
- **Credits:** Azahar Emulator Project, Citra Emulator Project, Lime3DS, and above all **weihuoya and zhangwei
  (Citra MMJ)**, whose per-game research this fork adapts. Provenance links to the original MMJ commits are in the
  [Procedencia](#procedencia-de-dónde-sale-cada-dato) section.
- **Details:** [`docs/MMJ_PORT.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/MMJ_PORT.md), [`docs/ANDROID_TEST.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/ANDROID_TEST.md),
  original README in [`docs/UPSTREAM_README.md`](https://github.com/axelconde7w7-gif/azahar-mmj/blob/mmj-hacks-port/docs/UPSTREAM_README.md).
