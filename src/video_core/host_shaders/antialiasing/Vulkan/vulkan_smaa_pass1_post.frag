void main() {
    vec4 subsampleIndices = vec4(0.0);
    color = SMAABlendingWeightCalculationPS(frag_tex_coord, pixcoord, offset, screen_textures[0], screen_textures[1], screen_textures[2], subsampleIndices);
}
