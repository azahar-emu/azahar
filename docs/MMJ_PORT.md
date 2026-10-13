# Port de hacks de Citra MMJ a Azahar

Base: azahar master `bfa768727` (2026-10-12). Origen: weihuoya/citra (MMJ) @ `302b0c3` (2025-11-14).

## De donde salen los IDs
MMJ guarda los overrides por juego en dos sitios:
1. `LoadOverrides` en `src/core/core.cpp` (lista corta).
2. `src/android/app/src/main/assets/config/config-games.ini` (lista completa, por seccion).

La primera version del port solo uso (1). Ahora las listas se regeneran desde (2).

## Estado de la verificacion
- Compila: los archivos modificados y los que incluyen los headers tocados (ver "Compilacion").
- NO probado en ningun juego ni dispositivo.

## Portado
| Hack MMJ | HackType en Azahar | IDs | Juegos |
|---|---|---|---|
| skip_slow_draw | SKIP_SLOW_DRAW | 4 | New Atelier Rorona, My Hero Academia, DQM Joker 3 (+Professional) |
| skip_texture_copy | SKIP_TEXTURE_COPY | 19 | Mario Kart 7, Kid Icarus: Uprising, Luigi's Mansion: Dark Moon, Metroid Prime: Federation Force, Batman: Arkham Origins Blackgate, Professor Layton and the Miracle Mask (+Plus) |
| disable_clip_coef | DISABLE_CLIP_COEF | 9 | Star Fox 64, Picross 2, Art of Balance |
| use_linear_filter | FORCE_LINEAR_MAG_FILTER | 5 | Attack on Titan 1 / 1 CHAIN / 2 |
| y2r_perform_hack | SKIP_REDUNDANT_Y2R_CONVERSION | 2 | Danball Senki (solo en LoadOverrides) |
| accurate_mul_fast | ACCURATE_MULTIPLICATION (ampliada) | 57 en total | incluye Zelda OoT 3D, Majora's Mask 3D, A Link Between Worlds, Super Mario 3D Land, Kid Icarus: Uprising, Ever Oasis. No-op con ajustes por defecto (shaders_accurate_mul = true) |

## Notas de fidelidad
- disable_clip_coef: en el vertex shader normal el efecto es identico al de MMJ. En el camino de geometry shader
  (`EmitVtx`) MMJ ademas omite `gl_ClipDistance[0]` (plano fijo z <= 0); aqui ese plano se mantiene.
- skip_slow_draw: Tales of the Abyss (3 IDs en el ini, 1 en LoadOverrides) se excluye a proposito: la causa del
  crash se corrigio en Azahar (#7381). Inferido por coincidencia de sintomas, no probado.

## Ya existia en Azahar
- y2r_event_delay (FIFA): retraso minimo de 50000 ciclos aplicado siempre.
- Cache de shaders en disco, compilacion asincrona, presentacion asincrona: ajustes existentes.

## Inertes en MMJ (nadie lee el flag: nada que portar)
`accurate_rcp_rsq` (**todos los Pokemon 3DS** y Captain Toad), `accurate_max_min`, `skip_load_buffer`
(Metroid Prime: Federation Force), `merge_framebuffer`, `hw_gs_mode` / `disable_hw_gs` (BIOHAZARD Revelations),
`use_fence_sync`, `use_compatible_mode`. Se declaran e inicializan pero ninguna ruta de video_core/core los consulta.

## No portado, con motivo
- display_transfer_hack (Tales of the Abyss / Pac-Man Party 3D): remapea direcciones fisicas fijas; no validable sin el juego.
- core_downcount_hack y use_fmv_hack (`core_ticks_hack`): ajustes de temporizacion de CPU; Azahar usa una sola escala de reloj.
- skip_cpu_write (Lord of Magna, 3 IDs): depende del rasterizer cache que Azahar reescribio.
- stream_buffer_hack (Shovel Knight, Gunvolt, 1001 Spikes, Gravity Falls): especifico del stream buffer de OpenGL de MMJ.
- disable_shadow_rendering (Luigi's Mansion: Dark Moon): Azahar no tiene ajuste de sombras equivalente.
- new_3ds_mode (41 IDs): Azahar usa is_new_3ds=true por defecto y lo permite fijar por juego.
- game_region_*: Azahar selecciona la region automaticamente.

## Titulos populares
| Titulo | En MMJ | En esta rama |
|---|---|---|
| Zelda: Ocarina of Time 3D / Majora's Mask 3D / A Link Between Worlds | accurate_mul_fast (+ region) | ACCURATE_MULTIPLICATION |
| Pokemon X/Y, ORAS, Sun/Moon, USUM | solo accurate_rcp_rsq (inerte) | sin hack: MMJ no aporta nada |
| Mario Kart 7 | skip_texture_copy | SKIP_TEXTURE_COPY |
| Super Mario 3D Land | accurate_mul_fast | ACCURATE_MULTIPLICATION (+ DELAY_TEXTURE_COPY_COMPLETION de Azahar) |
| Kid Icarus: Uprising | accurate_mul_fast, skip_texture_copy | ambos |
| Animal Crossing, Smash, Monster Hunter, Kirby | sin entradas | sin hack |

## Procedencia (commits originales de MMJ)

GitHub permite un solo repositorio padre por fork (aqui, Azahar), asi que la relacion con Citra MMJ
(https://github.com/weihuoya/citra) se documenta con enlaces permanentes. Cada fila es el primer commit de MMJ donde
aparece por primera vez el nombre del ajuste (verificado con `git log -S<nombre>` sobre el historial completo, 8715
commits, HEAD `302b0c3`). Los mensajes de MMJ son muy breves y un commit puede agrupar otros cambios.

| Ajuste de MMJ | Commit | Autor | Fecha | Mensaje original |
|---|---|---|---|---|
| `skip_slow_draw` | e85a43cf92e446b4ac4af99a22b53655ea12b92e | weihuoya | 2020-07-06 | android project |
| `disable_clip_coef` | 0467d74465a73aba30b8ab712243f9f14b7bcb2d | weihuoya | 2020-09-21 | minor fx |
| `y2r_perform_hack` | 965594b140bf6c5c3f5caf6a674d5d3a8a44f5a7 | weihuoya | 2020-12-25 | compress shader cache |
| `use_linear_filter` | 21c231fbc2e1d1523a250330b2543b3c96c0875f | weihuoya | 2021-01-18 | use linear filter |
| `skip_texture_copy` | 3c93fb489497dd49b58afa5187e735609b149a53 | weihuoya | 2022-01-20 | skip texture copy |
| `config-games.ini` (creacion) | 79564b5f34fec812c6750b7165099023f83086a7 | weihuoya | 2022-04-19 | fx |
| version usada como fuente | 302b0c3bc4312bc96489c11584a97664894b343f | zhangwei | 2025-11-14 | cache |

URL de cada commit: `https://github.com/weihuoya/citra/commit/<hash>`.
Historial local: `git remote add mmj https://github.com/weihuoya/citra.git && git fetch mmj`.
