// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

//? #version 430 core

layout(location = 0) out vec2 dst_coord;

layout(location = 0) uniform mediump ivec2 dst_size;

#ifdef VULKAN
#define gl_VertexID gl_VertexIndex
#endif

const vec2 vertices[4] =
vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));

void main() {
    gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
    dst_coord = (vertices[gl_VertexID] / 2.0 + 0.5) * vec2(dst_size);
}
