# Port de hacks de Citra MMJ a Azahar

Base: azahar master `bfa768727` (2026-10-12). Origen: weihuoya/citra (MMJ), `LoadOverrides` en `src/core/core.cpp`.
Estado: ningun cambio se ha compilado como arbol completo ni probado en juegos. Solo se verifico
`hack_list.cpp` con g++ y que la serie aplica sobre master.

## Portado (un commit por hack)
| Hack MMJ | HackType en Azahar | Juegos |
|---|---|---|
| skip_slow_draw | SKIP_SLOW_DRAW | New Atelier Rorona, My Hero Academia, DQM Joker 3 (+Professional) |
| skip_texture_copy | SKIP_TEXTURE_COPY | Mario Kart 7 |
| disable_clip_coef | DISABLE_CLIP_COEF | Star Fox 64, Picross 2 |
| y2r_perform_hack | SKIP_REDUNDANT_Y2R_CONVERSION | Danball Senki (2 titulos) |
| accurate_mul (lista) | ACCURATE_MULTIPLICATION (ampliada) | +36 IDs / 12 juegos; no-op con ajustes por defecto |
| use_linear_filter | FORCE_LINEAR_MAG_FILTER | Attack on Titan (4 IDs) |

## Ya existia en Azahar
- y2r_event_delay (FIFA): Azahar aplica siempre un retraso minimo de 50000 ciclos.
- Cache de shaders en disco, compilacion asincrona, presentacion asincrona: ajustes existentes.

## No portado, con motivo
- Tales of the Abyss / Pac-Man Party 3D en skip_slow_draw: causa raiz corregida en Azahar (#7381). Inferido por sintomas, no probado.
- display_transfer_hack: remapea direcciones fisicas fijas de framebuffer; no se puede validar sin el juego.
- core_downcount_hack: subreloj por nucleo ({1,4,2,2}); Azahar tiene una sola escala de reloj. Alternativa: `cpu_clock_percentage` por juego.
- skip_cpu_write: ningun titulo lo activa en MMJ.
- Forzar New 3DS por titulo: Azahar usa is_new_3ds=true por defecto y permite fijarlo por juego.

## Sin analizar (ajustes de MMJ fuera de LoadOverrides)
stream_buffer_hack (MMJ lo desactiva para Shovel Knight, Gunvolt, 1001 Spikes, Gravity Falls),
merge_framebuffer, skip_load_buffer, hw_gs_mode, use_fence_sync, y los cambios de shaders para Mali/Adreno.
