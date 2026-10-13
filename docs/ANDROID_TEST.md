# Probar la rama en Android Studio

## Requisitos (segun src/android/app/build.gradle.kts)
- Android SDK Platform 35, NDK `27.3.13750724`, CMake `3.25.0+` (SDK Manager > SDK Tools), JDK 17.
- ~15 GB libres. Primera compilacion de la parte nativa: larga (decenas de minutos en un portatil).

## Pasos
1. `git clone --recurse-submodules -b mmj-hacks-port https://github.com/axelconde7w7-gif/azahar-mmj.git`
   (si ya clonaste sin submodulos: `git submodule update --init --recursive`)
2. Abrir **`src/android`** (no la raiz) en Android Studio y esperar el Gradle sync.
3. Para compilar solo lo que usa un telefono, en `app/build.gradle.kts` linea 24:
   `val abiFilter = listOf("arm64-v8a")` (no lo subas al repo).
4. Build variant recomendada: **relWithDebInfo** (rinde como release, firma de debug, se instala junto a otras
   versiones con sufijo `.debug`). Terminal: `./gradlew assembleRelWithDebInfo`.

## Como probar cada hack (A/B)
Instala tambien el Azahar oficial y compara el mismo punto del juego en ambos.
| Hack | Juego | Que mirar |
|---|---|---|
| SKIP_TEXTURE_COPY | Mario Kart 7, Kid Icarus: Uprising | rendimiento y que no falten texturas/efectos en menus y pista |
| DISABLE_CLIP_COEF | Star Fox 64, Picross 2 | geometria que desaparece o se recorta mal en los bordes de pantalla |
| SKIP_SLOW_DRAW | DQM Joker 3, New Atelier Rorona, My Hero Academia | modelos o efectos que faltan frente al oficial (esto es lo que el hack omite a proposito) |
| FORCE_LINEAR_MAG_FILTER | Attack on Titan | texturas ampliadas suavizadas en vez de pixeladas |
| SKIP_REDUNDANT_Y2R_CONVERSION | Danball Senki | videos/cinematicas sin congelarse ni parpadear |
| ACCURATE_MULTIPLICATION (ampliada) | Zelda OoT 3D, Kid Icarus | sin cambios con ajustes por defecto; solo cambia si desactivas "accurate multiplication" |

Si un juego se ve peor con la rama que con el oficial, quita su ID de `src/common/hacks/hack_list.cpp`
(la lista del hack correspondiente) y reconstruye: cada hack es independiente.
