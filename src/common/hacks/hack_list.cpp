// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hacks/hack_manager.h"

namespace Common::Hacks {

HackManager hack_manager = {
    .entries = {

        // The following games cannot use the right eye disable hack due to the way they
        // handle rendering.
        {HackType::RIGHT_EYE_DISABLE,
         HackEntry{
             .mode = HackAllowMode::DISALLOW,
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
                 },
         }},

        // The following games require accurate multiplication to render properly.
        {HackType::ACCURATE_MULTIPLICATION,
         HackEntry{
             .mode = HackAllowMode::FORCE,
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
                 },
         }},

        {HackType::DECRYPTION_AUTHORIZED,
         HackEntry{
             .mode = HackAllowMode::ALLOW,
             .affected_title_ids =
                 {
                     // NIM
                     0x0004013000002C02, // Normal
                     0x0004013000002C03, // Safe mode
                     0x0004013020002C03, // New 3DS safe mode
                 },
         }},

        {HackType::ONLINE_LLE_REQUIRED,
         HackEntry{
             .mode = HackAllowMode::FORCE,
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
         }},

        {HackType::REGION_FROM_SECURE,
         HackEntry{
             .mode = HackAllowMode::FORCE,
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
         }},

    }};
}