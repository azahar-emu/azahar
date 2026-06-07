// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

//? #version 430 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

uniform vec4 i_resolution;
uniform vec4 o_resolution;
uniform vec2 cursor_pos;
uniform bool cursor_enable;
uniform int layer;
in vec2 pixelUnit;
void main() {
    vec4 pixel = texture(color_texture, frag_tex_coord);
    vec2 rfrag_tex_coord = vec2(frag_tex_coord.y, frag_tex_coord.x);
    //Cursor
    if (cursor_enable){
        //Black Outline
        if (rfrag_tex_coord.x <= (cursor_pos.x + (2.0*pixelUnit.x)) &&
            rfrag_tex_coord.x >= (cursor_pos.x - (1.0*pixelUnit.x))) {
            if (rfrag_tex_coord.y <= (cursor_pos.y + (5.0*pixelUnit.y)) &&
                rfrag_tex_coord.y >= (cursor_pos.y - (4.0*pixelUnit.y))) {
                pixel = vec4(0.0, 0.0, 0.0, 1.0);
            }
        }

        if (rfrag_tex_coord.y <= (cursor_pos.y + (2.0*pixelUnit.y)) &&
            rfrag_tex_coord.y >= (cursor_pos.y - (1.0*pixelUnit.y))) {
            if (rfrag_tex_coord.x <= (cursor_pos.x + (5.0*pixelUnit.x)) &&
                rfrag_tex_coord.x >= (cursor_pos.x - (4.0*pixelUnit.x))) {
                pixel = vec4(0.0, 0.0, 0.0, 1.0);
            }
        }
        //White Cross
        if (rfrag_tex_coord.x <= (cursor_pos.x + (1.0*pixelUnit.x)) &&
            rfrag_tex_coord.x >= (cursor_pos.x - (0.0*pixelUnit.x))) {
            if (rfrag_tex_coord.y <= (cursor_pos.y + (4.0*pixelUnit.y)) &&
                rfrag_tex_coord.y >= (cursor_pos.y - (3.0*pixelUnit.y))) {
                pixel = vec4(1.0, 1.0, 1.0, 1.0);
            }
        }

        if (rfrag_tex_coord.y <= (cursor_pos.y + (1.0*pixelUnit.y)) &&
            rfrag_tex_coord.y >= (cursor_pos.y - (0.0*pixelUnit.y))) {
            if (rfrag_tex_coord.x <= (cursor_pos.x + (4.0*pixelUnit.x)) &&
                rfrag_tex_coord.x >= (cursor_pos.x - (3.0*pixelUnit.x))) {
                pixel = vec4(1.0, 1.0, 1.0, 1.0);
            }
        }
    }

    color = vec4(pixel.rgb, 1.0);
}
