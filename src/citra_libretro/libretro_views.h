// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include "libretro.h"

// RetroArch's video views API as of libretro/RetroArch#19811, until the vendored libretro.h
// has it.
#ifndef RETRO_ENVIRONMENT_SET_VIDEO_VIEWS

#define RETRO_ENVIRONMENT_SET_VIDEO_VIEWS (95 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS (96 | RETRO_ENVIRONMENT_EXPERIMENTAL)

#define RETRO_VIDEO_VIEWS_STATUS_PRESENTS (1 << 0)
#define RETRO_VIDEO_VIEWS_STATUS_STEREO (1 << 1)
// A headset is tracking; only set together with PRESENTS and STEREO.
#define RETRO_VIDEO_VIEWS_STATUS_HMD (1 << 2)

#define RETRO_VIDEO_VIEW_EYE_NONE 0
#define RETRO_VIDEO_VIEW_EYE_LEFT 1
#define RETRO_VIDEO_VIEW_EYE_RIGHT 2
#define RETRO_VIDEO_VIEWS_MAX 8
// The map's LEFT and RIGHT views were drawn from the frontend's eye poses; Azahar's never are.
#define RETRO_VIDEO_VIEWS_FLAG_TRACKED (1 << 0)

enum retro_vr_reference_space {
    RETRO_VR_REFERENCE_SPACE_LOCAL = 0,
    RETRO_VR_REFERENCE_SPACE_STAGE = 1,
    RETRO_VR_REFERENCE_SPACE_DUMMY = INT_MAX
};

struct retro_video_view {
    unsigned x;
    unsigned y;
    unsigned width;
    unsigned height;
    unsigned screen;
    unsigned eye;
    float aspect_ratio;
};

struct retro_video_views {
    const struct retro_video_view* views;
    unsigned num_views;
    unsigned flags;
    enum retro_vr_reference_space reference_space;
    // Outputs: the frontend writes these back while a headset is tracking.
    unsigned recommended_view_width;
    unsigned recommended_view_height;
};

#endif
