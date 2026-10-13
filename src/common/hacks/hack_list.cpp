// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "common/hacks/hack_manager.h"

namespace Common::Hacks {

HackManager hack_manager = {
    .entries = {

        // The following games cannot use the right eye disable hack due to the way they
        // handle rendering.
        {HackType::RIGHT_EYE_DISABLE,
         HackEntry{
             .mode = HackAllowMode::DISALLOW,
             // clang-format off
             .affected_title_ids =
                 {
                     // Luigi's Mansion
                     0x00040000001D1900,
                     0x00040000001D1A00,

                     // Luigi's Mansion 2
                     0x0004000000055F00,
                     0x0004000000076500,
                     0x0004000000076400,
                     0x00040000000D0000,

                     // Rayman Origins
                     0x000400000005A500,
                     0x0004000000084400,
                     0x0004000000057600,

                     // Minecraft: New Nintendo 3DS Edition
                     0x00040000001B8700, // USA
                     0x000400000017CA00, // EUR
                     0x000400000017FD00, // JPN
                 },
             // clang-format on
         }},

        // The following games require accurate multiplication to render properly.
        {HackType::ACCURATE_MULTIPLICATION,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // The Legend of Zelda: Ocarina of Time 3D
                     0x0004000000033400, // JPN
                     0x0004000000033500, // USA
                     0x0004000000033600, // EUR
                     0x000400000008F800, // KOR
                     0x000400000008F900, // CHI

                     // Mario & Luigi: Superstar Saga + Bowsers Minions
                     0x00040000001B8F00, // USA
                     0x00040000001B9000, // EUR
                     0x0004000000194B00, // JPN

                     // Mario & Luigi: Bowsers Inside Story + Bowser Jrs Journey
                     0x00040000001D1400, // USA
                     0x00040000001D1500, // EUR
                     0x00040000001CA900, // JPN

                     // Mario & Luigi: Paper Jam
                     0x0004000000132600, // JPN
                     0x0004000000132700, // USA
                     0x0004000000132800, // EUR
                     0x000400000018A100, // EUR (Demo)

                     // Added from Citra MMJ's accurate multiplication list (no region labels)
                     // Attack on Titan
                     0x00040000000DF800,
                     0x0004000000134500,
                     0x0004000000152000,
                     0x00040000001AA200,

                     // Super Mario 3D Land
                     0x0004000000053F00,
                     0x0004000000054000,
                     0x0004000000054100,
                     0x0004000000089E00,
                     0x0004000000089F00,

                     // Captain Toad: Treasure Tracker
                     0x00040000001CB000,
                     0x00040000001CB100,
                     0x00040000001CB200,

                     // The Legend of Zelda: A Link Between Worlds
                     0x00040000000EC200,
                     0x00040000000EC300,
                     0x00040000000EC400,

                     // New Super Mario Bros. 2
                     0x000400000007AD00,
                     0x000400000007AE00,
                     0x000400000007AF00,
                     0x00040000000B8A00,

                     // Jett Rocket II
                     0x0004000000079600,

                     // Cut the Rope
                     0x0004000000112600,
                     0x0004000000116700,

                     // Luigi's Mansion: Dark Moon
                     0x0004000000055F00,
                     0x0004000000076400,
                     0x0004000000076500,
                     0x00040000000D0000,

                     // Digimon World Re:Digitize Decode
                     0x00040000000AFC00,

                     // The Legend of Zelda: Majoras Mask 3D
                     0x00040000000D6E00,
                     0x0004000000125500,
                     0x0004000000125600,

                     // Lego City Undercover
                     0x00040000000AD500,
                     0x00040000000AD600,
                     0x0004000000154700,

                     // Luigi's Mansion
                     0x00040000001D1800,
                     0x00040000001D1900,
                     0x00040000001D1A00,
                 },
             // clang-format on
         }},

        {HackType::DECRYPTION_AUTHORIZED,
         HackEntry{
             .mode = HackAllowMode::ALLOW,
             // clang-format off
             .affected_title_ids =
                 {
                     // NIM
                     0x0004013000002C02, // Normal
                     0x0004013000002C03, // Safe mode
                     0x0004013020002C03, // New 3DS safe mode

                     // DLP
                     0x0004013000002802,
                 },
             // clang-format on
         }},

