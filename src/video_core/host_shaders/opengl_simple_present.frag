// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

//? #version 430 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;

uniform int convert_colors;

vec3 sRGBToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 LinearTosRGB(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec4 pixel = texture(color_texture, frag_tex_coord);
    if (convert_colors == 2){
        pixel = vec4(LinearTosRGB(pixel.rgb), pixel.a);
    } else if (convert_colors == 1){
        pixel = vec4(sRGBToLinear(pixel.rgb), pixel.a);
    }
    color = pixel;
}
