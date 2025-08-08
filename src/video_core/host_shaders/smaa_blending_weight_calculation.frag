// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

#extension GL_GOOGLE_include_directive : enable

layout (binding = 0) uniform sampler2D edges_tex;
layout (binding = 1) uniform sampler2D area_tex;
layout (binding = 2) uniform sampler2D search_tex;

layout (location = 0) in vec2 tex_coord;
layout (location = 1) in vec2 pix_coord;
layout (location = 2) in vec4 offset[3];

layout (location = 0) out vec4 frag_color;

vec4 metrics = vec4(1.0 / textureSize(edges_tex, 0), textureSize(edges_tex, 0));
#define SMAA_RT_METRICS metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_ULTRA
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1

#include "opengl_smaa.glsl"

void main() {
    frag_color = SMAABlendingWeightCalculationPS(tex_coord,
                                       pix_coord,
                                       offset,
                                       edges_tex,
                                       area_tex,
                                       search_tex,
                                       vec4(0)
                                       );
}