        {HackType::ONLINE_LLE_REQUIRED,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // eShop
                     0x0004001000020900, // JPN
                     0x0004001000021900, // USA
                     0x0004001000022900, // EUR
                     0x0004001000027900, // KOR
                     0x0004001000028900, // TWN

                     // System Settings
                     0x0004001000020000, // JPN
                     0x0004001000021000, // USA
                     0x0004001000022000, // EUR
                     0x0004001000026000, // CHN
                     0x0004001000027000, // KOR
                     0x0004001000028000, // TWN

                     // Nintendo Network ID Settings
                     0x000400100002BF00, // JPN
                     0x000400100002C000, // USA
                     0x000400100002C100, // EUR

                     // System Settings
                     0x0004003000008202, // JPN
                     0x0004003000008F02, // USA
                     0x0004003000009802, // EUR
                     0x000400300000A102, // CHN
                     0x000400300000A902, // KOR
                     0x000400300000B102, // TWN

                     // Pretendo Network's Nimbus
                     0x000400000D40D200,
                 },
             // clang-format on
         }},

        {HackType::REGION_FROM_SECURE,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // eShop
                     0x0004001000020900, // JPN
                     0x0004001000021900, // USA
                     0x0004001000022900, // EUR
                     0x0004001000027900, // KOR
                     0x0004001000028900, // TWN

                     // System Settings
                     0x0004001000020000, // JPN
                     0x0004001000021000, // USA
                     0x0004001000022000, // EUR
                     0x0004001000026000, // CHN
                     0x0004001000027000, // KOR
                     0x0004001000028000, // TWN

                     // Nintendo Network ID Settings
                     0x000400100002BF00, // JPN
                     0x000400100002C000, // USA
                     0x000400100002C100, // EUR

                     // System Settings
                     0x0004003000008202, // JPN
                     0x0004003000008F02, // USA
                     0x0004003000009802, // EUR
                     0x000400300000A102, // CHN
                     0x000400300000A902, // KOR
                     0x000400300000B102, // TWN

                     // NIM
                     0x0004013000002C02, // Normal
                     0x0004013000002C03, // Safe mode
                     0x0004013020002C03, // New 3DS safe mode

                     // ACT
                     0x0004013000003802, // Normal

                     // FRD
                     0x0004013000003202, // Normal
                     0x0004013000003203, // Safe mode
                     0x0004013020003203, // New 3DS safe mode
                 },
             // clang-format on
         }},
        {HackType::REQUIRES_SHADER_FIXUP,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids = {},
             // clang-format on
         }},
        {HackType::SPOOF_FRIEND_CODE_SEED,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Luigi's Mansion 3ds
                     0x00040000001D1800, // JPN
                     0x00040000001D1900, // USA
                     0x00040000001D1A00, // EUR
                 },
             // clang-format on
         }},
        {HackType::DELAY_TEXTURE_COPY_COMPLETION,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Super Mario 3D Land
                     0x0004000000054100, // JPN
                     0x0004000000054000, // USA
                     0x0004000000053F00, // EUR
                     0x0004000000089E00, // CHN
                     0x0004000000089D00, // KOR
                 },
             // clang-format on
         }},

        // Ported from Citra MMJ (skip_slow_draw): these games crash or stall when a draw cannot
        // be hardware accelerated and falls back to the software vertex path, so the draw is
        // skipped instead.
        // NOTE: MMJ also listed Tales of the Abyss / Pac-Man Party 3D here to avoid a crash in
        // the geometry pipeline reconfiguration. That was fixed at the root in Azahar (#7381),
        // so those titles are intentionally not included.
        {HackType::SKIP_SLOW_DRAW,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // New Atelier Rorona
                     0x000400000015CB00,

                     // My Hero Academia
                     0x000400000018E900,

                     // Dragon Quest Monsters: Joker 3
                     0x000400000016AD00,

                     // Dragon Quest Monsters: Joker 3 Professional
                     0x00040000001ACB00,
                 },
             // clang-format on
         }},

        // Ported from Citra MMJ (skip_texture_copy): when the source of a texture copy is not
        // present in the GPU surface cache, skip the (slow) software copy entirely.
        {HackType::SKIP_TEXTURE_COPY,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Mario Kart 7
                     0x000400000008B400,
                     0x0004000000030600,
                     0x0004000000030800,
                     0x0004000000030700,
                 },
             // clang-format on
         }},

        // Ported from Citra MMJ (disable_clip_coef): these games render incorrectly when the
        // user clip plane (clip_coef) is applied, so the second clip plane is disabled.
        {HackType::DISABLE_CLIP_COEF,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Star Fox 64 (as labeled in Citra MMJ)
                     0x0004000000049100,
                     0x0004000000030400,
                     0x0004000000049000,

                     // Picross 2 (as labeled in Citra MMJ)
                     0x0004000000187E00,
                     0x0004000000169A00,
                 },
             // clang-format on
         }},

        // Ported from Citra MMJ (y2r_perform_hack): these games start a new Y2R conversion while
        // the completion event of the previous one is still signaled. The redundant conversion
        // is skipped.
        {HackType::SKIP_REDUNDANT_Y2R_CONVERSION,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Danball Senki W Chou Custom (as labeled in Citra MMJ)
                     0x00040000000DCA00,

                     // Danball Senki WARS (as labeled in Citra MMJ)
                     0x00040000000F4000,
                 },
             // clang-format on
         }},

        // Ported from Citra MMJ (use_linear_filter): these games request nearest magnification
        // filtering but look better with linear filtering. Only the magnification filter is
        // promoted to linear, as in Citra MMJ.
        {HackType::FORCE_LINEAR_MAG_FILTER,
         HackEntry{
             .mode = HackAllowMode::FORCE,
             // clang-format off
             .affected_title_ids =
                 {
                     // Attack on Titan (as labeled in Citra MMJ)
                     0x00040000000DF800,

                     // Attack on Titan: Humanity in Chain (as labeled in Citra MMJ)
                     0x0004000000134500,
                     0x0004000000152000,

                     // Attack on Titan 2 (as labeled in Citra MMJ)
                     0x00040000001AA200,
                 },
             // clang-format on
         }},
    }};
}