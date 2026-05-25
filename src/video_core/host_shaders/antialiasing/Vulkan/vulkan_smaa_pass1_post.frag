void main() {
    vec4 subsampleIndices = vec4(0.0);
    color = SMAABlendingWeightCalculationPS(frag_tex_coord, pixcoord, offset, color_texture, areaTex, searchTex, subsampleIndices);
}
