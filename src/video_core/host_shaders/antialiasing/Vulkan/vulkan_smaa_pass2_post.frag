vec3 LinearTosRGB(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec4 pixel = SMAANeighborhoodBlendingPS(frag_tex_coord, offset, screen_textures[1], screen_textures[0]);
    if (convert_colors == 2){
        pixel = vec4(LinearTosRGB(pixel.rgb), pixel.a);
    }
    color = pixel;
}
